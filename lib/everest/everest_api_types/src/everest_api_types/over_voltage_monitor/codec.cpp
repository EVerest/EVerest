// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "over_voltage_monitor/codec.hpp"
#include "nlohmann/json.hpp"
#include "over_voltage_monitor/API.hpp"
#include "over_voltage_monitor/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::over_voltage_monitor {

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorSeverityEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OverVoltageLimits const& val) noexcept {
    return utilities::dump_json(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> ErrorSeverityEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorSeverityEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

template <> OverVoltageLimits deserialize(std::string_view val) {
    return utilities::parse_json<OverVoltageLimits>(val);
}

std::ostream& operator<<(std::ostream& os, const ErrorEnum& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ErrorSeverityEnum& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Error& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const OverVoltageLimits& val) {
    os << serialize(val);
    return os;
}

} // namespace everest::lib::API::V1_0::types::over_voltage_monitor
