// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20 {

MessageExchange::MessageExchange(io::StreamOutputView output_) : request(std::move(output_)) {
}

void MessageExchange::set_response(std::unique_ptr<message_20::Variant> new_request) {
    if (response) {
        // FIXME (aw): we might want to have a stack here?
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }

    response = std::move(new_request);
}

std::unique_ptr<message_20::Variant> MessageExchange::pull_response() {
    if (not response) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }

    return std::move(response);
}

message_20::Type MessageExchange::peek_response_type() const {
    if (not response) {
        logf_warning("Tried to access V2G message, but there is none");
        return message_20::Type::None;
    }
    return response->get_type();
}

Context::Context(session::feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_) :
    feedback(std::move(feedback_callbacks)), message_exchange(message_exchange_) {
}

std::unique_ptr<message_20::Variant> Context::pull_response() {
    return message_exchange.pull_response();
}

message_20::Type Context::peek_response_type() const {
    return message_exchange.peek_response_type();
}

} // namespace iso15118::ev::d20
