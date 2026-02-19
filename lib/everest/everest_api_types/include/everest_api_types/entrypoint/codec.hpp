// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest_api_types/entrypoint/API.hpp>
#include <optional>

namespace everest::lib::API::V1_0::types::entrypoint {

std::string serialize(ApiParameter val) noexcept;
std::string serialize(ApiDiscoverResponse val) noexcept;
std::string serialize(ModuleAssociations val) noexcept;
std::string serialize(ModuleParameter val) noexcept;
std::string serialize(QueryEVerestConfigurationResponse val) noexcept;
std::string serialize(EVerestVersion val) noexcept;

std::ostream& operator<<(std::ostream& os, ApiParameter const& val);
std::ostream& operator<<(std::ostream& os, ApiDiscoverResponse const& val);
std::ostream& operator<<(std::ostream& os, ModuleAssociations const& val);
std::ostream& operator<<(std::ostream& os, ModuleParameter const& val);
std::ostream& operator<<(std::ostream& os, QueryEVerestConfigurationResponse const& val);
std::ostream& operator<<(std::ostream& os, EVerestVersion const& val);

template <class T> T deserialize(std::string const& val);
template <class T> std::optional<T> try_deserialize(std::string const& val) {
    try {
        return deserialize<T>(val);
    } catch (...) {
        return std::nullopt;
    }
}
template <class T> bool adl_deserialize(std::string const& json_data, T& obj) {
    auto opt = try_deserialize<T>(json_data);
    if (opt) {
        obj = opt.value();
        return true;
    }
    return false;
}

} // namespace everest::lib::API::V1_0::types::entrypoint
