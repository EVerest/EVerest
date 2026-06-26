// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <stdexcept>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>

namespace iso15118::ev::d20 {

std::optional<std::pair<std::vector<uint8_t>, io::v2gtp::PayloadType>> MessageExchange::take_request() {
    request_available = false;
    auto serializer = std::move(pending_serialize);
    pending_serialize = nullptr;

    if (not serializer) {
        // A precondition violation: the caller checks has_request() first. Surface it
        // as an error so it is not confused with an encode failure.
        logf_error("take_request called with no pending request");
        return std::nullopt;
    }

    try {
        const auto size = serializer(io::StreamOutputView{out_buffer.data(), out_buffer.size()});
        return std::make_pair(std::vector<uint8_t>(out_buffer.begin(), out_buffer.begin() + size), out_type);
    } catch (const std::exception& e) {
        logf_error("EV request encode failed (buffer overflow or encode error, buffer=%zu bytes): %s",
                   out_buffer.size(), e.what());
        return std::nullopt;
    }
}

void MessageExchange::set_response(std::unique_ptr<message_20::Variant> new_response) {
    if (response) {
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }
    response = std::move(new_response);
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

Context::Context(feedback::Callbacks feedback_callbacks, MessageExchange& message_exchange_, SessionLogger& logger,
                 message_20::datatypes::Identifier evcc_id_,
                 const std::optional<ControlEvent>& current_control_event_) :
    feedback(std::move(feedback_callbacks)),
    log(logger),
    message_exchange(message_exchange_),
    evcc_id(std::move(evcc_id_)),
    current_control_event(current_control_event_) {
}

std::unique_ptr<message_20::Variant> Context::pull_response() {
    return message_exchange.pull_response();
}

message_20::Type Context::peek_response_type() const {
    return message_exchange.peek_response_type();
}

} // namespace iso15118::ev::d20
