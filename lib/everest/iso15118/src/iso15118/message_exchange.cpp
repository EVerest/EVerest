// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/message_exchange.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118 {

MessageExchange::MessageExchange(io::StreamOutputView output_) : response(std::move(output_)) {
}

void MessageExchange::set_input(std::unique_ptr<msg::d20::Variant> new_input) {
    if (input.has_value()) {
        // FIXME (aw): we might want to have a stack here?
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }

    input = std::make_optional<Variants>(std::move(new_input));
}

void MessageExchange::set_input(std::unique_ptr<msg::d2::Variant> new_input) {
    if (input.has_value()) {
        // FIXME (aw): we might want to have a stack here?
        throw std::runtime_error("Previous V2G message has not been handled yet");
    }

    input = std::make_optional<Variants>(std::move(new_input));
}

std::tuple<bool, size_t, io::v2gtp::PayloadType, ResponseTypes> MessageExchange::check_and_clear_response() {
    auto retval = std::make_tuple(response_available, response_size, payload_type, response_type);

    response_available = false;
    response_size = 0;
    response_type = msg::d20::Type::None;

    return retval;
}

template <> std::unique_ptr<msg::d20::Variant> MessageExchange::pull_input() {
    if (not input.has_value()) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }

    if (not std::holds_alternative<std::unique_ptr<msg::d20::Variant>>(input.value())) {
        throw std::runtime_error("Tried to access V2G message, but the false Variant is there");
    }

    return std::move(std::get<std::unique_ptr<msg::d20::Variant>>(input.value()));
}

template <> std::unique_ptr<msg::d2::Variant> MessageExchange::pull_input() {
    if (not input.has_value()) {
        throw std::runtime_error("Tried to access V2G message, but there is none");
    }

    if (not std::holds_alternative<std::unique_ptr<msg::d2::Variant>>(input.value())) {
        throw std::runtime_error("Tried to access V2G message, but the false Variant is there");
    }

    return std::move(std::get<std::unique_ptr<msg::d2::Variant>>(input.value()));
}

template <> msg::d20::Type MessageExchange::peek_input_type() const {
    if (not input.has_value()) {
        logf_warning("Tried to access V2G message, but there is none");
        return msg::d20::Type::None;
    }

    if (not std::holds_alternative<std::unique_ptr<msg::d20::Variant>>(input.value())) {
        logf_warning("Tried to access V2G message, but there is the false variant active");
        return msg::d20::Type::None;
    }
    return std::get<std::unique_ptr<msg::d20::Variant>>(input.value())->get_type();
}

template <> msg::d2::Type MessageExchange::peek_input_type() const {
    if (not input.has_value()) {
        logf_warning("Tried to access V2G message, but there is none");
        return msg::d2::Type::None;
    }

    if (not std::holds_alternative<std::unique_ptr<msg::d20::Variant>>(input.value())) {
        logf_warning("Tried to access V2G message, but there is the false variant active");
        return msg::d2::Type::None;
    }
    return std::get<std::unique_ptr<msg::d2::Variant>>(input.value())->get_type();
}

} // namespace iso15118
