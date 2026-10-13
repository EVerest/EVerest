// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "machine.hpp"

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

#include <algorithm>
#include <chrono>
#include <endian.h>
#include <memory>

namespace everest::lib::slac::sm::ev {

namespace {

using Result = EvState::Result;

std::unique_ptr<EvState> make_reset(SlacEvMachine& m);
std::unique_ptr<EvState> make_failed(SlacEvMachine& m);
std::unique_ptr<EvState> make_wait_parm_cnf(SlacEvMachine& m);

template <StateId Id> struct EvLevelState : EvState {
    EvLevelState(SlacEvMachine& machine_, char const* name_, std::chrono::milliseconds timeout = {}) :
        machine(machine_), ctx(machine_.ctx), state_name(name_) {
        state_timer.duration = timeout;
    }
    char const* name() const override {
        return state_name;
    }
    int id() const override {
        return static_cast<int>(Id);
    }
    void deadlines(earliest_deadline& next) const override {
        if (has_timer) {
            state_timer.deadlines(next);
        }
    }

protected:
    // States with a timeout arm it on entry.
    void arm_timer() {
        has_timer = true;
        state_timer.arm(ctx.current_time);
    }
    bool timed_out() const {
        return state_timer.state_timeout(ctx.current_time);
    }
    // Every state but Reset and Idle drops the matching run on reset.
    Result reset_matching() {
        machine.parm_req_attempt_count = 0;
        machine.match_req_attempt_count = 0;
        machine.active_session = {};
        machine.start_atten_char_count = 0;
        machine.mnbc_sound_count = 0;
        machine.pending_nmk.fill(0);
        return transition(make_reset(machine));
    }

    SlacEvMachine& machine;
    fsm::ev::Context& ctx;

private:
    StateTimer state_timer;
    bool has_timer{false};
    char const* state_name;
};

std::chrono::milliseconds configured_or(std::chrono::milliseconds configured, std::chrono::milliseconds fallback) {
    return configured.count() > 0 ? configured : fallback;
}

void send_slac_parm_req(SlacEvMachine& m) {
    messages::cm_slac_parm_req req{};
    req.application_type = defs::COMMON_APPLICATION_TYPE;
    req.security_type = defs::COMMON_SECURITY_TYPE;
    copy_to_wire(req.run_id, m.active_session.run_id);

    if (not m.ctx.send_slac_message(fsm::ev::Context::BROADCAST_MAC, req)) {
        m.ctx.log_warn("Failed to send CM_SLAC_PARM.REQ");
    }
    m.parm_req_attempt_count++;
}

void send_slac_match_req(SlacEvMachine& m) {
    messages::cm_slac_match_req match_req{};
    match_req.application_type = defs::COMMON_APPLICATION_TYPE;
    match_req.security_type = defs::COMMON_SECURITY_TYPE;
    match_req.mvf_length = htole16(defs::CM_SLAC_MATCH_REQ_MVF_LENGTH);
    std::fill(std::begin(match_req.pev_id), std::end(match_req.pev_id), std::uint8_t{0});
    copy_to_wire(match_req.pev_mac, m.ctx.ev_host_mac);
    std::fill(std::begin(match_req.evse_id), std::end(match_req.evse_id), std::uint8_t{0});
    copy_to_wire(match_req.evse_mac, m.active_session.evse_mac);
    copy_to_wire(match_req.run_id, m.active_session.run_id);
    std::fill(std::begin(match_req._reserved), std::end(match_req._reserved), std::uint8_t{0});

    if (not m.ctx.send_slac_message(m.active_session.evse_mac, match_req)) {
        m.ctx.log_warn("Failed to send CM_SLAC_MATCH.REQ");
    }
    m.match_req_attempt_count++;
}

void send_next_sounding(SlacEvMachine& m) {
    auto& ctx = m.ctx;
    if (m.start_atten_char_count < defs::C_EV_START_ATTEN_CHAR_INDS) {
        messages::cm_start_atten_char_ind msg{};
        msg.application_type = defs::COMMON_APPLICATION_TYPE;
        msg.security_type = defs::COMMON_SECURITY_TYPE;
        msg.num_sounds = defs::C_EV_MATCH_MNBC;
        msg.timeout = static_cast<std::uint8_t>((defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100);
        msg.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
        copy_to_wire(msg.forwarding_sta, ctx.ev_host_mac);
        copy_to_wire(msg.run_id, m.active_session.run_id);
        m.start_atten_char_count++;
        if (not ctx.send_slac_message(fsm::ev::Context::BROADCAST_MAC, msg)) {
            ctx.log_warn("Failed to send CM_START_ATTEN_CHAR.IND");
        }
        return;
    }

    if (m.mnbc_sound_count < defs::C_EV_MATCH_MNBC) {
        messages::cm_mnbc_sound_ind msg{};
        msg.application_type = defs::COMMON_APPLICATION_TYPE;
        msg.security_type = defs::COMMON_SECURITY_TYPE;
        std::fill(std::begin(msg.sender_id), std::end(msg.sender_id), std::uint8_t{0});
        ++m.mnbc_sound_count;
        msg.remaining_sound_count = defs::C_EV_MATCH_MNBC - m.mnbc_sound_count;
        copy_to_wire(msg.run_id, m.active_session.run_id);
        std::fill(std::begin(msg._reserved), std::end(msg._reserved), std::uint8_t{0});

        for (auto& octet : msg.random) {
            octet = ctx.random_byte();
        }

        if (not ctx.send_slac_message(fsm::ev::Context::BROADCAST_MAC, msg)) {
            ctx.log_warn("Failed to send CM_MNBC_SOUND.IND");
        }
    }
}

bool all_sounding_messages_sent(SlacEvMachine const& m) {
    return m.start_atten_char_count >= defs::C_EV_START_ATTEN_CHAR_INDS && m.mnbc_sound_count >= defs::C_EV_MATCH_MNBC;
}

bool is_slac_parm_cnf(SlacEvMachine const& m, Event const& e) {
    if (not e.is(EventId::message) or not e.payload->is_valid()) {
        return false;
    }
    const auto msg = e.payload->payload_as<messages::cm_slac_parm_cnf>();
    if (not msg.has_value()) {
        return false;
    }
    return e.payload->get_mmtype() == (defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF) and
           wire_pointer_equal(msg->run_id, m.active_session.run_id);
}

bool is_atten_char_ind_after_sounding(SlacEvMachine const& m, Event const& e) {
    if (not e.is(EventId::message) or not e.payload->is_valid()) {
        return false;
    }
    const auto msg = e.payload->payload_as<messages::cm_atten_char_ind>();
    if (e.payload->get_mmtype() != (defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND) or not msg.has_value()) {
        return false;
    }
    return all_sounding_messages_sent(m) && wire_pointer_equal(msg->run_id, m.active_session.run_id);
}

bool is_slac_match_cnf(SlacEvMachine const& m, Event const& e) {
    if (not e.is(EventId::message) or not e.payload->is_valid()) {
        return false;
    }
    auto const msg = e.payload->payload_as<messages::cm_slac_match_cnf>();
    if (not msg.has_value()) {
        return false;
    }
    if (e.payload->get_mmtype() != (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_CNF)) {
        return false;
    }
    if (not wire_pointer_equal(msg->run_id, m.active_session.run_id)) {
        return false;
    }
    auto const source_mac = e.payload->get_src_mac();
    if (source_mac != nullptr && not wire_pointer_equal(source_mac, m.active_session.evse_mac)) {
        return false;
    }
    return true;
}

bool is_set_key_cnf(Event const& e) {
    if (not e.is(EventId::message) or not e.payload->is_valid()) {
        return false;
    }
    return e.payload->get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF) and
           e.payload->payload_as<messages::cm_set_key_cnf>().has_value();
}

struct Matched : EvLevelState<StateId::Matched> {
    explicit Matched(SlacEvMachine& m) : EvLevelState(m, "Matched") {
    }
    void enter() override {
        ctx.signal_state("MATCHED");
        ctx.signal_dlink_ready(true);
        ctx.log_info("EV MSM entered matched");
    }
    Result feed(Event const& e) override {
        return e.is(EventId::reset) ? reset_matching() : unhandled();
    }
};

struct Failed : EvLevelState<StateId::Failed> {
    explicit Failed(SlacEvMachine& m) : EvLevelState(m, "Failed") {
    }
    void enter() override {
        ctx.signal_state("UNMATCHED");
        ctx.signal_dlink_ready(false);
        ctx.log_warn("EV MSM entered failed");
    }
    Result feed(Event const& e) override {
        return e.is(EventId::reset) ? reset_matching() : unhandled();
    }
};

struct WaitSetKeyCnf : EvLevelState<StateId::WaitSetKeyCnf> {
    explicit WaitSetKeyCnf(SlacEvMachine& m) :
        EvLevelState(
            m, "WaitSetKeyCnf",
            configured_or(m.ctx.slac_config.set_key_timeout, std::chrono::milliseconds{defs::TT_MATCH_JOIN_MS})) {
    }
    void enter() override {
        arm_timer();
    }
    Result feed(Event const& e) override {
        switch (e.id) {
        case EventId::update:
            return timed_out() ? transition(make_failed(machine)) : unhandled();
        case EventId::message:
            return is_set_key_cnf(e) ? transition(std::make_unique<Matched>(machine)) : unhandled();
        case EventId::reset:
            return reset_matching();
        default:
            return unhandled();
        }
    }
};

struct WaitMatchCnf : EvLevelState<StateId::WaitMatchCnf> {
    explicit WaitMatchCnf(SlacEvMachine& m) :
        EvLevelState(
            m, "WaitMatchCnf",
            configured_or(m.ctx.slac_config.match_req_timeout, std::chrono::milliseconds{defs::TT_MATCH_RESPONSE_MS})) {
    }
    void enter() override {
        arm_timer();
    }
    void store_match_cnf_and_send_set_key(Event const& e) {
        const auto match_cnf = e.payload->payload_as<messages::cm_slac_match_cnf>();
        if (not match_cnf.has_value()) {
            ctx.log_warn("Received CM_SLAC_MATCH.CNF with invalid payload");
            return;
        }
        std::copy_n(std::begin(match_cnf->nmk), defs::NMK_LEN, machine.pending_nmk.begin());
        if (not ctx.send_slac_message(fsm::ev::Context::EV_PLC_MAC,
                                      fsm::evse::MatchingSessionData::create_cm_set_key_req(machine.pending_nmk))) {
            ctx.log_warn("Failed to send CM_SET_KEY.REQ");
        }
    }
    Result feed(Event const& e) override {
        switch (e.id) {
        case EventId::update:
            if (not timed_out()) {
                return unhandled();
            }
            if (machine.match_req_attempt_count >= ctx.slac_config.match_req_attempts) {
                return transition(make_failed(machine));
            }
            send_slac_match_req(machine);
            return transition(std::make_unique<WaitMatchCnf>(machine));
        case EventId::message:
            if (is_slac_match_cnf(machine, e)) {
                store_match_cnf_and_send_set_key(e);
                return transition(std::make_unique<WaitSetKeyCnf>(machine));
            }
            return unhandled();
        case EventId::reset:
            return reset_matching();
        default:
            return unhandled();
        }
    }
};

// CM_ATTEN_CHAR.IND after sounding: answer it and ask for the match.
template <StateId Id> struct AwaitingAttenChar : EvLevelState<Id> {
    using EvLevelState<Id>::EvLevelState;
    Result send_atten_char_rsp_and_match_req(Event const& e) {
        auto& ctx = this->ctx;
        auto& m = this->machine;
        const auto atten_char_ind = e.payload->payload_as<messages::cm_atten_char_ind>();
        if (not atten_char_ind.has_value()) {
            ctx.log_warn("Received CM_ATTEN_CHAR.IND with invalid payload");
        } else {
            messages::cm_atten_char_rsp rsp{};
            rsp.application_type = defs::COMMON_APPLICATION_TYPE;
            rsp.security_type = defs::COMMON_SECURITY_TYPE;
            copy_to_wire(rsp.source_address, ctx.ev_host_mac);
            copy_to_wire(rsp.run_id, m.active_session.run_id);
            std::fill(std::begin(rsp.source_id), std::end(rsp.source_id), std::uint8_t{0});
            std::fill(std::begin(rsp.resp_id), std::end(rsp.resp_id), std::uint8_t{0});
            rsp.result = defs::CM_ATTEN_CHAR_RSP_RESULT;
            if (not ctx.send_slac_message(m.active_session.evse_mac, rsp)) {
                ctx.log_warn("Failed to send CM_ATTEN_CHAR.RSP");
            }
            send_slac_match_req(m);
        }
        return this->transition(std::make_unique<WaitMatchCnf>(m));
    }
};

struct WaitAttenCharInd : AwaitingAttenChar<StateId::WaitAttenCharInd> {
    explicit WaitAttenCharInd(SlacEvMachine& m) :
        AwaitingAttenChar(m, "WaitAttenCharInd", std::chrono::milliseconds{defs::TT_EV_ATTEN_RESULTS_MS}) {
    }
    void enter() override {
        arm_timer();
    }
    Result feed(Event const& e) override {
        switch (e.id) {
        case EventId::update:
            return timed_out() ? transition(make_failed(machine)) : unhandled();
        case EventId::message:
            return is_atten_char_ind_after_sounding(machine, e) ? send_atten_char_rsp_and_match_req(e) : unhandled();
        case EventId::reset:
            return reset_matching();
        default:
            return unhandled();
        }
    }
};

struct Sounding : AwaitingAttenChar<StateId::Sounding> {
    explicit Sounding(SlacEvMachine& m) :
        AwaitingAttenChar(m, "Sounding", std::chrono::milliseconds{defs::TT_EV_ATTEN_RESULTS_MS}) {
    }
    void enter() override {
        arm_timer();
        batch_start = ctx.current_time;
        next_sound = {};
    }
    // Frame n is due n intervals after the batch started, so wake-up lateness does not add up,
    // but never closer than the minimum spacing to the frame just sent: a late frame is caught up
    // over the following ones instead of leaving a deadline in the past.
    void pace(int frames_sent, timer::tp now) {
        auto const anchored = batch_start + frames_sent * interval;
        next_sound.expire_at(std::max(anchored, now + minimum_gap));
    }
    static constexpr std::chrono::milliseconds interval{defs::TP_EV_BATCH_MSG_INTERVAL_MS};
    static constexpr std::chrono::milliseconds minimum_gap{defs::TP_EV_BATCH_MSG_INTERVAL_MIN_MS};

    void deadlines(earliest_deadline& next) const override {
        AwaitingAttenChar::deadlines(next);
        next.offer(next_sound);
    }
    Result feed(Event const& e) override {
        auto const now = ctx.current_time;
        switch (e.id) {
        case EventId::update:
            if (timed_out()) {
                return transition(make_failed(machine));
            }
            if (all_sounding_messages_sent(machine)) {
                return transition(std::make_unique<WaitAttenCharInd>(machine));
            }
            if (next_sound.expired(now)) {
                send_next_sounding(machine);
                pace(machine.start_atten_char_count + machine.mnbc_sound_count, now);
                return handled();
            }
            return unhandled();
        case EventId::message:
            return is_atten_char_ind_after_sounding(machine, e) ? send_atten_char_rsp_and_match_req(e) : unhandled();
        case EventId::reset:
            return reset_matching();
        default:
            return unhandled();
        }
    }

private:
    timer::tp batch_start{};
    timer next_sound;
};

struct WaitParmCnf : EvLevelState<StateId::WaitParmCnf> {
    explicit WaitParmCnf(SlacEvMachine& m) :
        EvLevelState(
            m, "WaitParmCnf",
            configured_or(m.ctx.slac_config.parm_req_timeout, std::chrono::milliseconds{defs::TT_MATCH_RESPONSE_MS})) {
    }
    void enter() override {
        arm_timer();
    }
    void capture_slac_parm_cnf(Event const& e) {
        const auto msg = e.payload->payload_as<messages::cm_slac_parm_cnf>();
        if (not msg.has_value()) {
            ctx.log_warn("Received CM_SLAC_PARM.CNF with invalid payload");
            return;
        }
        auto const src_mac = e.payload->get_src_mac();
        if (src_mac == nullptr) {
            ctx.log_warn("Received CM_SLAC_PARM.CNF without source MAC");
            return;
        }
        std::copy_n(src_mac, machine.active_session.evse_mac.size(), machine.active_session.evse_mac.begin());
        ctx.signal_state("MATCHING");
    }
    Result feed(Event const& e) override {
        switch (e.id) {
        case EventId::update:
            if (not timed_out()) {
                return unhandled();
            }
            if (machine.parm_req_attempt_count >= ctx.slac_config.parm_req_attempts) {
                return transition(make_failed(machine));
            }
            send_slac_parm_req(machine);
            return transition(std::make_unique<WaitParmCnf>(machine));
        case EventId::message:
            if (is_slac_parm_cnf(machine, e)) {
                capture_slac_parm_cnf(e);
                return transition(std::make_unique<Sounding>(machine));
            }
            return unhandled();
        case EventId::reset:
            return reset_matching();
        default:
            return unhandled();
        }
    }
};

Result start_matching(SlacEvMachine& m) {
    m.parm_req_attempt_count = 0;
    m.match_req_attempt_count = 0;
    m.start_atten_char_count = 0;
    m.mnbc_sound_count = 0;
    m.pending_nmk.fill(0);

    m.active_session = fsm::ev::SessionParameters{};
    for (auto& octet : m.active_session.run_id) {
        octet = m.ctx.random_byte();
    }
    send_slac_parm_req(m);
    m.ctx.log_info("EV MSM start matching");
    return EvState::transition(make_wait_parm_cnf(m));
}

struct Idle : EvLevelState<StateId::Idle> {
    explicit Idle(SlacEvMachine& m) : EvLevelState(m, "Idle") {
    }
    void enter() override {
        ctx.signal_state("UNMATCHED");
        ctx.signal_dlink_ready(false);
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::trigger_matching)) {
            return start_matching(machine);
        }
        if (e.is(EventId::reset)) {
            return transition(make_reset(machine));
        }
        return unhandled();
    }
};

struct Reset : EvLevelState<StateId::Reset> {
    explicit Reset(SlacEvMachine& m) : EvLevelState(m, "Reset") {
    }
    void enter() override {
        ctx.signal_state("UNMATCHED");
        ctx.signal_dlink_ready(false);
        ctx.log_debug("EV MSM entered reset");
    }
    Result feed(Event const& e) override {
        switch (e.id) {
        case EventId::update:
            return transition(std::make_unique<Idle>(machine));
        case EventId::trigger_matching:
            return start_matching(machine);
        case EventId::reset:
            return transition(make_reset(machine));
        default:
            return unhandled();
        }
    }
};

std::unique_ptr<EvState> make_reset(SlacEvMachine& m) {
    return std::make_unique<Reset>(m);
}
std::unique_ptr<EvState> make_failed(SlacEvMachine& m) {
    return std::make_unique<Failed>(m);
}
std::unique_ptr<EvState> make_wait_parm_cnf(SlacEvMachine& m) {
    return std::make_unique<WaitParmCnf>(m);
}

} // namespace

SlacEvMachine::SlacEvMachine(fsm::ev::Context& ctx_) : ctx(ctx_) {
}

void SlacEvMachine::start() {
    top.start(make_reset(*this));
}

void SlacEvMachine::process(Event const& e) {
    top.feed(e);
}

void SlacEvMachine::traverse(Visitor& visitor) const {
    if (top.active()) {
        top.traverse(visitor, 0);
    }
}

} // namespace everest::lib::slac::sm::ev
