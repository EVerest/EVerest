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
#include "evse_status.hpp"

namespace iso15118::din {

class MessageExchange {
public:
    explicit MessageExchange(io::StreamOutputView);

    void set_request(std::unique_ptr<message_din::Variant> new_request);
    std::unique_ptr<message_din::Variant> pull_request();

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

private:
    std::unique_ptr<message_din::Variant> request{nullptr};

    const io::StreamOutputView response;
    size_t response_size{0};
    bool response_available{false};
    io::v2gtp::PayloadType payload_type{io::v2gtp::PayloadType::SAP};
    message_din::Type response_type{message_din::Type::None};
    std::any response_message;
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

class Context {
public:
    Context(session::feedback::Callbacks, SessionConfig, const std::optional<d20::ControlEvent>&, MessageExchange&,
            d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_din::Variant> pull_request();

    template <typename ResponseType> void respond(const ResponseType& msg) {
        // Two situations fail every response from here on and terminate with it: the EV kept the session
        // going beyond the grace period after an EVSE-initiated stop, or the module reported an emergency
        // shutdown. DIN sends the FAILED response first ([V2G-DC-866] uses that shape) rather than drop the
        // TCP connection and leave the EV without a reason; the physical shutdown runs over the control pilot.
        if ((evse_status.charger_stop_ignored or evse_status.emergency_shutdown) and
            msg.response_code < message_din::datatypes::ResponseCode::FAILED) {
            auto failed = msg;
            failed.response_code = message_din::datatypes::ResponseCode::FAILED;
            session_stopped = true;
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
            message_exchange.set_response(failed);
            return;
        }
        // Every FAILED_* response terminates a DIN session, so arm the marker centrally here and let
        // Session::send_response() report FailedTermination once the response hit the wire.
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

    // Forwarded on change only (every DC request carries it); without this the EV state of charge never
    // reaches EvseManager/OCPP.
    void report_ev_status(const message_din::datatypes::DcEvStatus& status);

    // On change only. PowerDeliveryReq carries the completion flags without the remaining times, which
    // stay absent there rather than being reported as zero.
    void report_charge_progress(const session::feedback::DcEvChargeProgress& progress);

    void set_session_id(const message_din::datatypes::SessionId& id) {
        session_id = id;
    }

    // True once SessionSetup assigned a SessionID. Before that an unexpected first message is a
    // sequence error [V2G-DC-666], never FAILED_UnknownSession -- [V2G-DC-391] applies to requests
    // inside an established session.
    bool session_established() const {
        return session_id.has_value();
    }

    // All-zero while no session is established: zero is the wire value for "no session"
    // ([V2G-DC-876]), so it is what a response built before SessionSetup should carry.
    message_din::datatypes::SessionId get_session_id() const {
        return session_id.value_or(message_din::datatypes::SessionId{});
    }

    void start_timeout(d20::TimeoutType type, uint32_t time_ms) {
        timeouts.start_timeout(type, time_ms);
    }

    void stop_timeout(d20::TimeoutType type) {
        timeouts.stop_timeout(type);
    }

    // Always armed fresh: Timeouts::start_timeout() refuses an occupied slot, so a state that hands
    // over with a request still parked would otherwise leave the next state inheriting a leftover,
    // possibly already expired, deadline.
    void arm_cp_state_timeout(uint32_t time_ms) {
        timeouts.reset_timeout(d20::TimeoutType::CPSTATE);
        timeouts.start_timeout(d20::TimeoutType::CPSTATE, time_ms);
    }

    // Silent if none is armed; used when a state hands over while parked.
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

    // Read by the CurrentDemand charge loop, which answers EVSE_Shutdown so the EV stops and ends the session.
    void request_shutdown() {
        requested_shutdown = true;
    }

    [[nodiscard]] bool shutdown_requested() const {
        return requested_shutdown;
    }

    const session::Feedback feedback;

    SessionConfig session_config;

    // What the module last reported about the charger, read-only to the states; see evse_status.hpp.
    const EvseStatus& evse() const {
        return evse_status;
    }

    void set_active_error(d20::EvseErrorCode code) {
        evse_status.active_error = code;
    }
    void set_emergency_shutdown() {
        evse_status.emergency_shutdown = true;
    }
    void set_isolation_status(d20::IsolationStatus status) {
        evse_status.reported_isolation_status = status;
    }
    void set_charger_stop_requested(bool requested) {
        evse_status.charger_stop_requested = requested;
    }
    void set_charger_stop_ignored(bool ignored) {
        evse_status.charger_stop_ignored = ignored;
    }
    void set_cp_state(d20::CpState state) {
        evse_status.current_cp_state = state;
    }

    // Latched by the DC states, not the engine, which does not handle PresentVoltageCurrent.
    //
    // Two setters because the states disagree today: PowerDelivery and CurrentDemand latch both values,
    // while PreCharge and WeldingDetection latch the voltage alone. Not unified here, because latching
    // the current in those two would change what an out-of-sequence CurrentDemandRes reports
    // (sequence_error.cpp reads present_current).
    void set_present_values(float voltage, float current) {
        evse_status.present_voltage = voltage;
        evse_status.present_current = current;
    }
    void set_present_voltage(float voltage) {
        evse_status.present_voltage = voltage;
    }

    // Latches: a reported success does not clear a previous fault, nor the other way round.
    void set_cable_check_done() {
        evse_status.cable_check_done = true;
    }
    void set_cable_check_fault() {
        evse_status.cable_check_fault = true;
    }
    // Opening the contactor invalidates a completed cable check, so a post-stop restart must run the
    // physical test again.
    void invalidate_cable_check() {
        evse_status.cable_check_done = false;
        evse_status.cable_check_fault = false;
    }

    std::optional<dt::DcEvseStatusCode> error_status_code() const {
        switch (evse_status.active_error) {
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

    // nullopt when the module has not reported one and the caller should keep its own value. The level
    // derived from the cable check's own progress cannot express Warning or Fault.
    std::optional<dt::IsolationLevel> reported_isolation_level() const {
        if (not evse_status.reported_isolation_status.has_value()) {
            return std::nullopt;
        }
        switch (evse_status.reported_isolation_status.value()) {
        case d20::IsolationStatus::Invalid:
            return dt::IsolationLevel::Invalid;
        case d20::IsolationStatus::Valid:
            return dt::IsolationLevel::Valid;
        case d20::IsolationStatus::Warning:
            return dt::IsolationLevel::Warning;
        case d20::IsolationStatus::Fault:
            return dt::IsolationLevel::Fault;
        case d20::IsolationStatus::NoImd:
            // DIN's isolationLevelType has no No_IMD enumerator (ISO 15118-2 does). With no monitoring device
            // fitted no fault was detected, so report Valid.
            return dt::IsolationLevel::Valid;
        }
        return std::nullopt;
    }

    bool session_stopped{false};
    // Detected outside a response (CP State A / unplug, [V2G-DC-962]): Session closes at once.
    bool session_ended_with_error{false};
    // A PowerDeliveryRes(ReadyToChargeState=FALSE, OK) was sent, so the next request requires CP State B
    // within V2G_SECC_CPState_Detection_Timeout ([V2G-DC-988]/[V2G-DC-556]).
    bool power_delivery_stopped{false};
    // Set from CableCheck onward: while it holds, an unexpected CP State B is a fault ([V2G-DC-668])
    // that ends the session with an EVSE-initiated emergency shutdown.
    bool expect_cp_state_cd{false};
    // Drained by Session::send_response() to emit feedback.session_stop_res_sent ([V2G-DC-968] anchor).
    std::optional<session::feedback::SessionStopAction> session_stop_res_pending{};

private:
    const std::optional<d20::ControlEvent>& current_control_event;
    MessageExchange& message_exchange;

    // Empty until SessionSetup assigns it, which is what separates "no session yet" from a session
    // whose id happens to be zero.
    std::optional<message_din::datatypes::SessionId> session_id{std::nullopt};

    std::optional<session::feedback::DcEvChargeProgress> last_reported_charge_progress{std::nullopt};
    std::optional<message_din::datatypes::DcEvStatus> last_reported_ev_status{std::nullopt};

    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};

    EvseStatus evse_status{};

    bool requested_shutdown{false};
};

} // namespace iso15118::din
