// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <tuple>

#include <iso15118/d20/timeout.hpp>
#include <iso15118/message/payload_type.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/message_exchange.hpp>
#include <iso15118/session/feedback.hpp>
#include <iso15118/session/logger.hpp>

#include "config.hpp"
#include "control_event.hpp"
#include "ev_information.hpp"
#include "ev_session_info.hpp"
#include "session.hpp"

namespace iso15118::d20 {

// forward declare
class ControlEventQueue;

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

class Context {
public:
    // FIXME (aw): bundle arguments
    Context(session::feedback::Callbacks, session::SessionLogger&, d20::SessionConfig, std::optional<PauseContext>&,
            const std::optional<ControlEvent>&, MessageExchange&, Timeouts&);

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    std::unique_ptr<msg::d20::Variant> pull_request();
    msg::d20::Type peek_request_type() const;

    template <typename MessageType> void respond(const MessageType& msg) {
        message_exchange.set_d20_response(msg);
    }

    template <typename Msg> std::optional<Msg> get_response() {
        return message_exchange.get_d20_response<Msg>();
    }

    const auto& get_control_event() {
        return current_control_event;
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

    void set_new_vehicle_cert_hash(std::optional<io::sha512_hash_t> hash) {
        vehicle_cert_hash = hash;
    }

    auto get_new_vehicle_cert_hash() const {
        return vehicle_cert_hash;
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

    void set_active_timeout(TimeoutType timeout) {
        current_timeout = timeout;
    }

    const session::Feedback feedback;

    session::SessionLogger& log;

    Session session;

    SessionConfig session_config;

    // Contains the EV received data
    EVSessionInfo session_ev_info;
    EVInformation ev_info;

    std::optional<d20::PauseContext>& pause_ctx;

    bool session_stopped{false};
    bool session_paused{false};

    std::optional<UpdateDynamicModeParameters> cache_dynamic_mode_parameters;
    std::optional<AcTargetPower> cache_ac_target_power;
    std::optional<AcPresentPower> cache_ac_present_power;

private:
    const std::optional<ControlEvent>& current_control_event;
    MessageExchange& message_exchange;

    std::optional<io::sha512_hash_t> vehicle_cert_hash{std::nullopt};

    Timeouts& timeouts;

    std::optional<TimeoutType> current_timeout{std::nullopt};
};

} // namespace iso15118::d20
