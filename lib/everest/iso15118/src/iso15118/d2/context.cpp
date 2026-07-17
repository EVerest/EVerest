// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/context.hpp>

#include <cstring>
#include <stdexcept>
#include <utility>

#include <iso15118/detail/helper.hpp>

namespace iso15118::d2 {

Context::Context(session::feedback::Callbacks callbacks, session::SessionLogger& logger, d2::SessionConfig config,
                 std::optional<PauseContext>& pause_ctx_, const std::optional<d20::ControlEvent>& control_event,
                 MessageExchange& message_exchange_, d20::Timeouts& timeouts_) :
    feedback(std::move(callbacks)),
    log(logger),
    session_config(std::move(config)),
    pause_ctx(pause_ctx_),
    current_control_event(control_event),
    message_exchange(message_exchange_),
    timeouts(timeouts_) {
}

std::unique_ptr<message_2::Variant> Context::pull_request() {
    return message_exchange.pull_request();
}

message_2::Type Context::peek_request_type() const {
    return message_exchange.peek_request_type();
}

void Context::setup_header(message_2::Header& header) const {
    header.session_id = session_id;
}

} // namespace iso15118::d2

namespace iso15118::d2 {

MessageExchange::MessageExchange(io::StreamOutputView output_) : response(std::move(output_)) {
}

void MessageExchange::set_request(std::unique_ptr<message_2::Variant> new_request) {
    if (request) {
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }
    request = std::move(new_request);
}

std::unique_ptr<message_2::Variant> MessageExchange::pull_request() {
    if (not request) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }
    return std::move(request);
}

void MessageExchange::set_raw_response(const uint8_t* data, size_t len) {
    if (data == nullptr or len == 0) {
        logf_error("Refusing to stage an empty raw relay response");
        return;
    }
    if (len > response.payload_len) {
        logf_error("Raw relay response (%zu bytes) exceeds the output buffer (%zu bytes)", len,
                   response.payload_len);
        return;
    }
    std::memcpy(response.payload, data, len);
    response_size = len;
    response_available = true;
    payload_type = io::v2gtp::PayloadType::SAP;
    response_type = message_2::Type::None;
}

std::tuple<bool, size_t, io::v2gtp::PayloadType, message_2::Type> MessageExchange::check_and_clear_response() {
    auto retval = std::make_tuple(response_available, response_size, payload_type, response_type);

    response_available = false;
    response_size = 0;
    response_type = message_2::Type::None;

    return retval;
}

message_2::Type MessageExchange::peek_request_type() const {
    if (not request) {
        logf_warning("Tried to access V2G message, but there is none");
        return message_2::Type::None;
    }
    return request->get_type();
}

} // namespace iso15118::d2
