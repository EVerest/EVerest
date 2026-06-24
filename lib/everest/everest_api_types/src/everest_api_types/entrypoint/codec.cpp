// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "entrypoint/codec.hpp"
#include "entrypoint/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"

#include <iostream>

namespace everest::lib::API::V1_0::types::entrypoint {

std::string serialize(ApiParameter val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ApiDiscoverResponse val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(CommunicationParameters val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, ApiParameter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ApiDiscoverResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommunicationParameters const& val) {
    os << serialize(val);
    return os;
}

template <> ApiParameter deserialize(std::string_view val) {
    return utilities::parse_json<ApiParameter>(val);
}

template <> ApiDiscoverResponse deserialize(std::string_view val) {
    return utilities::parse_json<ApiDiscoverResponse>(val);
}

template <> CommunicationParameters deserialize(std::string_view val) {
    return utilities::parse_json<CommunicationParameters>(val);
}

} // namespace everest::lib::API::V1_0::types::entrypoint
