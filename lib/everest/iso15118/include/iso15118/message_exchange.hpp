// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <any>
#include <optional>
#include <variant>

#include <iso15118/message/d2/variant.hpp>
#include <iso15118/message/d20/payload_type.hpp>
#include <iso15118/message/d20/variant.hpp>

namespace iso15118 {

using Variants = std::variant<std::unique_ptr<msg::d20::Variant>, std::unique_ptr<msg::d2::Variant>>;
using ResponseTypes = std::variant<msg::d20::Type, msg::d2::Type>;

class MessageExchange {
public:
    explicit MessageExchange(io::StreamOutputView);

    void set_input(std::unique_ptr<msg::d20::Variant> new_input);
    void set_input(std::unique_ptr<msg::d2::Variant> new_input);
    template <typename Variant> std::unique_ptr<Variant> pull_input();
    template <typename Type> Type peek_input_type() const;

    template <typename MessageType> void set_d20_response(const MessageType& msg) {
        response_size = msg::d20::serialize(msg, response);
        response_available = true;
        payload_type = msg::d20::PayloadTypeTrait<MessageType>::type;

        const auto res_type = msg::d20::TypeTrait<MessageType>::type;
        response_type = res_type;

        response_message = msg;
    }

    template <typename MessageType> void set_d2_response(const MessageType& msg) {
        response_size = msg::d2::serialize(msg, response);
        response_available = true;
        payload_type = io::v2gtp::PayloadType::SAP;

        const auto res_type = msg::d2::TypeTrait<MessageType>::type;
        response_type = res_type;

        response_message = msg;
    }
    // -----------------
    template <typename Msg> std::optional<Msg> get_d20_response() {
        static_assert(msg::d20::TypeTrait<Msg>::type != msg::d20::Type::None, "Unhandled type!");

        if (not std::holds_alternative<msg::d20::Type>(response_type)) {
            return std::nullopt;
        }

        if (msg::d20::TypeTrait<Msg>::type != std::get<msg::d20::Type>(response_type)) {
            return std::nullopt;
        }
        try {
            return std::any_cast<Msg>(response_message);
        } catch (const std::bad_any_cast& ex) {
            return std::nullopt;
        }
    }

    std::tuple<bool, size_t, io::v2gtp::PayloadType, ResponseTypes> check_and_clear_response();
    bool has_response() const {
        return response_available;
    }

private:
    // input
    std::optional<Variants> input;

    // output
    const io::StreamOutputView response;
    size_t response_size{0};
    bool response_available{false};
    io::v2gtp::PayloadType payload_type;
    ResponseTypes response_type;
    std::any response_message;
};

} // namespace iso15118
