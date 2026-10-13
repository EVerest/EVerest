// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Reset sub-machine: (re)generates the NMK and programs it into the modem with CM_SET_KEY.REQ,
// then exits to ResetChip or Idle.
//
//   Init --completion / send CM_SET_KEY.REQ--> MsgSent
//   MsgSent --update/timeout, attempts left / resend--> MsgSent          (retry_confirmed only)
//   MsgSent --update/timeout, no attempts left--> MsgValid               (retry_confirmed only)
//   MsgSent --message(CM_SET_KEY.CNF ok)--> MsgValid
//   MsgSent --message(CM_SET_KEY.CNF failed), attempts left--> MsgSent   (retry_confirmed only)
//   MsgSent --message(CM_SET_KEY.CNF failed), no attempts left--> MsgValid (retry_confirmed only)
//   MsgValid --update--> ResetChip (exit) | Idle (exit)
//   outer:  reset --> Reset;  ResetChip exit --> ResetChip;  Idle exit --> Idle;
//           update/set-key timeout in legacy_single_attempt mode --> Failed

#include "../misc.hpp"
#include "machine.hpp"

#include <everest/slac/EvseSlacConfig.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/telemetry.hpp>

#include <string>

namespace everest::lib::slac::sm::evse {

namespace {

using fsm::evse::SetKeyHandlingMode;

bool is_retry_confirmed(fsm::evse::Context& ctx) {
    return ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::retry_confirmed;
}
bool is_legacy_set_key_handling_mode(fsm::evse::Context& ctx) {
    return ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::legacy_single_attempt;
}
bool has_set_key_attempts_left(SlacMachine& m) {
    return m.reset_data.set_key_attempts < m.ctx.slac_config.set_key_max_attempts;
}
bool set_key_timeout(SlacMachine& m) {
    return m.reset_data.set_key_timer.expired(m.ctx.current_time);
}
bool is_set_key_cnf(Event const& e) {
    return is_message_of_type(e, defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF) and
           e.payload->payload_as<messages::cm_set_key_cnf>().has_value();
}
bool is_set_key_cnf_success(SlacMachine& m, Event const& e) {
    if (not is_set_key_cnf(e)) {
        return false;
    }
    auto const msg = e.payload->payload_as<messages::cm_set_key_cnf>();
    return accepts_set_key_cnf_success_result(m.ctx.slac_config.set_key_cnf_success_mode, msg->result);
}
bool is_set_key_cnf_failed(SlacMachine& m, Event const& e) {
    if (not is_set_key_cnf(e)) {
        return false;
    }
    auto const msg = e.payload->payload_as<messages::cm_set_key_cnf>();
    return not accepts_set_key_cnf_success_result(m.ctx.slac_config.set_key_cnf_success_mode, msg->result);
}

// Decide which NMK the upcoming CM_SET_KEY.REQ carries. With regenerate_key_on_reset a fresh key is
// generated: into pending_nmk in retry_confirmed mode (it becomes the session key only once the
// modem confirmed it, see apply_set_key_cnf) or straight into the session key otherwise. Without
// regeneration the current session key is re-sent. legacy_single_attempt always sends the session
// key. Also arms the first attempt.
void select_nmk_for_reset(SlacMachine& m) {
    auto& config = m.ctx.slac_config;
    auto& data = m.reset_data;
    if (config.regenerate_key_on_reset) {
        if (config.set_key_handling_mode == SetKeyHandlingMode::retry_confirmed) {
            config.generate_nmk(data.pending_nmk);
        } else {
            config.generate_nmk(config.session_nmk);
        }
    } else {
        data.pending_nmk = config.session_nmk;
    }
    if (config.set_key_handling_mode == SetKeyHandlingMode::legacy_single_attempt) {
        data.pending_nmk = config.session_nmk;
    }
    data.set_key_attempts = 1;
}

void send_set_key_req(SlacMachine& m) {
    auto& ctx = m.ctx;
    Nmk const& nmk = is_retry_confirmed(ctx) ? m.reset_data.pending_nmk : ctx.slac_config.session_nmk;
    ctx.log_info("Using SLAC session NMK " + format_session_nmk_for_log(nmk));
    auto msg = fsm::evse::MatchingSessionData::create_cm_set_key_req(nmk);
    m.reset_data.set_key_timer.arm(ctx.current_time, ctx.slac_config.set_key_timeout);
    if (not ctx.send_slac_message(ctx.slac_config.plc_peer_mac, msg)) {
        ctx.log_warn("Failed to send CM_SET_KEY.REQ");
    }
}

struct MsgValid : InnerLevelState<StateId::ResetMsgValid> {
    explicit MsgValid(SlacMachine& m) : InnerLevelState(m, "MsgValid") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::update;
    }
    Result feed(Event const& e) override {
        if (not e.is(EventId::update)) {
            return unhandled();
        }
        if (not ctx.slac_config.chip_reset.enabled) {
            return transition(std::make_unique<ExitPoint<StateId::ResetExitIdle>>(machine, "Idle", EventId::update, e));
        }
        return transition(
            std::make_unique<ExitPoint<StateId::ResetExitResetChip>>(machine, "ResetChip", EventId::update, e));
    }
};

struct MsgSent : InnerLevelState<StateId::ResetMsgSent> {
    explicit MsgSent(SlacMachine& m) : InnerLevelState(m, "MsgSent") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    Result feed(Event const& e) override {
        auto& data = machine.reset_data;
        auto const& config = ctx.slac_config;
        if (e.is(EventId::update)) {
            if (not(set_key_timeout(machine) and is_retry_confirmed(ctx))) {
                return unhandled();
            }
            if (not has_set_key_attempts_left(machine)) {
                ctx.log_error("CM_SET_KEY timeout without valid CM_SET_KEY.CNF after " +
                              std::to_string(config.set_key_max_attempts) + " attempts; continuing to reset/idle path");
                return transition(std::make_unique<MsgValid>(machine));
            }
            data.set_key_attempts++;
            ctx.log_warn("Retrying CM_SET_KEY.REQ due to timeout. Attempt " + std::to_string(data.set_key_attempts) +
                         " of " + std::to_string(config.set_key_max_attempts));
            send_set_key_req(machine);
            return transition(std::make_unique<MsgSent>(machine));
        }
        if (e.is(EventId::message)) {
            if (is_set_key_cnf_failed(machine, e) and is_retry_confirmed(ctx)) {
                auto const reply = e.payload->payload_as<messages::cm_set_key_cnf>();
                if (not has_set_key_attempts_left(machine)) {
                    ctx.log_error("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) +
                                  " after maximum attempts; continuing to reset/idle path");
                    return transition(std::make_unique<MsgValid>(machine));
                }
                ctx.log_warn("CM_SET_KEY.CNF indicates failure with result=" + std::to_string(reply->result) +
                             " on attempt " + std::to_string(data.set_key_attempts) + "; retrying after timeout (max " +
                             std::to_string(config.set_key_max_attempts) + ")");
                return transition(std::make_unique<MsgSent>(machine));
            }
            if (is_set_key_cnf_success(machine, e)) {
                ctx.slac_config.session_nmk = data.pending_nmk;
                ctx.log_info("CM_SET_KEY.CNF success, NMK set on modem");
                ctx.status.modem_NMK = true;
                return transition(std::make_unique<MsgValid>(machine));
            }
        }
        return unhandled();
    }
};

struct ResetInit : InnerLevelState<StateId::ResetInit> {
    explicit ResetInit(SlacMachine& m) : InnerLevelState(m, "Init") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::completion;
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::completion)) {
            send_set_key_req(machine);
            return transition(std::make_unique<MsgSent>(machine));
        }
        return unhandled();
    }
};

struct Reset : TopLevelState<StateId::Reset> {
    explicit Reset(SlacMachine& m) : TopLevelState(m, "Reset_def") {
    }
    void enter() override {
        select_nmk_for_reset(machine);
        ctx.enter_state(SlacState::Reset, D3State::Unmatched, "Entered Reset state");
        ctx.clear_match_confirm_cache();
        ctx.status.modem_NMK = false;
        region.start(std::make_unique<ResetInit>(machine));
        region.complete();
    }
    void leave() override {
        region.stop();
    }
    Result feed(Event const& e) override {
        if (region.feed(e)) {
            region.complete();
            return handled();
        }
        if (e.is(EventId::reset)) {
            return transition(make_reset(machine));
        }
        if (e.is(EventId::update)) {
            if (region.current_id() == static_cast<int>(StateId::ResetExitIdle)) {
                return transition(make_idle(machine));
            }
            if (region.current_id() == static_cast<int>(StateId::ResetExitResetChip)) {
                return transition(make_reset_chip(machine));
            }
            if (set_key_timeout(machine) and is_legacy_set_key_handling_mode(ctx)) {
                return transition(make_failed(machine));
            }
        }
        return unhandled();
    }
    void deadlines(earliest_deadline& next) const override {
        next.offer(machine.reset_data.set_key_timer);
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::reset, EventId::update}) or region.current().reacts_to(e);
    }
    bool is_submachine() const override {
        return true;
    }
    void traverse_children(Visitor& visitor, std::size_t depth) const override {
        region.traverse(visitor, depth);
    }

private:
    InnerRegion region;
};

} // namespace

TopStatePtr make_reset(SlacMachine& machine) {
    return std::make_unique<Reset>(machine);
}

} // namespace everest::lib::slac::sm::evse
