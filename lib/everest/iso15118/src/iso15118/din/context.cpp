// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/context.hpp>

#include <stdexcept>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::din {

MessageExchange::MessageExchange(io::StreamOutputView output_) : response(std::move(output_)) {
}

void MessageExchange::set_request(std::unique_ptr<message_din::Variant> new_request) {
    if (request) {
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }
    request = std::move(new_request);
}

std::unique_ptr<message_din::Variant> MessageExchange::pull_request() {
    if (not request) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }
    return std::move(request);
}

std::tuple<bool, size_t, io::v2gtp::PayloadType, message_din::Type> MessageExchange::check_and_clear_response() {
    auto retval = std::make_tuple(response_available, response_size, payload_type, response_type);

    response_available = false;
    response_size = 0;
    response_type = message_din::Type::None;

    return retval;
}

message_din::Type MessageExchange::peek_request_type() const {
    if (not request) {
        logf_warning("Tried to access V2G message, but there is none");
        return message_din::Type::None;
    }
    return request->get_type();
}

Context::Context(session::feedback::Callbacks feedback_callbacks, session::SessionLogger& logger,
                 SessionConfig session_config_, const std::optional<d20::ControlEvent>& current_control_event_,
                 MessageExchange& message_exchange_, d20::Timeouts& timeouts_) :
    feedback(std::move(feedback_callbacks)),
    log(logger),
    session_config(std::move(session_config_)),
    current_control_event{current_control_event_},
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

std::unique_ptr<message_din::Variant> Context::pull_request() {
    return message_exchange.pull_request();
}

message_din::Type Context::peek_request_type() const {
    return message_exchange.peek_request_type();
}

void Context::setup_header(message_din::Header& header) const {
    header.session_id = session_id;
}

} // namespace iso15118::din
