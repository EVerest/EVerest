// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "charger_information/codec.hpp"
#include "charger_information/API.hpp"
#include "charger_information/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <iostream>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::charger_information {

std::string serialize(ChargerInformation const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, ChargerInformation const& val) {
    os << serialize(val);
    return os;
}

template <> ChargerInformation deserialize(std::string_view val) {
    return utilities::parse_json<ChargerInformation>(val);
}

} // namespace everest::lib::API::V1_0::types::charger_information
