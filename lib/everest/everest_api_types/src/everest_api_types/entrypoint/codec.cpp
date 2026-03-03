// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "entrypoint/codec.hpp"
#include "entrypoint/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"

#include <iostream>

namespace everest::lib::API::V1_0::types::entrypoint {

std::string serialize(ApiParameter val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(ApiDiscoverResponse val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(ModuleAssociations val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(ModuleParameter val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(QueryEVerestConfigurationResponse val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(EVerestVersion val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::string serialize(CommunicationParameters val) noexcept {
    json result = val;
    return result.dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, ApiParameter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ApiDiscoverResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleAssociations const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleParameter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, QueryEVerestConfigurationResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, EVerestVersion const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, CommunicationParameters const& val) {
    os << serialize(val);
    return os;
}

template <> ApiParameter deserialize(std::string const& val) {
    auto data = json::parse(val);
    ApiParameter obj = data;
    return obj;
}

template <> ApiDiscoverResponse deserialize(std::string const& val) {
    auto data = json::parse(val);
    ApiDiscoverResponse obj = data;
    return obj;
}

template <> ModuleAssociations deserialize(std::string const& val) {
    auto data = json::parse(val);
    ModuleAssociations obj = data;
    return obj;
}

template <> ModuleParameter deserialize(std::string const& val) {
    auto data = json::parse(val);
    ModuleParameter obj = data;
    return obj;
}

template <> QueryEVerestConfigurationResponse deserialize(std::string const& val) {
    auto data = json::parse(val);
    QueryEVerestConfigurationResponse obj = data;
    return obj;
}

template <> EVerestVersion deserialize(std::string const& val) {
    auto data = json::parse(val);
    EVerestVersion obj = data;
    return obj;
}

template <> CommunicationParameters deserialize(std::string const& val) {
    auto data = json::parse(val);
    CommunicationParameters obj = data;
    return obj;
}

} // namespace everest::lib::API::V1_0::types::entrypoint
