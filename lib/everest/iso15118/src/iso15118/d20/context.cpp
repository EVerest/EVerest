// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/context.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d20 {

Context::Context(session::feedback::Callbacks feedback_callbacks, session::SessionLogger& logger,
                 SessionConfig session_config_, std::optional<PauseContext>& pause_ctx_,
                 const std::optional<ControlEvent>& current_control_event_, MessageExchange& message_exchange_,
                 Timeouts& timeouts_) :
    feedback(std::move(feedback_callbacks)),
    log(logger),
    session_config(std::move(session_config_)),
    pause_ctx(pause_ctx_),
    current_control_event{current_control_event_},
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

std::unique_ptr<msg::d20::Variant> Context::pull_request() {
    return message_exchange.pull_input<msg::d20::Variant>();
}

msg::d20::Type Context::peek_request_type() const {
    return message_exchange.peek_input_type<msg::d20::Type>();
}

} // namespace iso15118::d20
