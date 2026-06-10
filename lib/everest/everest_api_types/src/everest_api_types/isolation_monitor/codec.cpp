// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "isolation_monitor/codec.hpp"
#include "isolation_monitor/API.hpp"
#include "isolation_monitor/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::isolation_monitor {

std::string serialize(IsolationMeasurement const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, IsolationMeasurement const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ErrorEnum& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Error& val) {
    os << serialize(val);
    return os;
}

template <> IsolationMeasurement deserialize(std::string_view val) {
    return utilities::parse_json<IsolationMeasurement>(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

} // namespace everest::lib::API::V1_0::types::isolation_monitor
