// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "ev_board_support/codec.hpp"
#include "ev_board_support/API.hpp"
#include "ev_board_support/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::ev_board_support {

std::string serialize(EvCpState val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(BspMeasurement const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, EvCpState const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const BspMeasurement& val) {
    os << serialize(val);
    return os;
}

template <> EvCpState deserialize(std::string_view val) {
    return utilities::parse_json<EvCpState>(val);
}

template <> BspMeasurement deserialize(std::string_view val) {
    return utilities::parse_json<BspMeasurement>(val);
}

} // namespace everest::lib::API::V1_0::types::ev_board_support
