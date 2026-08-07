// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <any>
#include <memory>
#include <optional>
#include <tuple>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_din/common_types.hpp>
#include <iso15118/message_din/payload_type.hpp>
#include <iso15118/message_din/type.hpp>
#include <iso15118/message_din/variant.hpp>
#include <iso15118/session/feedback.hpp>

#include "config.hpp"

namespace iso15118::din {

// DIN SPEC 70121 counterpart of d20::MessageExchange. Serializes an outgoing request into the shared
// output buffer and holds the last decoded incoming message.
class MessageExchange {
public:
    explicit MessageExchange(io::StreamOutputView);

    void set_request(std::unique_ptr<message_din::Variant> new_request);
    std::unique_ptr<message_din::Variant> pull_request();
    message_din::Type peek_request_type() const;

    template <typename MessageType> void set_response(const MessageType& msg) {
        response_size = message_din::serialize(msg, response);
        response_available = true;
        payload_type = message_din::PayloadTypeTrait<MessageType>::type;
        response_type = message_din::TypeTrait<MessageType>::type;
        response_message = msg;
    }

    template <typename Msg> std::optional<Msg> get_response() {
        static_assert(message_din::TypeTrait<Msg>::type != message_din::Type::None, "Unhandled type!");
        if (message_din::TypeTrait<Msg>::type != response_type) {
            return std::nullopt;
        }
        try {
            return std::any_cast<Msg>(response_message);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    std::tuple<bool, size_t, io::v2gtp::PayloadType, message_din::Type> check_and_clear_response();
    bool has_response() const {
        return response_available;
    }

    // True while an incoming message is decoded and not yet pulled. The SECC engine uses this to re-feed a
    // state that deferred a branch message to the state it transitioned to (peek without pull).
    bool has_request() const {
        return request != nullptr;
    }

private:
    // input
    std::unique_ptr<message_din::Variant> request{nullptr};

    // output
    const io::StreamOutputView response;
    size_t response_size{0};
    bool response_available{false};
    io::v2gtp::PayloadType payload_type{io::v2gtp::PayloadType::SAP};
    message_din::Type response_type{message_din::Type::None};
    std::any response_message;
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// SECC-side DIN SPEC 70121 context, the counterpart of din::ev::Context. Holds the offered session
// config, the feedback the SECC emits, the assigned session id and the control-event-driven runtime
// quantities (authorization, isolation, present voltage/current) the response states read.
class Context {
public:
    Context(session::feedback::Callbacks, SessionConfig, const std::optional<d20::ControlEvent>&, MessageExchange&,
            d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    // --- receive path (EV -> SECC): the incoming decoded request lives in the request slot ---
    std::unique_ptr<message_din::Variant> pull_request();
    message_din::Type peek_request_type() const;

    // --- send path (SECC -> EV): the outgoing response goes into the response slot ---
    template <typename ResponseType> void respond(const ResponseType& msg) {
        // Two situations end the session by failing every response from here on, whatever state built
        // it, and terminating with it. The state saw its own OK response, so session_stopped is set here
        // as well.
        //   - The EV kept the session going beyond the grace period after an EVSE-initiated stop
        //     (charger_stop_ignored, armed by the engine's STOP_CHARGING guard, EvseV2G stop_hlc parity).
        //   - The module reported an emergency shutdown (send_error). DIN has the SECC send the FAILED
        //     response and only then stop the session ([V2G-DC-866] uses that shape for its own abort),
        //     rather than dropping the TCP connection and leaving the EV without a reason. The physical
        //     shutdown itself runs over the control pilot, not over V2G ([V2G-DC-638] NOTE 2), so this
        //     response reports the emergency, it never enforces it.
        if ((charger_stop_ignored or emergency_shutdown) and
            msg.response_code < message_din::datatypes::ResponseCode::FAILED) {
            auto failed = msg;
            failed.response_code = message_din::datatypes::ResponseCode::FAILED;
            session_stopped = true;
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
            message_exchange.set_response(failed);
            return;
        }
        // Every FAILED_* response terminates a DIN session (all states set session_stopped on
        // FAILED). Arm the marker here centrally so Session::send_response() reports
        // FailedTermination once the response hit the wire: oscillator off without delay
        // [V2G-DC-942] and SECC-side TCP close [V2G-DC-940].
        if (msg.response_code >= message_din::datatypes::ResponseCode::FAILED) {
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
        }
        message_exchange.set_response(msg);
    }

    template <typename T> T const* get_control_event() {
        if (not current_control_event.has_value()) {
            return nullptr;
        }
        if (not std::holds_alternative<T>(*current_control_event)) {
            return nullptr;
        }
        return &std::get<T>(*current_control_event);
    }

    // Fills a response header with the assigned session id.
    void setup_header(message_din::Header& header) const;

    // Reports the EV's DC_EVStatus (ready flag, error code, RESS state of charge) to the module. Every DC
    // request of the DIN sequence carries it, so this forwards on change only (EvseV2G
    // publish_DIN_DcEvStatus parity); without it the EV state of charge never reaches EvseManager/OCPP.
    void report_ev_status(const message_din::datatypes::DcEvStatus& status);

    // Hands the EV's charge progress (remaining times, completion flags) to the module, on change only.
    // The EV repeats the values in every CurrentDemandReq, and PowerDeliveryReq carries the completion
    // flags without the remaining times -- those stay absent there rather than being reported as zero.
    void report_charge_progress(const session::feedback::DcEvChargeProgress& progress);

    void set_session_id(const message_din::datatypes::SessionId& id) {
        session_id = id;
    }

    const message_din::datatypes::SessionId& get_session_id() const {
        return session_id;
    }

    void start_timeout(d20::TimeoutType type, uint32_t time_ms) {
        timeouts.start_timeout(type, time_ms);
    }

    void stop_timeout(d20::TimeoutType type) {
        timeouts.stop_timeout(type);
    }

    // Arms V2G_SECC_CPState_Detection_Timeout for a request parked waiting on the CP state, always as a
    // fresh window. Timeouts::start_timeout() refuses an already-occupied slot, so a state that hands
    // over while it still has a request parked would otherwise leave the next state silently inheriting
    // the leftover -- possibly already expired -- deadline instead of getting its own.
    void arm_cp_state_timeout(uint32_t time_ms) {
        timeouts.reset_timeout(d20::TimeoutType::CPSTATE);
        timeouts.start_timeout(d20::TimeoutType::CPSTATE, time_ms);
    }

    // Drops the CP-state timeout of a parked request (silent if none is armed). Used when a state hands
    // over while parked, so no stale CPSTATE timeout can fire in the state that takes over.
    void clear_cp_state_timeout() {
        timeouts.reset_timeout(d20::TimeoutType::CPSTATE);
    }

    d20::TimeoutType const* get_active_timeout() {
        if (not current_timeout.has_value()) {
            return nullptr;
        }
        return &current_timeout.value();
    }

    void set_active_timeout(d20::TimeoutType timeout) {
        current_timeout = timeout;
    }

    // Graceful HLC shutdown (Session::request_shutdown): read by the CurrentDemand charge loop, which
    // answers with EVSEStatusCode = EVSE_Shutdown so the EV stops charging and ends the session.
    void request_shutdown() {
        requested_shutdown = true;
    }

    [[nodiscard]] bool shutdown_requested() const {
        return requested_shutdown;
    }

    const session::Feedback feedback;

    SessionConfig session_config;

    // Runtime quantities driven by control events during the session.
    bool authorized{false};
    bool cable_check_done{false};
    // Set when the module reports a finished-but-failed cable check (isolation fault) via
    // CableCheckFinished{success=false} [V2G-DC-890]; distinct from "not finished yet".
    bool cable_check_fault{false};
    // EVSE-initiated stop (module stop_charging / driver shutdown), latched by the engine in ANY state:
    // every subsequent status-carrying response tells the EV to stop via EVSEStatusCode = EVSE_Shutdown
    // (the EVSENotification stays None for DC [V2G-DC-500]). Mirrors EvseV2G, which stamps its context
    // status code into every response after handle_stop_charging.
    bool charger_stop_requested{false};
    // The EV ignored the stop request beyond the STOP_CHARGING guard: respond() fails every further
    // response and ends the session (EvseV2G stop_hlc parity). Set by the engine on the guard timeout.
    bool charger_stop_ignored{false};
    // The module reported an emergency shutdown (send_error EmergencyShutdown): the next response is
    // failed and terminates the DIN session. Guarded by TIMEOUT_EMERGENCY_SHUTDOWN_GUARD in the engine so a
    // silent EV cannot hold the connection open.
    bool emergency_shutdown{false};

    // Latest EVSE error reported by the module (send_error / reset_error). Stamped into DC charge
    // responses; EmergencyShutdown aborts the session (handled in the engine). DIN is DC-only (no RCD).
    d20::EvseErrorCode active_error{d20::EvseErrorCode::None};

    std::optional<dt::DcEvseStatusCode> error_status_code() const {
        switch (active_error) {
        case d20::EvseErrorCode::UtilityInterruptEvent:
            return dt::DcEvseStatusCode::EVSE_UtilityInterruptEvent;
        case d20::EvseErrorCode::Malfunction:
            return dt::DcEvseStatusCode::EVSE_Malfunction;
        case d20::EvseErrorCode::EmergencyShutdown:
            return dt::DcEvseStatusCode::EVSE_EmergencyShutdown;
        default:
            return std::nullopt;
        }
    }

    // Latest isolation-monitoring result reported by the module (update_isolation_status). Absent until
    // the module reports one.
    std::optional<d20::IsolationStatus> reported_isolation_status{std::nullopt};

    // The EVSEIsolationStatus to report in a DC response, or nullopt when the module has not reported one
    // and the caller should keep its own value. CableCheckRes takes it too: the level derived from the
    // cable check's own progress (Invalid while monitoring, Valid once finished) cannot express Warning
    // or Fault, and the module reports its result before signalling cable_check_finished.
    std::optional<dt::IsolationLevel> reported_isolation_level() const {
        if (not reported_isolation_status.has_value()) {
            return std::nullopt;
        }
        switch (reported_isolation_status.value()) {
        case d20::IsolationStatus::Invalid:
            return dt::IsolationLevel::Invalid;
        case d20::IsolationStatus::Valid:
            return dt::IsolationLevel::Valid;
        case d20::IsolationStatus::Warning:
            return dt::IsolationLevel::Warning;
        case d20::IsolationStatus::Fault:
            return dt::IsolationLevel::Fault;
        case d20::IsolationStatus::NoImd:
            // DIN SPEC 70121 isolationLevelType has no No_IMD enumerator (ISO 15118-2 does). With no
            // insulation monitoring device fitted no fault was detected, so report Valid.
            return dt::IsolationLevel::Valid;
        }
        return std::nullopt;
    }

    // The EVSEIsolationStatus for a DC response after the cable check: the module's report when it made
    // one, else derived from whether the cable check has finished.
    dt::IsolationLevel isolation_level() const {
        return reported_isolation_level().value_or(cable_check_done ? dt::IsolationLevel::Valid
                                                                    : dt::IsolationLevel::Invalid);
    }
    float present_voltage{0.0f};
    float present_current{0.0f};

    bool session_stopped{false};
    // DIN signals a pause only via a later re-join, so the SECC never sets session_paused (kept for the
    // shared engine's is_finished()/is_paused() interface).
    bool session_paused{false};
    // Last CP state reported by the module (CpStateChanged control event); updated by the engine.
    // Initial A: only "== B" decisions are taken from it and B is always an explicit report.
    d20::CpState current_cp_state{d20::CpState::A};
    // The session ended on an error condition detected outside a response (CP State A / unplug,
    // [V2G-DC-962]): the Session closes the TCP connection immediately, no EV-first linger.
    bool session_ended_with_error{false};
    // A PowerDeliveryRes(ReadyToChargeState=FALSE, OK) was sent: the next WeldingDetection/SessionStop
    // request requires CP State B within V2G_SECC_CPState_Detection_Timeout ([V2G-DC-988]/[V2G-DC-556]).
    bool power_delivery_stopped{false};
    // Set from CableCheck onward (the EV has moved to CP State C/D for the DC charging phase): while
    // this holds and the session has not been stopped, an unexpected CP State B is a fault
    // ([V2G-DC-668]) that ends the session with an EVSE-initiated emergency shutdown.
    bool expect_cp_state_cd{false};
    // Armed by the SessionStop state on a positive Res; drained by Session::send_response() right
    // after the response hit the wire to emit feedback.session_stop_res_sent ([V2G-DC-968] anchor).
    std::optional<session::feedback::SessionStopAction> session_stop_res_pending{};

private:
    const std::optional<d20::ControlEvent>& current_control_event;
    MessageExchange& message_exchange;

    message_din::datatypes::SessionId session_id{};

    // Last charge progress handed to the module; the change filter of report_charge_progress().
    std::optional<session::feedback::DcEvChargeProgress> last_reported_charge_progress{std::nullopt};
    // Last DC_EVStatus handed to the module; the change filter of report_ev_status().
    std::optional<message_din::datatypes::DcEvStatus> last_reported_ev_status{std::nullopt};

    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};

    bool requested_shutdown{false};
};

} // namespace iso15118::din
