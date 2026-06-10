// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "evse_board_support/codec.hpp"
#include "evse_board_support/API.hpp"
#include "evse_board_support/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::evse_board_support {

std::string serialize(Event val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(BspEvent const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ErrorEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Error const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(HardwareCapabilities const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Reason val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(PowerOnOff const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Ampacity val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ProximityPilot const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, Event const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const BspEvent& val) {
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

std::ostream& operator<<(std::ostream& os, const HardwareCapabilities& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Reason& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const PowerOnOff& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const Ampacity& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, const ProximityPilot& val) {
    os << serialize(val);
    return os;
}

template <> Event deserialize(std::string_view val) {
    return utilities::parse_json<Event>(val);
}

template <> BspEvent deserialize(std::string_view val) {
    return utilities::parse_json<BspEvent>(val);
}

template <> ErrorEnum deserialize(std::string_view val) {
    return utilities::parse_json<ErrorEnum>(val);
}

template <> Error deserialize(std::string_view val) {
    return utilities::parse_json<Error>(val);
}

template <> HardwareCapabilities deserialize(std::string_view val) {
    return utilities::parse_json<HardwareCapabilities>(val);
}

template <> Reason deserialize(std::string_view val) {
    return utilities::parse_json<Reason>(val);
}

template <> PowerOnOff deserialize(std::string_view val) {
    return utilities::parse_json<PowerOnOff>(val);
}

template <> Ampacity deserialize(std::string_view val) {
    return utilities::parse_json<Ampacity>(val);
}

template <> ProximityPilot deserialize(std::string_view val) {
    return utilities::parse_json<ProximityPilot>(val);
}

} // namespace everest::lib::API::V1_0::types::evse_board_support
