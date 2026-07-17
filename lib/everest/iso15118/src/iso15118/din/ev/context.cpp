// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/ev/context.hpp>

#include <utility>

namespace iso15118::din::ev {

Context::Context(session::ev::feedback::Callbacks feedback_callbacks, session::SessionLogger& logger,
                 EvSessionConfig session_config_, const std::optional<ControlEvent>& current_control_event_,
                 din::MessageExchange& message_exchange_, d20::Timeouts& timeouts_) :
    feedback(std::move(feedback_callbacks)),
    log(logger),
    session_config(std::move(session_config_)),
    current_control_event{current_control_event_},
    message_exchange(message_exchange_),
    timeouts(timeouts_) {

    // DIN SPEC 70121 has no charging pause/resume [V2G-DC-241]: the EVCC always starts a new session with
    // SessionID=0, so a resumed_session_id (if any leaked in from the shared config) is deliberately
    // ignored here. session_id stays at its all-zero default.
}

std::unique_ptr<message_din::Variant> Context::pull_response() {
    return message_exchange.pull_request();
}

message_din::Type Context::peek_response_type() const {
    return message_exchange.peek_request_type();
}

void Context::setup_header(message_din::Header& header) const {
    header.session_id = session_id;
}

} // namespace iso15118::din::ev
