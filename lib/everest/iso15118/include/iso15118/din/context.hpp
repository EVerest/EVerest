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
    bool charger_stop_requested{false};
    float present_voltage{0.0f};
    float present_current{0.0f};

    bool session_stopped{false};
    // DIN signals a pause only via a later re-join, so the SECC never sets session_paused (kept for the
    // shared engine's is_finished()/is_paused() interface).
    bool session_paused{false};

private:
    const std::optional<d20::ControlEvent>& current_control_event;
    MessageExchange& message_exchange;

    message_din::datatypes::SessionId session_id{};

    d20::Timeouts& timeouts;
    std::optional<d20::TimeoutType> current_timeout{std::nullopt};

    bool requested_shutdown{false};
};

} // namespace iso15118::din
