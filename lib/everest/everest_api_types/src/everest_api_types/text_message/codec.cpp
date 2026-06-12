// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "text_message/codec.hpp"
#include "nlohmann/json.hpp"
#include "text_message/API.hpp"
#include "text_message/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::text_message {

std::string serialize(MessageFormat val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(MessageContent const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, MessageFormat const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, MessageContent const& val) {
    os << serialize(val);
    return os;
}

template <> MessageFormat deserialize(std::string_view val) {
    return utilities::parse_json<MessageFormat>(val);
}

template <> MessageContent deserialize(std::string_view val) {
    return utilities::parse_json<MessageContent>(val);
}

} // namespace everest::lib::API::V1_0::types::text_message
