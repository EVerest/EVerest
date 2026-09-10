// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/payload_type.hpp>
#include <iso15118/message_2/type.hpp>
#include <iso15118/message_2/variant.hpp>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/session/feedback.hpp>

#include "config.hpp"
#include "evse_status.hpp"
#include "session_parameters.hpp"

namespace iso15118::d2 {

// Serves both roles: the SECC stores the incoming request and stages the outgoing response, the EV
// side the other way around.
class MessageExchange {
public:
    explicit MessageExchange(io::StreamOutputView);

    void set_request(std::unique_ptr<message_2::Variant> new_request);
    std::unique_ptr<message_2::Variant> pull_request();

    template <typename MessageType> void set_response(const MessageType& msg) {
        response_size = message_2::serialize(msg, response);
        response_available = true;
        payload_type = message_2::PayloadTypeTrait<MessageType>::type;
        response_type = message_2::TypeTrait<MessageType>::type;
        response_message = msg;
    }

    template <typename Msg> std::optional<Msg> get_response() {
        static_assert(message_2::TypeTrait<Msg>::type != message_2::Type::None, "Unhandled type!");
        if (message_2::TypeTrait<Msg>::type != response_type) {
            return std::nullopt;
        }
        try {
            return std::any_cast<Msg>(response_message);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    // Stage an already-encoded EXI verbatim, bypassing the message_2 codec: the SECC relay splice for a
    // backend-delivered CertificateInstallationRes, and the EVCC for requests it signed itself.
    void set_raw_response(const uint8_t* data, size_t len, message_2::Type type = message_2::Type::None);

    std::tuple<bool, size_t, io::v2gtp::PayloadType, message_2::Type> check_and_clear_response();
    bool has_response() const {
        return response_available;
    }

private:
    std::unique_ptr<message_2::Variant> request{nullptr};

    const io::StreamOutputView response;
    size_t response_size{0};
    bool response_available{false};
    io::v2gtp::PayloadType payload_type{io::v2gtp::PayloadType::SAP};
    message_2::Type response_type{message_2::Type::None};
    std::any response_message;
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

class Context {
public:
    Context(session::feedback::Callbacks, d2::SessionConfig, std::optional<PauseContext>&,
            const std::optional<d20::ControlEvent>&, MessageExchange&, d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_2::Variant> pull_request();

    template <typename MessageType> void respond(const MessageType& msg) {
        // Two situations fail every response from here on and terminate with it: the EV kept the session
        // going beyond the grace period after an EVSE-initiated stop, or the module reported an emergency
        // shutdown. [V2G2-539]/[V2G2-034] have the SECC answer FAILED first rather than drop the TCP
        // connection and leave the EV without a reason; the physical shutdown runs over the control pilot.
        if ((charger_stop_ignored or evse_status.emergency_shutdown) and
            msg.response_code < message_2::datatypes::ResponseCode::FAILED) {
            auto failed = msg;
            failed.response_code = message_2::datatypes::ResponseCode::FAILED;
            session_stopped = true;
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
            message_exchange.set_response(failed);
            return;
        }
        // Every FAILED_* response terminates an ISO-2 session, so arm the marker centrally here and let
        // Session::send_response() report FailedTermination once the response hit the wire.
        if (msg.response_code >= message_2::datatypes::ResponseCode::FAILED) {
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
        }
        message_exchange.set_response(msg);
    }

    void respond_raw(const std::vector<uint8_t>& exi) {
        message_exchange.set_raw_response(exi.data(), exi.size());
    }

    template <typename Msg> std::optional<Msg> get_response() {
        return message_exchange.get_response<Msg>();
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
    void report_ev_status(const dt::DC_EVStatus& status);

    // The change filter lives here rather than in the loop state, which is rebuilt around a metering
    // receipt and would forget it (EvseV2G publish_dc_ev_target_voltage_current parity).
    using DcSetpoint = std::tuple<double, double, std::optional<double>, std::optional<double>, std::optional<double>>;
    void report_dc_setpoint(const DcSetpoint& setpoint, const session::feedback::DcReqControlMode& mode);

    void set_charge_loop_started() {
        session_params.charge_loop_started = true;
    }
    void clear_charge_loop_started() {
        session_params.charge_loop_started = false;
    }

    // On change only. PowerDeliveryReq carries the completion flags without the remaining times, which
    // stay absent there rather than being reported as zero.
    void report_charge_progress(const session::feedback::DcEvChargeProgress& progress);

    void set_session_id(const dt::SessionId& id) {
        session_id = id;
    }
    // What the module last reported about the charger, read-only to the states; see evse_status.hpp.
    const EvseStatus& evse() const {
        return evse_status;
    }

    void set_present_values(float voltage, float current) {
        evse_status.present_voltage = voltage;
        evse_status.present_current = current;
    }
    void set_meter_info(const dt::MeterInfo& info) {
        evse_status.latest_meter_info = info;
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
    void set_cp_state(d20::CpState state) {
        evse_status.current_cp_state = state;
    }
    // Latches: a reported success does not clear a previous fault, nor the other way round.
    void set_cable_check_done() {
        evse_status.cable_check_done = true;
    }
    void set_cable_check_fault() {
        evse_status.cable_check_fault = true;
    }
    // PowerDelivery owns the open; the engine records the close from the ClosedContactor event.
    void set_contactor_closed(bool closed) {
        evse_status.ac_contactor_closed = closed;
    }
    // Opening the contactor invalidates a completed cable check (ISO 15118-2 8.7.4.3 NOTE 1).
    void invalidate_cable_check() {
        evse_status.cable_check_done = false;
        evse_status.cable_check_fault = false;
    }

    // Session-scoped facts, read-only to the states; see session_parameters.hpp.
    const SessionParameters& session() const {
        return session_params;
    }

    void set_session_resumed() {
        session_params.session_resumed = true;
    }
    void set_contract_selected() {
        session_params.contract_selected = true;
    }
    void set_certificate_services(bool install, bool update) {
        session_params.cert_install_selected = install;
        session_params.cert_update_selected = update;
    }
    void set_contract_identity(std::vector<uint8_t> leaf_der, std::string emaid, std::string chain_pem) {
        session_params.contract_leaf_der = std::move(leaf_der);
        session_params.contract_emaid = std::move(emaid);
        session_params.contract_chain_pem = std::move(chain_pem);
    }
    void set_sa_schedules(dt::SAScheduleList list, uint8_t tuple_id) {
        session_params.sa_schedule_list = std::move(list);
        session_params.sa_schedule_tuple_id = tuple_id;
    }
    void set_receipt_received() {
        session_params.receipt_received = true;
    }
    void set_power_delivery_started() {
        session_params.power_delivery_started = true;
    }
    void set_power_delivery_stopped() {
        session_params.power_delivery_stopped = true;
    }

    // True once SessionSetup assigned or re-joined a SessionID. Before that an unexpected first message
    // is a SequenceError, never FAILED_UnknownSession -- Table 112 does not list that code for SessionSetupRes.
    bool session_established() const {
        return session_id.has_value();
    }

    // All-zero while no session is established: zero is the wire value for "no session" ([V2G2-750]),
    // so it is what a response built before SessionSetup should carry.
    dt::SessionId get_session_id() const {
        return session_id.value_or(dt::SessionId{});
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

    const session::Feedback feedback;
    d2::SessionConfig session_config;
    std::optional<PauseContext>& pause_ctx;

    std::optional<dt::DC_EVSEStatusCode> error_status_code() const {
        switch (evse_status.active_error) {
        case d20::EvseErrorCode::UtilityInterruptEvent:
            return dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent;
        case d20::EvseErrorCode::Malfunction:
            return dt::DC_EVSEStatusCode::EVSE_Malfunction;
        case d20::EvseErrorCode::EmergencyShutdown:
            return dt::DC_EVSEStatusCode::EVSE_EmergencyShutdown;
        default:
            return std::nullopt;
        }
    }

    // An active RCD error has no DC_EVSEStatusCode; AC_EVSEStatus carries it in its own flag, which
    // [Table 104] makes mandatory in every AC response, not just the charge loop.
    bool rcd_error() const {
        return evse_status.active_error == d20::EvseErrorCode::RCD;
    }

    // nullopt when the module has not reported one and the caller should keep its own value. The level
    // derived from the cable check's own progress cannot express Warning, Fault or No_IMD.
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
            return dt::IsolationLevel::No_IMD;
        }
        return std::nullopt;
    }

    dt::IsolationLevel isolation_level() const {
        return reported_isolation_level().value_or(evse_status.cable_check_done ? dt::IsolationLevel::Valid
                                                                                : dt::IsolationLevel::Invalid);
    }
    // A PowerDelivery(Start) resuming after a renegotiation finds the contactor already closed, so the
    // SECC must answer OK at once rather than wait for a close confirmation that never comes.
    // A Renegotiate before any Start is illegal and answered FAILED [V2G2-812].
    // Latched by the engine in ANY state: every later status-carrying response tells the EV to stop.
    // Set by the engine on the STOP_CHARGING guard timeout; respond() then fails every further response.
    bool charger_stop_ignored{false};
    // Guarded by TIMEOUT_EMERGENCY_SHUTDOWN_GUARD so a silent EV cannot hold the connection open.

    bool session_stopped{false};
    bool session_paused{false};
    // Detected outside a response (CP State A / unplug): Session closes at once, with no EV-first linger.
    bool session_ended_with_error{false};
    // Drained by Session::send_response() to emit feedback.session_stop_res_sent ([V2G-DC-968] anchor).
    std::optional<session::feedback::SessionStopAction> session_stop_res_pending{};

private:
    // Empty until SessionSetup assigns it, which is what separates "no session yet" from a session
    // whose id happens to be zero.
    std::optional<dt::SessionId> session_id{std::nullopt};
    std::optional<session::feedback::DcEvChargeProgress> last_reported_charge_progress{std::nullopt};
    std::optional<dt::DC_EVStatus> last_reported_ev_status{std::nullopt};
    std::optional<DcSetpoint> last_forwarded_dc_setpoint{std::nullopt};

    const std::optional<d20::ControlEvent>& current_control_event;
    MessageExchange& message_exchange;
    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};
    EvseStatus evse_status{};
    SessionParameters session_params{};
};

} // namespace iso15118::d2
