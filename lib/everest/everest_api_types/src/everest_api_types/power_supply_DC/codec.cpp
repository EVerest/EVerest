// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "power_supply_DC/codec.hpp"
#include "nlohmann/json.hpp"
#include "power_supply_DC/API.hpp"
#include "power_supply_DC/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::power_supply_DC {

std::string serialize(Capabilities const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Mode val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ChargingPhase val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModeRequest val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(VoltageCurrent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

template <> Capabilities deserialize(std::string_view val) {
    return utilities::parse_json<Capabilities>(val);
}

template <> Mode deserialize(std::string_view val) {
    return utilities::parse_json<Mode>(val);
}

template <> ChargingPhase deserialize(std::string_view val) {
    return utilities::parse_json<ChargingPhase>(val);
}

template <> ModeRequest deserialize(std::string_view val) {
    return utilities::parse_json<ModeRequest>(val);
}

template <> VoltageCurrent deserialize(std::string_view val) {
    return utilities::parse_json<VoltageCurrent>(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

std::ostream& operator<<(std::ostream& os, const Capabilities& k) {
    os << serialize(k);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Mode& k) {
    os << serialize(k);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ChargingPhase& k) {
    os << serialize(k);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ModeRequest& k) {
    os << serialize(k);
    return os;
}

std::ostream& operator<<(std::ostream& os, const VoltageCurrent& k) {
    os << serialize(k);
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

} // namespace everest::lib::API::V1_0::types::power_supply_DC
