// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <any>
#include <array>
#include <cstdint>
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

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/ev/d20/session_id.hpp>
#include <iso15118/ev/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

namespace iso15118::ev::d20 {

using SessionLogger = ::iso15118::session::SessionLogger;

// The EV is the inverse of the SECC: it ENCODES requests and DECODES responses.
class MessageExchange {
public:
    MessageExchange() = default;

    // Defer serialization to transmit time; also retain the typed request for get_request<Msg>.
    template <typename Msg> void set_request(const Msg& msg) {
        pending_serialize = [msg](io::StreamOutputView view) { return message_20::serialize(msg, view); };
        out_type = message_20::PayloadTypeTrait<Msg>::type;
        request_available = true;
        request_type = message_20::TypeTrait<Msg>::type;
        request_message = msg;
    }

    bool has_request() const {
        return request_available;
    }

    // Encode the pending request to EXI bytes; std::nullopt on no-request or encode failure.
    std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> take_request();

    template <typename Msg> std::optional<Msg> get_request() {
        static_assert(message_20::TypeTrait<Msg>::type != message_20::Type::None, "Unhandled type!");
        if (message_20::TypeTrait<Msg>::type != request_type) {
            return std::nullopt;
        }
        try {
            return std::any_cast<Msg>(request_message);
        } catch (const std::bad_any_cast&) {
            return std::nullopt;
        }
    }

    // Inbound (DECODE).
    void set_response(std::unique_ptr<message_20::Variant> new_response);
    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

private:
    static constexpr std::size_t OUT_BUFFER_SIZE = 4096;
    std::array<uint8_t, OUT_BUFFER_SIZE> out_buffer{};
    std::function<std::size_t(io::StreamOutputView)> pending_serialize;
    io::v2gtp::PayloadType out_type{io::v2gtp::PayloadType::Part20Main};
    bool request_available{false};

    // output: typed request retained for get_request<Msg> introspection.
    message_20::Type request_type{message_20::Type::None};
    std::any request_message;

    // input: decoded response.
    std::unique_ptr<message_20::Variant> response{nullptr};
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

class Context {
public:
    Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, SessionLogger& logger,
            message_20::datatypes::Identifier evcc_id_, const std::optional<ControlEvent>& current_control_event_);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<message_20::Variant> pull_response();
    message_20::Type peek_response_type() const;

    template <typename MessageType> void respond(const MessageType& msg) {
        message_exchange.set_request(msg);
    }

    template <typename Msg> std::optional<Msg> get_request() {
        return message_exchange.get_request<Msg>();
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

    // Contains the EVSE received data
    EVSESessionInfo evse_session_info;

    message_20::datatypes::RationalNumber dc_pre_charge_target_voltage{0, 0};

    // Advertised SupportedAppProtocol list (config-driven). Defaults to the single
    // ISO 15118-20 DC entry; the Session overwrites this from EvConfig. Only -20 is wired.
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols{
        {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};

    const iso15118::ev::Feedback feedback;

    SessionLogger& log;

private:
    MessageExchange& message_exchange;

    message_20::datatypes::Identifier evcc_id;

    const std::optional<ControlEvent>& current_control_event;

    SessionId session{std::array<uint8_t, SessionId::ID_LENGTH>{}};

    bool session_stopped{false};

    std::optional<io::sha512_hash_t> charger_cert_hash{std::nullopt};

    std::optional<io::sha512_hash_t> charger_cert_session_hash{std::nullopt};
};

} // namespace iso15118::ev::d20
