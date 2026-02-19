// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once
#include <everest_api_types/entrypoint/API.hpp>

namespace everest::lib::API::V1_0::types::entrypoint {

std::string serialize(ApiTypeEnum val) noexcept;
std::string serialize(ApiParameter val) noexcept;
std::string serialize(ApiDiscoverResponse val) noexcept;

std::ostream& operator<<(std::ostream& os, ApiTypeEnum const& val);
std::ostream& operator<<(std::ostream& os, ApiParameter const& val);
std::ostream& operator<<(std::ostream& os, ApiDiscoverResponse const& val);

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
