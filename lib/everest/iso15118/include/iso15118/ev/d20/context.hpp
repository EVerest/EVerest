// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
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

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118::ev::d20 {

using SessionLogger = ::iso15118::session::SessionLogger;

// The EV is the inverse of the SECC: it ENCODES requests and DECODES responses.
class MessageExchange {
public:
    MessageExchange() = default;

    // Defer serialization to transmit time. Requests queue in submission order so a
    // state's request survives the next state's enter().
    template <typename Msg> void set_request(const Msg& msg) {
        PendingRequest entry;
        entry.serialize = [msg](io::StreamOutputView view) { return message_20::serialize(msg, view); };
        entry.out_type = message_20::PayloadTypeTrait<Msg>::type;
        requests.push_back(std::move(entry));
    }

    bool has_request() const {
        return not requests.empty();
    }

    // Encode the oldest pending request to EXI bytes; std::nullopt on no-request or encode failure.
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

    // output: FIFO of pending requests, sent oldest-first across reactor passes.
    std::deque<PendingRequest> requests;

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
            everest::lib::util::monitor<DcChargeParams>* dc_params_ = nullptr);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

    template <typename MessageType> void respond(const MessageType& msg) {
        message_exchange.set_request(msg);
    }

    // Control-event seam (mirrors iso15118::d20::Context). The pump owns the
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

    void stop_session(bool stop) {
        session_stopped = stop;
    }

    bool is_session_stopped() const {
        return session_stopped;
    }

    // EV-initiated stop latch. Set once (in any state) and read by DC_ChargeLoop so
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
    // Returns a default DcChargeParams when no monitor was wired in.
    DcChargeParams get_dc_params() const {
        if (dc_params == nullptr) {
            return {};
        }
        auto h = dc_params->handle();
        return *h;
    }

    // Contains the EVSE received data
    EVSESessionInfo evse_session_info;

    // Advertised SupportedAppProtocol list, set from the ctor (config-driven via
    // EvConfig). Read by the SupportedAppProtocol state. Only -20 is wired.
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols;

    const iso15118::ev::Feedback feedback;

    SessionLogger& log;

private:
    MessageExchange& message_exchange;

    message_20::datatypes::Identifier evcc_id;

    const std::optional<ControlEvent>& current_control_event;

    // Non-owning handle to the module -> FSM DC-params channel; null when unwired
    // (many tests construct the Context directly). Non-const because acquiring the
    // monitor lock mutates its mutex; read access is a locked-copy snapshot.
    everest::lib::util::monitor<DcChargeParams>* dc_params;

    SessionId session{std::array<uint8_t, SessionId::ID_LENGTH>{}};

    bool session_stopped{false};

    bool stop_charging_requested{false};

    std::optional<io::sha512_hash_t> charger_cert_hash{std::nullopt};

    std::optional<io::sha512_hash_t> charger_cert_session_hash{std::nullopt};
};

} // namespace iso15118::ev::d20
