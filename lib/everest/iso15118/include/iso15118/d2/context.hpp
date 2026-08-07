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

namespace iso15118::d2 {

// Message exchange for ISO 15118-2, mirroring d20::MessageExchange over the message_2 types. It serves
// both roles: the SECC stores the incoming decoded request in the request slot and stages the outgoing
// response in the response slot; the EV side uses it the other way around.
class MessageExchange {
public:
    explicit MessageExchange(io::StreamOutputView);

    void set_request(std::unique_ptr<message_2::Variant> new_request);
    std::unique_ptr<message_2::Variant> pull_request();
    message_2::Type peek_request_type() const;
    bool has_request() const {
        return request != nullptr;
    }

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

    // Stage an already-encoded message EXI verbatim, bypassing the message_2 codec. Used both by the SECC
    // relay splice (Plug-and-Charge CertificateInstallationRes delivered by the backend) and by the EVCC
    // to send a request it signed itself (AuthorizationReq/MeteringReceiptReq/CertificateInstallationReq).
    // `type` is reported for logging only; it defaults to None (protocol-neutral, like every d2 response).
    void set_raw_response(const uint8_t* data, size_t len, message_2::Type type = message_2::Type::None);

    std::tuple<bool, size_t, io::v2gtp::PayloadType, message_2::Type> check_and_clear_response();
    bool has_response() const {
        return response_available;
    }

private:
    // input
    std::unique_ptr<message_2::Variant> request{nullptr};

    // output
    const io::StreamOutputView response;
    size_t response_size{0};
    bool response_available{false};
    io::v2gtp::PayloadType payload_type{io::v2gtp::PayloadType::SAP};
    message_2::Type response_type{message_2::Type::None};
    std::any response_message;
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// SECC-side ISO 15118-2 context, the mirror of d20::Context. It holds the SECC session configuration,
// the module-facing feedback (reused d20 callbacks), the reused d20 control event / timeouts, and the
// runtime session state (assigned session id, present V/I, advertised SAScheduleList, ...).
class Context {
public:
    Context(session::feedback::Callbacks, d2::SessionConfig, std::optional<PauseContext>&,
            const std::optional<d20::ControlEvent>&, MessageExchange&, d20::Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_2::Variant> pull_request();
    message_2::Type peek_request_type() const;
    bool has_request() const {
        return message_exchange.has_request();
    }

    template <typename MessageType> void respond(const MessageType& msg) {
        // Two situations end the session by failing every response from here on, whatever state built
        // it, and terminating with it. The state saw its own OK response, so session_stopped is set here
        // as well.
        //   - The EV kept the session going beyond the grace period after an EVSE-initiated stop
        //     (charger_stop_ignored, armed by the engine's STOP_CHARGING guard, EvseV2G stop_hlc parity).
        //   - The module reported an emergency shutdown (send_error). The standard has the SECC answer
        //     FAILED and only then terminate the connection [V2G2-539]/[V2G2-034], rather than dropping the TCP
        //     connection and leaving the EV without a reason. The physical shutdown itself runs over the
        //     control pilot, not over V2G ([V2G2-880] NOTE 1 / [V2G-DC-638] NOTE 2), so this response
        //     reports the emergency, it never enforces it.
        if ((charger_stop_ignored or emergency_shutdown) and
            msg.response_code < message_2::datatypes::ResponseCode::FAILED) {
            auto failed = msg;
            failed.response_code = message_2::datatypes::ResponseCode::FAILED;
            session_stopped = true;
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
            message_exchange.set_response(failed);
            return;
        }
        // Every FAILED_* response terminates an ISO-2 session (all states set session_stopped on
        // FAILED). Arm the marker here centrally so Session::send_response() reports
        // FailedTermination once the response hit the wire: oscillator off without delay and
        // SECC-side TCP close (DIN [V2G-DC-942]/[V2G-DC-940] semantics, mirrored for ISO-2).
        if (msg.response_code >= message_2::datatypes::ResponseCode::FAILED) {
            session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
        }
        message_exchange.set_response(msg);
    }

    // Relay-only splice for the CertificateInstallation pass-through (see MessageExchange::set_raw_response).
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

    // Fills the response header with the assigned session id.
    void setup_header(message_2::Header& header) const;

    // Reports the EV's DC_EVStatus (ready flag, error code, RESS state of charge) to the module. Every DC
    // request of the charging sequence carries it, so this forwards on change only (EvseV2G
    // publish_iso_DcEvStatus parity); without it the EV state of charge never reaches EvseManager/OCPP.
    void report_ev_status(const dt::DC_EVStatus& status);

    // Hands the EV's charge progress (remaining times, completion flags) to the module, on change only.
    // The EV repeats the values in every CurrentDemandReq, and PowerDeliveryReq carries the completion
    // flags without the remaining times -- those stay absent there rather than being reported as zero.
    void report_charge_progress(const session::feedback::DcEvChargeProgress& progress);

    void set_session_id(const dt::SessionId& id) {
        session_id = id;
    }
    const dt::SessionId& get_session_id() const {
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

    void request_shutdown() {
        requested_shutdown = true;
    }
    [[nodiscard]] bool shutdown_requested() const {
        return requested_shutdown;
    }

    const session::Feedback feedback;
    d2::SessionConfig session_config;
    std::optional<PauseContext>& pause_ctx;

    // Runtime session state (SECC).
    dt::SessionId session_id{};
    float present_voltage{0.0f};
    float present_current{0.0f};
    // Latest meter reading forwarded by the module (latched by the engine in any state, so the first
    // charge-loop response already carries one). Reported as MeterInfo in every ChargingStatusRes and, in
    // PnC sessions only, in every CurrentDemandRes.
    std::optional<dt::MeterInfo> latest_meter_info{};
    bool authorized{false};

    // Set in SessionSetup when the EV re-joins a paused session (OK_OldSessionJoined). The resumed
    // session runs the full message sequence again, but [V2G2-741] constrains the SECC's values: only
    // the previously selected payment option (pause_ctx->selected_payment_option) may be offered.
    bool session_resumed{false};

    // Plug-and-Charge (Contract) session state. Set in PaymentServiceSelection (contract_selected) and
    // PaymentDetails (leaf DER + eMAID + chain PEM + GenChallenge); consumed by the Authorization state
    // to verify the AuthorizationReq signature and challenge and to publish the require_auth_pnc token.
    bool contract_selected{false};
    // Certificate service selection from PaymentServiceSelectionReq (ISO 15118-2 VAS, ServiceID 2). Gate
    // the certificate exchange in PaymentDetails: a CertificateInstallation/Update request is accepted only
    // if the matching action was selected [V2G2-432]. Table 106 ParameterSetID 1 = Installation, 2 = Update;
    // a SelectedService for the certificate service without a ParameterSetID permits either exchange.
    bool cert_install_selected{false};
    bool cert_update_selected{false};
    // Set once the SUT has received and accepted a MeteringReceipt, so it stops setting ReceiptRequired
    // in the subsequent charge-loop responses.
    bool receipt_received{false};
    std::vector<uint8_t> contract_leaf_der{};
    std::string contract_emaid{};
    std::string contract_chain_pem{};
    dt::GenChallenge gen_challenge{};
    bool cable_check_done{false};
    // PnC only: the higher layer rejected the authorization because the contract certificate is revoked
    // (AuthorizationResponse control event); answered with FAILED_CertificateRevoked instead of FAILED.
    bool certificate_revoked{false};
    // Set when the module reports a finished-but-failed cable check (isolation fault) via
    // CableCheckFinished{success=false}; distinct from "not finished yet". Drives CableCheckRes FAILED.
    bool cable_check_fault{false};

    // Latest EVSE error reported by the module (send_error / reset_error). Stamped into DC charge
    // responses; EmergencyShutdown aborts the session (handled in the engine).
    d20::EvseErrorCode active_error{d20::EvseErrorCode::None};

    // Maps the active error to the DC_EVSEStatusCode to advertise, or nullopt when no override applies.
    std::optional<dt::DC_EVSEStatusCode> error_status_code() const {
        switch (active_error) {
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

    // An active RCD error has no DC_EVSEStatusCode; AC_EVSEStatus carries it in its own RCD flag, which
    // is mandatory in every AC response ([Table 104]), not just the charge loop (EvseV2G
    // populate_ac_evse_status stamps it into all of them).
    bool rcd_error() const {
        return active_error == d20::EvseErrorCode::RCD;
    }

    // Latest isolation-monitoring result reported by the module (update_isolation_status). Absent until
    // the module reports one.
    std::optional<d20::IsolationStatus> reported_isolation_status{std::nullopt};

    // The EVSEIsolationStatus to report in a DC response, or nullopt when the module has not reported one
    // and the caller should keep its own value. CableCheckRes takes it too: the level derived from the
    // cable check's own progress (Invalid while monitoring, Valid once finished) cannot express Warning,
    // Fault or No_IMD, and the module reports its result before signalling cable_check_finished.
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
            return dt::IsolationLevel::No_IMD;
        }
        return std::nullopt;
    }

    // The EVSEIsolationStatus for a DC response after the cable check: the module's report when it made
    // one, else derived from whether the cable check has finished.
    dt::IsolationLevel isolation_level() const {
        return reported_isolation_level().value_or(cable_check_done ? dt::IsolationLevel::Valid
                                                                    : dt::IsolationLevel::Invalid);
    }
    // Tracks the AC contactor state as reported by ClosedContactor control events. A PowerDelivery(Start)
    // that resumes after a renegotiation finds it already closed (the contactor never re-opened), so the
    // SECC must respond OK immediately rather than wait for a fresh close confirmation that never comes.
    bool ac_contactor_closed{false};
    bool dc_charging{true};
    // Set once a PowerDeliveryReq with ChargeProgress=Start has been accepted. A PowerDeliveryReq with
    // ChargeProgress=Renegotiate received before any Start is illegal and answered FAILED [V2G2-812].
    bool power_delivery_started{false};
    // EVSE-initiated stop (module stop_charging / driver shutdown), latched by the engine in ANY state:
    // every subsequent status-carrying response tells the EV to stop (EVSENotification StopCharging,
    // DC status code EVSE_Shutdown) -- EvseV2G parity, which stamps its context notification into every
    // response after handle_stop_charging.
    bool charger_stop_requested{false};
    // The EV ignored the stop request beyond the STOP_CHARGING guard: respond() fails every further
    // response and ends the session (EvseV2G stop_hlc parity). Set by the engine on the guard timeout.
    bool charger_stop_ignored{false};
    // The module reported an emergency shutdown (send_error EmergencyShutdown): the next response is
    // failed and terminates the ISO 15118-2 session. Guarded by TIMEOUT_EMERGENCY_SHUTDOWN_GUARD in the engine so a
    // silent EV cannot hold the connection open.
    bool emergency_shutdown{false};
    uint8_t sa_schedule_tuple_id{1};
    dt::SAScheduleList sa_schedule_list{};

    bool session_stopped{false};
    bool session_paused{false};
    // The session ended on an error condition detected outside a response (CP State A / unplug):
    // makes Session close the TCP connection immediately instead of granting the EV-first linger.
    bool session_ended_with_error{false};
    // Armed by the SessionStop state on a positive Res; drained by Session::send_response() right
    // after the response hit the wire to emit feedback.session_stop_res_sent ([V2G-DC-968] anchor).
    std::optional<session::feedback::SessionStopAction> session_stop_res_pending{};
    // Last CP state reported by the module (CpStateChanged control event); updated by the engine.
    d20::CpState current_cp_state{d20::CpState::A};
    // A PowerDeliveryReq(Stop) was processed: the next WeldingDetection/SessionStop request requires
    // CP State B within V2G_SECC_CPState_Detection_Timeout ([V2G2-920]..[V2G2-922]).
    bool power_delivery_stopped{false};

private:
    // Last charge progress handed to the module; the change filter of report_charge_progress().
    std::optional<session::feedback::DcEvChargeProgress> last_reported_charge_progress{std::nullopt};
    // Last DC_EVStatus handed to the module; the change filter of report_ev_status().
    std::optional<dt::DC_EVStatus> last_reported_ev_status{std::nullopt};

    const std::optional<d20::ControlEvent>& current_control_event;
    MessageExchange& message_exchange;
    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};
    bool requested_shutdown{false};
};

} // namespace iso15118::d2
