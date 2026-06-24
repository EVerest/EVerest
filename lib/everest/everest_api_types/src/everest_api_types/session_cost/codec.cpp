// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "session_cost/codec.hpp"
#include "nlohmann/json.hpp"
#include "session_cost/API.hpp"
#include "session_cost/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::session_cost {

std::string serialize(DefaultPrice val) noexcept {
    json result = val;
    return result.dump(json_indent);
}
std::string serialize(TariffMessage val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(IdlePrice val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(CostCategory val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(ChargingPriceComponent val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(NextPeriodPrice val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(SessionCostChunk val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(SessionStatus val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(SessionCost val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, DefaultPrice const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, TariffMessage const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, IdlePrice const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, CostCategory const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, ChargingPriceComponent const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, NextPeriodPrice const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, SessionCostChunk const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, SessionStatus const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, SessionCost const& val) {
    os << serialize(val);
    return os;
}

template <> DefaultPrice deserialize(std::string_view val) {
    auto data = json::parse(val.begin(), val.end());
    DefaultPrice obj = data;
    return obj;
}

template <> TariffMessage deserialize(std::string_view val) {
    return utilities::parse_json<TariffMessage>(val);
}

template <> IdlePrice deserialize(std::string_view val) {
    return utilities::parse_json<IdlePrice>(val);
}
template <> CostCategory deserialize(std::string_view val) {
    return utilities::parse_json<CostCategory>(val);
}
template <> ChargingPriceComponent deserialize(std::string_view val) {
    return utilities::parse_json<ChargingPriceComponent>(val);
}

template <> NextPeriodPrice deserialize(std::string_view val) {
    return utilities::parse_json<NextPeriodPrice>(val);
}

template <> SessionCostChunk deserialize(std::string_view val) {
    return utilities::parse_json<SessionCostChunk>(val);
}
template <> SessionStatus deserialize(std::string_view val) {
    return utilities::parse_json<SessionStatus>(val);
}
template <> SessionCost deserialize(std::string_view val) {
    return utilities::parse_json<SessionCost>(val);
}

} // namespace everest::lib::API::V1_0::types::session_cost
