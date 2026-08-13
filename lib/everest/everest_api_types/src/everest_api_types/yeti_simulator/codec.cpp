// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "yeti_simulator/codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "yeti_simulator/API.hpp"
#include "yeti_simulator/json_codec.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::yeti_simulator {

std::string serialize(Severity val) {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(RaiseError const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ClearError const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, Severity const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, RaiseError const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ClearError const& val) {
    os << serialize(val);
    return os;
}

template <> Severity deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> RaiseError deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

template <> ClearError deserialize(std::string_view val) {
    return json::parse(val.begin(), val.end());
}

} // namespace everest::lib::API::V1_0::types::yeti_simulator
