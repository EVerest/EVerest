// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "generic/string.hpp"
#include "nlohmann/json.hpp"
#include "utilities/json_codec_helpers.hpp"

namespace everest::lib::API::V1_0::types::generic {

std::string trimmed(std::string const& str) {
    auto length = str.length();
    auto offset = length >= 1 and str[0] == '"' ? 1 : 0;
    auto count = (length >= 2 and str[length - 1] == '"' ? length - 1 : length) - offset;
    return str.substr(offset, count);
}

std::optional<std::string> compress_json(std::string data) {
    try {
        return utilities::parse_json<nlohmann::json>(data).dump();
    } catch (...) {
    }
    return std::nullopt;
}

} // namespace everest::lib::API::V1_0::types::generic
