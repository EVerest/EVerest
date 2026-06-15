// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "money/codec.hpp"
#include "money/API.hpp"
#include "money/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::money {

std::string serialize(CurrencyCode val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(Currency val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(MoneyAmount val) noexcept {
    return utilities::dump_json(val);
}
std::string serialize(Price val) noexcept {
    return utilities::dump_json(val);
}
std::ostream& operator<<(std::ostream& os, CurrencyCode const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, Currency const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, MoneyAmount const& val) {
    os << serialize(val);
    return os;
}
std::ostream& operator<<(std::ostream& os, Price const& val) {
    os << serialize(val);
    return os;
}

template <> CurrencyCode deserialize(std::string_view val) {
    return utilities::parse_json<CurrencyCode>(val);
}

template <> Currency deserialize(std::string_view val) {
    return utilities::parse_json<Currency>(val);
}

template <> MoneyAmount deserialize(std::string_view val) {
    return utilities::parse_json<MoneyAmount>(val);
}
template <> Price deserialize(std::string_view val) {
    return utilities::parse_json<Price>(val);
}

} // namespace everest::lib::API::V1_0::types::money
