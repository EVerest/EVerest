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
#include <iso15118/session/logger.hpp>

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
    io::v2gtp::PayloadType payload_type;
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
    Context(session::feedback::Callbacks, session::SessionLogger&, SessionConfig,
            const std::optional<d20::ControlEvent>&, MessageExchange&, d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    // --- receive path (EV -> SECC): the incoming decoded request lives in the request slot ---
    std::unique_ptr<message_din::Variant> pull_request();
    message_din::Type peek_request_type() const;

    // --- send path (SECC -> EV): the outgoing response goes into the response slot ---
    template <typename ResponseType> void respond(const ResponseType& msg) {
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

    d20::TimeoutType const* get_active_timeout() {
        if (not current_timeout.has_value()) {
            return nullptr;
        }
        return &current_timeout.value();
    }

    void set_active_timeout(d20::TimeoutType timeout) {
        current_timeout = timeout;
    }

    void request_shutdown() {
        requested_shutdown = true;
    }

    [[nodiscard]] bool shutdown_requested() const {
        return requested_shutdown;
    }

    const session::Feedback feedback;

    session::SessionLogger& log;

    SessionConfig session_config;

    // Runtime quantities driven by control events during the session.
    bool authorized{false};
    bool cable_check_done{false};
    // Set when the module reports a finished-but-failed cable check (isolation fault) via
    // CableCheckFinished{success=false} [V2G-DC-890]; distinct from "not finished yet".
    bool cable_check_fault{false};
    bool charger_stop_requested{false};

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

    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};

    bool requested_shutdown{false};
};

} // namespace iso15118::din
