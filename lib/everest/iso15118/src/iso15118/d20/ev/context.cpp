// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/context.hpp>

#include <ctime>
#include <utility>

namespace iso15118::d20::ev {

Context::Context(session::ev::feedback::Callbacks feedback_callbacks, session::SessionLogger& logger,
                 session::EvSessionConfig session_config_, const std::optional<ControlEvent>& current_control_event_,
                 d20::MessageExchange& message_exchange_, Timeouts& timeouts_) :
    feedback(std::move(feedback_callbacks)),
    log(logger),
    session_config(std::move(session_config_)),
    current_control_event{current_control_event_},
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

std::unique_ptr<message_20::Variant> Context::pull_response() {
    return message_exchange.pull_request();
}

message_20::Type Context::peek_response_type() const {
    return message_exchange.peek_request_type();
}

void Context::setup_header(message_20::Header& header) const {
    header.session_id = session_id;
    header.timestamp = static_cast<uint64_t>(std::time(nullptr));
}

} // namespace iso15118::d20::ev
