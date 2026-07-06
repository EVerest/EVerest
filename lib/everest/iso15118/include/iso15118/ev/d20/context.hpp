// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <iso15118/io/sha_hash.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <everest/util/async/monitor.hpp>

#include <bitset>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118::ev::d20 {

using SessionLogger = ::iso15118::session::SessionLogger;

// The EV is the inverse of the SECC: it ENCODES requests and DECODES responses.
class MessageExchange {
public:
    MessageExchange() = default;

    // Defer serialization to transmit time. Exactly one request may be pending: the
    // session takes it (take_request) before the next response arrives, so depth is
    // always <= 1. A state that queues a request while one is still pending is a
    // protocol violation; throwing turns it into a loud session stop at the session's
    // reactor boundary rather than two requests on the wire.
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

class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, SessionLogger& logger,
            message_20::datatypes::Identifier evcc_id_,
            std::vector<message_20::SupportedAppProtocol> advertised_app_protocols_,
            const std::optional<ControlEvent>& current_control_event_,
            everest::lib::util::monitor<DcChargeParams>& dc_params_,
            everest::lib::util::monitor<AcChargeParams>& ac_params_,
            message_20::datatypes::ServiceCategory requested_service_, DerControlFunctions der_control_functions_ = {},
            bool der_stop_on_unsupported_functions_ = true);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

    template <typename MessageType> void respond(const MessageType& msg) {
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

    void set_charger_cert_hash(std::optional<io::sha512_hash_t> hash) {
        charger_cert_hash = hash;
    }

    auto get_charger_cert_hash() const {
        return charger_cert_hash;
    }

    void set_charger_cert_session_hash(std::optional<io::sha512_hash_t> hash) {
        charger_cert_session_hash = hash;
    }

    auto get_charger_cert_session_hash() const {
        return charger_cert_session_hash;
    }

    const message_20::datatypes::Identifier& get_evcc_id() const {
        return evcc_id;
    }

    SessionId& get_session() {
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

    // Energy service the EV drives this session. Defaults to the service requested
    // at construction and is refined once ServiceSelection negotiates.
    message_20::datatypes::ServiceCategory selected_service() const {
        return selected_service_;
    }

    void set_selected_service(message_20::datatypes::ServiceCategory service) {
        selected_service_ = service;
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

    SessionLogger& log;

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

    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_supported_functions_{};

    bool der_stop_on_unsupported_functions_{true};

    std::bitset<DER_CONTROL_FUNCTION_COUNT> der_negotiated_functions_{};

    EVSESessionInfo evse_session_info;

    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols;

    SessionId session{std::array<uint8_t, SessionId::ID_LENGTH>{}};

    bool session_stopped{false};

    bool stop_charging_requested{false};

    std::optional<io::sha512_hash_t> charger_cert_hash{std::nullopt};

    std::optional<io::sha512_hash_t> charger_cert_session_hash{std::nullopt};
};

} // namespace iso15118::ev::d20
