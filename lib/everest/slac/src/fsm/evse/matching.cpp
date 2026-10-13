// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// Matching sub-machine: answers CM_SLAC_PARM.REQ, runs one Session per EV (several in parallel for
// cross-talk), the CM_VALIDATE exchange, and exits to Matched or Failed.
//
//   region 0 (Init):   update / a session matched           --> Matched (exit)
//                      update / all failed or init timeout  --> Failed (exit), or once restarts
//                                                               Matching when reset_instead_of_fail
//   region 1 (Listen): message(CM_SLAC_PARM.REQ) / add or restart the EV's session
//                      message(CM_VALIDATE.REQ)  / CM_VALIDATE handling
//                      update / CM_VALIDATE timer service
//   region 2 (Pipe):   every other message and every update go to all sessions
//   outer:             reset --> Reset; leave_bcd --> Idle; Matched exit --> WaitForLink | Matched;
//                      Failed exit --> Failed
//
// Session, one per CM_SLAC_PARM.REQ, from CM_START_ATTEN_CHAR.IND to CM_SLAC_MATCH.CNF:
//   WaitStartAtten --message(CM_START_ATTEN_CHAR.IND)--> Sounding
//   Sounding --message(CM_ATTEN_PROFILE.IND)--> Sounding | FinalizeSounding (all sounds captured)
//   Sounding --update/timeout--> FinalizeSounding
//   FinalizeSounding --update/timeout / send CM_ATTEN_CHAR.IND--> WaitAttenRsp
//   WaitAttenRsp --update/timeout / resend--> WaitAttenRsp (up to C_EV_match_retry), then Failed
//   WaitAttenRsp --message(CM_ATTEN_CHAR.RSP)--> WaitSlacMatch
//   WaitSlacMatch --message(CM_SLAC_MATCH.REQ) / send CM_SLAC_MATCH.CNF--> MatchComplete
//   WaitSlacMatch --update/timeout or post-validation window expired--> Failed

#include "../misc.hpp"
#include "machine.hpp"

#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_types.hpp>
#include <everest/slac/telemetry.hpp>

#include <algorithm>
#include <cstdio>
#include <string>

namespace everest::lib::slac::sm::evse {

// One EV's SLAC run.
class Session {
public:
    Session() = default;
    Session(Session const&) = delete;
    Session& operator=(Session const&) = delete;

    void start();
    void process(Event const& e) {
        region.feed(e);
    }
    bool matched() const {
        return region.current_id() == static_cast<int>(StateId::SessionMatchComplete);
    }
    bool failed() const {
        return region.current_id() == static_cast<int>(StateId::SessionFailed);
    }
    void traverse(Visitor& visitor, std::size_t depth) const {
        region.traverse(visitor, depth);
    }

    struct Tag {};
    using StateBase = State<Tag>;

    fsm::evse::MatchingSessionData session_data;
    fsm::evse::Context* ctx{nullptr};

private:
    Region<StateBase> region;
};

MatchingData::MatchingData() = default;
MatchingData::~MatchingData() = default;

namespace {

// Message checks: the frame has the expected MMTYPE, decodes as MsgT and belongs to this session
// (run id / EV MAC, see MatchingSessionData::validate_message).
template <class MsgT>
bool check_message(Event const& e, std::uint16_t expected, fsm::evse::MatchingSessionData const& session_data) {
    if (not e.is(EventId::message)) {
        return false;
    }
    const auto mmtype = e.payload->get_mmtype();
    if (mmtype not_eq expected) {
        return false;
    }
    auto const msg = e.payload->template payload_as<MsgT>();
    if (not msg.has_value()) {
        return false;
    }
    return session_data.validate_message(*msg);
}
bool matches_start_atten_char(Event const& e, fsm::evse::MatchingSessionData const& session_data) {
    return check_message<messages::cm_start_atten_char_ind>(e, defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND,
                                                            session_data);
}
bool matches_atten_char_rsp(Event const& e, fsm::evse::MatchingSessionData const& session_data) {
    return check_message<messages::cm_atten_char_rsp>(e, defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_RSP,
                                                      session_data);
}
bool matches_slac_match_req(Event const& e, fsm::evse::MatchingSessionData const& session_data) {
    return check_message<messages::cm_slac_match_req>(e, defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ,
                                                      session_data);
}
bool matches_atten_profile_ind(Event const& e, fsm::evse::MatchingSessionData const& session_data) {
    return check_message<messages::cm_atten_profile_ind>(e, defs::MMTYPE_CM_ATTEN_PROFILE | defs::MMTYPE_MODE_IND,
                                                         session_data);
}

using SessionState = Session::StateBase;

template <StateId Id> struct SessionLevelState : SessionState {
    SessionLevelState(Session& session_, char const* name_, std::chrono::milliseconds timeout = {}) :
        session(session_), ctx(*session_.ctx), state_name(name_) {
        state_timer.duration = timeout;
    }
    char const* name() const override {
        return state_name;
    }
    int id() const override {
        return static_cast<int>(Id);
    }
    void enter() override {
        state_timer.arm(ctx.current_time);
    }
    void deadlines(earliest_deadline& next) const override {
        state_timer.deadlines(next);
    }
    bool timed_out() const {
        return state_timer.state_timeout(ctx.current_time);
    }
    void log_failed(char const* reason) {
        ctx.log_info(session_log_prefix(session.session_data) + reason);
    }

protected:
    Session& session;
    fsm::evse::Context& ctx;

private:
    StateTimer state_timer;
    char const* state_name;
};

// A state without a timer of its own; it only flags the session's outcome.
template <StateId Id> struct SessionEndState : SessionState {
    explicit SessionEndState(char const* name_) : state_name(name_) {
    }
    char const* name() const override {
        return state_name;
    }
    int id() const override {
        return static_cast<int>(Id);
    }

private:
    char const* state_name;
};
using MatchComplete = SessionEndState<StateId::SessionMatchComplete>;
using SessionFailed = SessionEndState<StateId::SessionFailed>;

std::unique_ptr<SessionState> make_session_failed() {
    return std::make_unique<SessionFailed>("Failed");
}

void send_atten_char(Session& session) {
    auto& ctx = *session.ctx;
    auto& data = session.session_data;
    auto atten_char = data.create_cm_atten_char_ind(ctx.slac_config.sounding_atten_adjustment);
    if (not ctx.send_slac_message(data.ev_mac, atten_char)) {
        ctx.log_warn("Failed to send CM_ATTEN_CHAR.IND");
    }
    int aag_overall_sum = 0;
    for (size_t i = 0; i < defs::AAG_LIST_LEN; ++i) {
        aag_overall_sum += atten_char.attenuation_profile.aag[i];
    }
    ctx.status.average_attenuation = aag_overall_sum / defs::AAG_LIST_LEN;
    char average[24];
    std::snprintf(average, sizeof(average), "%.1f", static_cast<double>(aag_overall_sum) / defs::AAG_LIST_LEN);
    std::string text = std::string("Avg atten.: ") + average + " dB";
    if (ctx.slac_config.sounding_atten_adjustment != 0) {
        text += " plus offset " + std::to_string(ctx.slac_config.sounding_atten_adjustment) + " dB";
    }
    text +=
        ", from " + std::to_string(defs::AAG_LIST_LEN) + " groups, " + std::to_string(data.captured_sounds) + " sounds";
    ctx.log_info(session_log_prefix(data) + text);
}

struct WaitSlacMatch : SessionLevelState<StateId::SessionWaitSlacMatch> {
    explicit WaitSlacMatch(Session& s) :
        SessionLevelState(s, "WaitSlacMatch", std::chrono::milliseconds(defs::TT_EVSE_MATCH_SESSION_MS)) {
    }
    // After a CM_VALIDATE process, the CM_SLAC_MATCH.REQ must arrive within TT_match_sequence (much
    // shorter than the overall TT_EVSE_match_session that bounds WaitSlacMatch otherwise). When that
    // shorter window elapses the matching has FAILED (ISO 15118-5 CmSlacMatch_003/004 cmValidate
    // variant); a late CM_SLAC_MATCH.REQ must then get no CM_SLAC_MATCH.CNF.
    // Only the validated EV's session is held to the post-validation window; a parallel session of
    // another EV keeps the full match session timeout.
    bool validation_window_expired() const {
        return ctx.validation_done and session.session_data.ev_mac == ctx.validation_ev_mac and
               ctx.validation_match_window.expired(ctx.current_time);
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update)) {
            if (validation_window_expired()) {
                log_failed("No CM_SLAC_MATCH.REQ within TT_match_sequence after CM_VALIDATE, session failed");
                return transition(make_session_failed());
            }
            if (timed_out()) {
                log_failed("Timeout waiting for CM_SLAC_MATCH.REQ, session failed");
                return transition(make_session_failed());
            }
            return unhandled();
        }
        if (matches_slac_match_req(e, session.session_data)) {
            send_match_cnf(e);
            return transition(std::make_unique<MatchComplete>("MatchComplete"));
        }
        return unhandled();
    }
    void send_match_cnf(Event const& e) {
        auto& data = session.session_data;
        messages::cm_slac_match_cnf& reply = ctx.match_confirm_cache.message;
        auto const msg = e.payload->payload_as<messages::cm_slac_match_req>();
        if (not msg.has_value()) {
            return;
        }
        ctx.log_info(session_log_prefix(data) +
                     "Received CM_SLAC_MATCH.REQ, sending CM_SLAC_MATCH.CNF -> session complete");
        static constexpr Nmk failed_match_session_nmk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                                      0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};

        Nmk const* session_nmk = &ctx.slac_config.session_nmk;
        if (ctx.slac_config.link_status.debug_simulate_failed_matching) {
            ctx.log_info("Sending wrong NMK to EV to simulate a failed link setup after match request");
            session_nmk = &failed_match_session_nmk;
        }

        data.create_cm_slac_match_cnf(reply, *msg, *session_nmk);
        if (not ctx.send_slac_message(data.ev_mac, reply)) {
            ctx.log_warn("Failed to send CM_SLAC_MATCH.CNF");
        }
        ctx.signal_cm_slac_match_cnf(data.ev_mac.data());
        ctx.cache_match_confirm_message(reply, data.ev_mac, data.evse_mac, data.run_id);
        std::copy(std::begin(data.ev_mac), std::end(data.ev_mac), std::begin(ctx.status.ev_mac));
    }
};

struct WaitAttenRsp : SessionLevelState<StateId::SessionWaitAttenRsp> {
    explicit WaitAttenRsp(Session& s) :
        SessionLevelState(s, "WaitAttenRsp", std::chrono::milliseconds(defs::TT_MATCH_RESPONSE_MS)) {
    }
    Result feed(Event const& e) override {
        auto& data = session.session_data;
        if (e.is(EventId::update)) {
            if (not timed_out()) {
                return unhandled();
            }
            if (data.num_retries >= defs::C_EV_MATCH_RETRY) {
                log_failed("No CM_ATTEN_CHAR.RSP after all retries, session failed");
                return transition(make_session_failed());
            }
            ctx.log_info(session_log_prefix(data) +
                         "No CM_ATTEN_CHAR.RSP yet, retransmitting CM_ATTEN_CHAR.IND (retry " +
                         std::to_string(data.num_retries + 1) + " of " + std::to_string(defs::C_EV_MATCH_RETRY) + ")");
            send_atten_char(session);
            data.num_retries++;
            return transition(std::make_unique<WaitAttenRsp>(session));
        }
        if (matches_atten_char_rsp(e, data)) {
            ctx.log_info(session_log_prefix(data) + "Received CM_ATTEN_CHAR.RSP, waiting for CM_SLAC_MATCH.REQ");
            return transition(std::make_unique<WaitSlacMatch>(session));
        }
        return unhandled();
    }
};

struct FinalizeSounding : SessionLevelState<StateId::SessionFinalizeSounding> {
    static constexpr auto FINALIZE_SOUNDING_DELAY_MS = 45;
    explicit FinalizeSounding(Session& s) :
        SessionLevelState(s, "FinalizeSounding", std::chrono::milliseconds(FINALIZE_SOUNDING_DELAY_MS)) {
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update) and timed_out()) {
            ctx.log_info(session_log_prefix(session.session_data) + "Finalize sounding, sending CM_ATTEN_CHAR.IND");
            send_atten_char(session);
            return transition(std::make_unique<WaitAttenRsp>(session));
        }
        return unhandled();
    }
};

struct Sounding : SessionLevelState<StateId::SessionSounding> {
    explicit Sounding(Session& s) :
        SessionLevelState(s, "Sounding", std::chrono::milliseconds(defs::TT_EVSE_MATCH_MNBC_MS)) {
    }
    // Accumulate one CM_ATTEN_PROFILE.IND.
    void capture_sound(Event const& e) {
        auto& data = session.session_data;
        auto const msg = e.payload->payload_as<messages::cm_atten_profile_ind>();
        if (not msg.has_value()) {
            return;
        }
        for (int i = 0; i < defs::AAG_LIST_LEN; ++i) {
            data.captured_aags[i] += msg->aag[i];
        }
        data.captured_sounds++;
        ctx.log_debug(session_log_prefix(data) + "Received CM_ATTEN_PROFILE.IND (" +
                      std::to_string(data.captured_sounds) + " of " +
                      std::to_string(defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) + " sounds captured)");
        if (data.captured_sounds >= defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) {
            ctx.log_info(session_log_prefix(data) + "Received all sounds, finalizing sounding");
        }
    }
    Result feed(Event const& e) override {
        auto& data = session.session_data;
        if (e.is(EventId::update)) {
            if (timed_out()) {
                return transition(std::make_unique<FinalizeSounding>(session));
            }
            return unhandled();
        }
        if (not matches_atten_profile_ind(e, data)) {
            return unhandled();
        }
        if (data.captured_sounds + 1 >= defs::CM_SLAC_PARM_CNF_NUM_SOUNDS) {
            capture_sound(e);
            return transition(std::make_unique<FinalizeSounding>(session));
        }
        capture_sound(e);
        return handled();
    }
};

struct WaitStartAtten : SessionLevelState<StateId::SessionWaitStartAtten> {
    explicit WaitStartAtten(Session& s) :
        SessionLevelState(s, "WaitStartAtten", std::chrono::milliseconds(defs::TT_MATCH_SEQUENCE_MS)) {
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update)) {
            if (timed_out()) {
                log_failed("Timeout waiting for CM_START_ATTEN_CHAR.IND, session failed");
                return transition(make_session_failed());
            }
            return unhandled();
        }
        if (not timed_out() and matches_start_atten_char(e, session.session_data)) {
            ctx.log_info(session_log_prefix(session.session_data) +
                         "Received CM_START_ATTEN_CHAR.IND, MNBC sounding started");
            return transition(std::make_unique<Sounding>(session));
        }
        return unhandled();
    }
};

} // namespace

void Session::start() {
    session_data.num_retries = 0;
    region.start(std::make_unique<WaitStartAtten>(*this));
}

namespace {

bool any_session_matched(MatchingData const& data) {
    return std::any_of(data.sessions.begin(), data.sessions.end(), [](auto const& s) { return s->matched(); });
}
// False while there is no session at all: "all failed" needs at least one session.
bool all_sessions_failed(MatchingData const& data) {
    if (data.sessions.empty()) {
        return false;
    }
    return std::all_of(data.sessions.begin(), data.sessions.end(), [](auto const& s) { return s->failed(); });
}

int max_matching_sessions(fsm::evse::Context const& ctx) {
    return std::max(1, ctx.slac_config.max_matching_sessions);
}

bool is_validate_req(Event const& e) {
    return is_message_of_type(e, defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_REQ);
}

// Region 0: decides when Matching is over.
struct MatchingInit : InnerLevelState<StateId::MatchingInit> {
    explicit MatchingInit(SlacMachine& m) : InnerLevelState(m, "Init") {
    }
    bool reacts_to(EventId e) const override {
        return e == EventId::update;
    }
    Result feed(Event const& e) override {
        if (not e.is(EventId::update)) {
            return unhandled();
        }
        auto& data = machine.matching_data;
        auto const should_timeout = data.to.expired(ctx.current_time);
        auto const is_failed = all_sessions_failed(data);
        auto const no_sessions = data.sessions.empty();
        auto const finished_without_match = is_failed or (no_sessions and should_timeout);
        auto const reset_instead_of_fail = ctx.slac_config.reset_instead_of_fail;

        if (finished_without_match and reset_instead_of_fail and not data.failed_matching_reset_once) {
            ctx.status.session_count = 0;
            data.sessions.clear();
            data.to.arm(ctx.current_time, ctx.slac_config.slac_init_timeout);
            data.failed_matching_reset_once = true;
            // A validation belongs to the sessions just dropped. Left armed, its expired window would
            // fail every new session the moment it reaches WaitSlacMatch.
            data.validate.reset();
            ctx.validation_done = false;
            return transition(std::make_unique<MatchingInit>(machine));
        }
        if (finished_without_match) {
            return transition(
                std::make_unique<ExitPoint<StateId::MatchingExitFailed>>(machine, "Failed", EventId::update, e));
        }
        // A session in MatchComplete (CM_SLAC_MATCH.CNF is out) ends Matching within the event that
        // delivered the CM_SLAC_MATCH.REQ, which is what keeps a later CM_SLAC_PARM.REQ unanswered
        // (TC_SECC_CMN_VTB_PLCLinkStatus_003).
        if (any_session_matched(data)) {
            return transition(
                std::make_unique<ExitPoint<StateId::MatchingExitMatched>>(machine, "Matched", EventId::update, e));
        }
        return unhandled();
    }
};

// Region 1: new sessions and the CM_VALIDATE exchange.
struct Listen : InnerLevelState<StateId::MatchingListen> {
    explicit Listen(SlacMachine& m) : InnerLevelState(m, "Listen") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    Result feed(Event const& e) override {
        auto& data = machine.matching_data;
        if (e.is(EventId::update)) {
            if (data.validate.needs_service(ctx.current_time)) {
                data.validate.tick(ctx);
                return transition(std::make_unique<Listen>(machine));
            }
            return unhandled();
        }
        // CM_VALIDATE (ISO 15118-3 9.4) BCB-toggle validation lives in fsm::evse::ValidateHandler;
        // Matching only routes the REQ frames and the update tick to it.
        if (is_validate_req(e)) {
            const auto req = e.payload->payload_as<messages::cm_validate_req>();
            if (req.has_value()) {
                data.validate.handle_req(*req, e.payload->get_src_mac(), ctx);
            }
            return transition(std::make_unique<Listen>(machine));
        }
        if (is_message_of_type(e, defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ)) {
            add_session(*e.payload);
            return transition(std::make_unique<Listen>(machine));
        }
        return unhandled();
    }

    void add_session(messages::HomeplugMessage const& payload) {
        auto& data = machine.matching_data;
        auto const msg = payload.payload_as<messages::cm_slac_parm_req>();
        if (not msg.has_value()) {
            return;
        }
        if (not fsm::evse::MatchingSessionData::validate_message(*msg)) {
            return;
        }
        auto const ev_mac = byte_array_from_wire<MacAddress>(payload.get_src_mac());
        auto const run_id = byte_array_from_wire<RunId>(msg->run_id);
        fsm::evse::MatchingSessionData session_data(ev_mac, run_id, ctx.evse_mac);
        auto session_iter = std::find_if(data.sessions.begin(), data.sessions.end(), [&session_data](auto const& s) {
            return s->session_data.matches_identity(session_data.ev_mac, session_data.run_id);
        });
        if (session_iter == data.sessions.end()) {
            auto const max_sessions = max_matching_sessions(ctx);
            if (static_cast<int>(data.sessions.size()) >= max_sessions) {
                ctx.log_warn("Ignoring CM_SLAC_PARM.REQ because max_matching_sessions was reached (" +
                             std::to_string(max_sessions) + ")");
                return;
            }
            session_iter = data.sessions.emplace(data.sessions.end(), std::make_unique<Session>());
        }
        auto& session = **session_iter;
        session.session_data = session_data;
        session.ctx = &ctx;
        // A new CM_SLAC_PARM.REQ starts this EV's SLAC run over; a validation from its previous
        // run, in progress or completed, does not carry into it.
        if (data.validate.owner_mac() == session_data.ev_mac) {
            data.validate.reset();
        }
        if (ctx.validation_done and ctx.validation_ev_mac == session_data.ev_mac) {
            ctx.validation_done = false;
        }
        session.start();
        // send reply
        ctx.log_info(session_log_prefix(session_data) + "Received CM_SLAC_PARM.REQ, sending CM_SLAC_PARM.CNF");
        auto param_confirm = session_data.create_cm_slac_parm_cnf();
        if (not ctx.send_slac_message(session_data.ev_mac, param_confirm)) {
            ctx.log_warn("Failed to send CM_SLAC_PARM.CNF");
        }
        ctx.signal_cm_slac_parm_req(session_data.ev_mac.data());
        ctx.status.session_count = data.sessions.size();
    }
};

// Region 2: hands everything else to the sessions.
struct Pipe : InnerLevelState<StateId::MatchingPipe> {
    explicit Pipe(SlacMachine& m) : InnerLevelState(m, "Pipe") {
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::update, EventId::message});
    }
    Result feed(Event const& e) override {
        if (e.is(EventId::update) or (e.is(EventId::message) and not is_validate_req(e))) {
            for (auto& session : machine.matching_data.sessions) {
                session->process(e);
            }
            return handled();
        }
        return unhandled();
    }
};

struct Matching : TopLevelState<StateId::Matching> {
    explicit Matching(SlacMachine& m) : TopLevelState(m, "Matching_def") {
    }
    void enter() override {
        auto& data = machine.matching_data;
        data.to.arm(ctx.current_time, ctx.slac_config.slac_init_timeout);
        data.failed_matching_reset_once = false;
        data.validate.reset();
        ctx.validation_done = false;
        ctx.enter_state(SlacState::Matching, D3State::Matching, "Entered Matching state, waiting for CM_SLAC_PARM.REQ");
        supervise.start(std::make_unique<MatchingInit>(machine));
        listen.start(std::make_unique<Listen>(machine));
        pipe.start(std::make_unique<Pipe>(machine));
    }
    void leave() override {
        supervise.stop();
        listen.stop();
        pipe.stop();
        if (machine.shutting_down()) {
            return;
        }
        machine.matching_data.sessions.clear();
        ctx.status.session_count = 0;
    }
    Result feed(Event const& e) override {
        auto handled_inside = supervise.feed(e);
        handled_inside = listen.feed(e) or handled_inside;
        handled_inside = pipe.feed(e) or handled_inside;
        if (handled_inside) {
            return handled();
        }
        if (e.is(EventId::reset)) {
            return transition(make_reset(machine));
        }
        if (e.is(EventId::leave_bcd)) {
            return transition(make_idle(machine));
        }
        if (e.is(EventId::completion)) {
            if (supervise.current_id() == static_cast<int>(StateId::MatchingExitMatched)) {
                if (ctx.slac_config.link_status.do_detect) {
                    return transition(make_wait_for_link(machine));
                }
                return transition(make_matched(machine));
            }
            if (supervise.current_id() == static_cast<int>(StateId::MatchingExitFailed)) {
                return transition(make_failed(machine));
            }
        }
        return unhandled();
    }
    void deadlines(earliest_deadline& next) const override {
        auto const& data = machine.matching_data;
        next.offer(data.to);
        data.validate.deadlines(next);
        if (ctx.validation_done) {
            next.offer(ctx.validation_match_window);
        }
    }
    bool reacts_to(EventId e) const override {
        return reacts_to_any(e, {EventId::reset, EventId::leave_bcd}) or supervise.current().reacts_to(e) or
               listen.current().reacts_to(e) or pipe.current().reacts_to(e);
    }
    bool is_submachine() const override {
        return true;
    }
    void traverse_children(Visitor& visitor, std::size_t depth) const override {
        supervise.traverse(visitor, depth);
        listen.traverse(visitor, depth);
        pipe.traverse(visitor, depth);
        auto const& sessions = machine.matching_data.sessions;
        visitor.on_dynamic_sessions_size(sessions.size());
        int idx = 0;
        for (auto const& session : sessions) {
            visitor.on_dynamic_session_start(idx, depth);
            session->traverse(visitor, depth + 1);
            visitor.on_dynamic_session_end(idx++, depth);
        }
    }

private:
    InnerRegion supervise;
    InnerRegion listen;
    InnerRegion pipe;
};

} // namespace

TopStatePtr make_matching(SlacMachine& machine) {
    return std::make_unique<Matching>(machine);
}

} // namespace everest::lib::slac::sm::evse
