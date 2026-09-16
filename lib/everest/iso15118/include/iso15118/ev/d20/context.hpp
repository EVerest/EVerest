// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/ev/sap_offer.hpp>
#include <iso15118/ev/service_family.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/protocol.hpp>

namespace iso15118::ev::d20 {

// The EV is the inverse of the SECC: it ENCODES requests and DECODES responses.
class MessageExchange {
public:
    MessageExchange() = default;

    // Serialization is deferred to transmit time; exactly one request may be pending.
    // Queueing a second one is a protocol violation, so it throws instead of reaching the wire.
    template <typename Msg> void set_request(const Msg& msg) {
        if (request.has_value()) {
            throw std::logic_error("EV request slot already occupied: a state produced a request while a previous "
                                   "one is still pending");
        }
        PendingRequest entry;
        entry.serialize = [msg](io::StreamOutputView view) { return message_20::serialize(msg, view); };
        entry.out_type = message_20::PayloadTypeTrait<Msg>::type;
        request = std::move(entry);
    }

    bool has_request() const {
        return request.has_value();
    }

    // Encode the pending request to EXI bytes; std::nullopt on no-request or encode failure.
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request();

    // Inbound (DECODE).
    void set_response(std::unique_ptr<message_20::Variant> new_response);
    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

private:
    static constexpr std::size_t OUT_BUFFER_SIZE = 4096;

    struct PendingRequest {
        std::function<std::size_t(io::StreamOutputView)> serialize;
        io::v2gtp::PayloadType out_type{io::v2gtp::PayloadType::Part20Main};
    };

    // output: single pending request, taken by the session before the next response.
    std::optional<PendingRequest> request;

    // input: decoded response.
    std::unique_ptr<message_20::Variant> response{nullptr};

    // Must remain the LAST member: the cbv2g EXI encoder can write past the end
    // of this buffer on an oversized payload, so keeping it at the object tail
    // confines the overrun instead of corrupting an adjacent member.
    std::array<uint8_t, OUT_BUFFER_SIZE> out_buffer{};
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// Session-scoped options beyond the positional Context arguments.
struct SessionOptions {
    // Preferred charge-loop control mode; ServiceDetail picks the parameter set matching it and falls
    // back to the first offered set.
    message_20::datatypes::ControlMode control_mode{message_20::datatypes::ControlMode::Dynamic};
    std::vector<message_20::datatypes::Authorization> supported_auth_options{message_20::datatypes::Authorization::EIM};
    // The owner reports CpState events: DC_CableCheck holds its first request until state C/D.
    bool has_cp_state_feedback{false};
    // Re-join this paused session (SessionSetupReq carries it; OK_OldSessionJoined expected).
    std::optional<std::array<uint8_t, SessionId::ID_LENGTH>> resumed_session_id{std::nullopt};
    // schema_id -> protocol map of the SAP offer; empty = every offered entry is ISO 15118-20.
    std::vector<OfferedProtocol> offered_protocols{};
};

class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_,
            message_20::datatypes::Identifier evcc_id_,
            std::vector<message_20::SupportedAppProtocol> advertised_app_protocols_,
            const std::optional<ControlEvent>& current_control_event_,
            everest::lib::util::monitor<DcChargeParams>& dc_params_,
            everest::lib::util::monitor<AcChargeParams>& ac_params_,
            message_20::datatypes::ServiceCategory requested_service_, DerControlFunctions der_control_functions_ = {},
            bool der_stop_on_unsupported_functions_ = true, SessionOptions options_ = {});
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

    template <typename MessageType> void send_request(const MessageType& msg) {
        message_exchange.set_request(msg);
    }

    // Control-event seam (mirrors iso15118::d20::Context). The Session owns the
    // optional and feeds CONTROL_MESSAGE; states read the active event by type.
    template <typename T> T const* get_control_event() {
        if (not current_control_event.has_value()) {
            return nullptr;
        }
        if (not std::holds_alternative<T>(*current_control_event)) {
            return nullptr;
        }
        return &std::get<T>(*current_control_event);
    }

    void stop_session() {
        session_stopped = true;
    }

    bool is_session_stopped() const {
        return session_stopped;
    }

    // EV-initiated stop flag. Set once (in any state) and read by DC_ChargeLoop so
    // a stop requested before that state is entered still drives PowerDelivery(Stop).
    void set_stop_charging_requested(bool requested) {
        stop_charging_requested = requested;
    }

    bool is_stop_charging_requested() const {
        return stop_charging_requested;
    }

    // EV-initiated pause flag; same latch semantics as the stop flag.
    void set_pause_charging_requested(bool requested) {
        pause_charging_requested = requested;
    }

    bool is_pause_charging_requested() const {
        return pause_charging_requested;
    }

    // Pause wins over stop only when no stop was requested.
    message_20::datatypes::ChargingSession requested_stop_reason() const {
        return (pause_charging_requested and not stop_charging_requested)
                   ? message_20::datatypes::ChargingSession::Pause
                   : message_20::datatypes::ChargingSession::Terminate;
    }

    // SessionStop(Pause) acknowledged: stopped, and the session id can be re-joined.
    void pause_session() {
        session_stopped = true;
        session_paused = true;
    }

    bool is_session_paused() const {
        return session_paused;
    }

    const SessionOptions& options() const {
        return session_options;
    }

    message_20::datatypes::ControlMode preferred_control_mode() const {
        return session_options.control_mode;
    }

    // Control mode of the parameter set ServiceDetail selected; the preferred mode until then.
    message_20::datatypes::ControlMode selected_control_mode() const {
        return selected_control_mode_.value_or(session_options.control_mode);
    }

    void set_selected_control_mode(message_20::datatypes::ControlMode mode) {
        selected_control_mode_ = mode;
    }

    // Scheduled mode: ScheduleTupleID chosen in ScheduleExchange, echoed by PowerDelivery.
    std::optional<uint8_t> selected_schedule_tuple_id() const {
        return selected_schedule_tuple_id_;
    }

    void set_selected_schedule_tuple_id(uint8_t id) {
        selected_schedule_tuple_id_ = id;
    }

    bool has_cp_state_feedback() const {
        return session_options.has_cp_state_feedback;
    }

    // Last reported control pilot state (CpState event); false until reported.
    bool cp_state_c_or_d() const {
        return cp_state_c_or_d_;
    }

    void set_cp_state(bool c_or_d) {
        cp_state_c_or_d_ = c_or_d;
    }

    // Set by SupportedAppProtocol from the negotiated schema_id.
    std::optional<ProtocolId> negotiated_protocol() const {
        return negotiated_protocol_;
    }

    void set_negotiated_protocol(ProtocolId protocol) {
        negotiated_protocol_ = protocol;
    }

    const message_20::datatypes::Identifier& get_evcc_id() const {
        return evcc_id;
    }

    SessionId& get_session() {
        return session;
    }

    const SessionId& get_session() const {
        return session;
    }

    // Locked-copy snapshot of the EV DC charge params (module -> FSM channel).
    DcChargeParams get_dc_params() const {
        auto h = dc_params.handle();
        return *h;
    }

    // Locked-copy snapshot of the EV AC charge params (module -> FSM channel).
    AcChargeParams get_ac_params() const {
        auto h = ac_params.handle();
        return *h;
    }

    // Energy service requested at construction; ServiceSelection sends exactly this.
    message_20::datatypes::ServiceCategory selected_service() const {
        return selected_service_;
    }

    bool is_ac_family() const {
        return ev::is_ac_family(selected_service_);
    }

    // AC connector of the parameter set ServiceDetail selected. Decides both how an advertised
    // total is split across lines and whether the _L2/_L3 peers may be emitted at all, so the AC
    // states must not guess it. Unset until an AC parameter set is chosen, and never set for DC.
    std::optional<message_20::datatypes::AcConnector> selected_ac_connector() const {
        return selected_ac_connector_;
    }

    void set_selected_ac_connector(message_20::datatypes::AcConnector connector) {
        selected_ac_connector_ = connector;
    }

    // The connector the AC states emit for. SinglePhase is the reading under which the base
    // element is never a sum.
    message_20::datatypes::AcConnector ac_connector() const {
        return selected_ac_connector_.value_or(message_20::datatypes::AcConnector::SinglePhase);
    }

    // IEC DER control functions the EV supports (config-driven), matched against the
    // SECC's AC_DER_IEC parameter sets in ServiceDetail.
    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_supported_functions() const {
        return der_supported_functions_;
    }

    // true -> stop the session when no offered AC_DER_IEC Dynamic set is a subset of
    // the supported functions; false -> select the first Dynamic set and warn.
    bool der_stop_on_unsupported_functions() const {
        return der_stop_on_unsupported_functions_;
    }

    // Negotiated functions for the selected AC_DER_IEC parameter set (offered mask AND
    // supported mask). Set by ServiceDetail, read by AC_DER_IEC_ChargeLoop.
    void set_der_negotiated_functions(std::bitset<DER_CONTROL_FUNCTION_COUNT> functions) {
        der_negotiated_functions_ = functions;
    }

    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_negotiated_functions() const {
        return der_negotiated_functions_;
    }

    // EVSE-reported session data, populated by AuthorizationSetup and read by the
    // Authorization states.
    EVSESessionInfo& get_evse_session_info() {
        return evse_session_info;
    }

    // Advertised SupportedAppProtocol list, set from the ctor (config-driven via
    // EvConfig). Read by the SupportedAppProtocol state. Only -20 is wired.
    const std::vector<message_20::SupportedAppProtocol>& get_advertised_app_protocols() const {
        return advertised_app_protocols;
    }

    const iso15118::ev::Feedback feedback;

private:
    MessageExchange& message_exchange;

    message_20::datatypes::Identifier evcc_id;

    const std::optional<ControlEvent>& current_control_event;

    // Module -> FSM DC-params channel. Non-const because acquiring the monitor lock
    // mutates its mutex; read access is a locked-copy snapshot.
    everest::lib::util::monitor<DcChargeParams>& dc_params;

    // Module -> FSM AC-params channel; same locked-copy-snapshot contract as dc_params.
    everest::lib::util::monitor<AcChargeParams>& ac_params;

    message_20::datatypes::ServiceCategory selected_service_;

    std::optional<message_20::datatypes::AcConnector> selected_ac_connector_{};

    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_supported_functions_{};

    bool der_stop_on_unsupported_functions_{true};

    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_negotiated_functions_{};

    EVSESessionInfo evse_session_info;

    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols;

    SessionId session{std::array<uint8_t, SessionId::ID_LENGTH>{}};

    bool session_stopped{false};
    bool session_paused{false};

    bool stop_charging_requested{false};
    bool pause_charging_requested{false};

    SessionOptions session_options;
    std::optional<message_20::datatypes::ControlMode> selected_control_mode_{std::nullopt};
    std::optional<uint8_t> selected_schedule_tuple_id_{std::nullopt};
    bool cp_state_c_or_d_{false};
    std::optional<ProtocolId> negotiated_protocol_{std::nullopt};
};

} // namespace iso15118::ev::d20
