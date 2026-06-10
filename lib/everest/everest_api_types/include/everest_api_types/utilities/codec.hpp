// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <string>
#include <string_view>

namespace everest::lib::API {

template <class T> bool deserialize(std::string_view json_data, T& obj) {
    return adl_deserialize(json_data, obj);
}

template <> bool deserialize(std::string_view data, bool& obj);
template <> bool deserialize(std::string_view data, int& obj);
template <> bool deserialize(std::string_view data, size_t& obj);
template <> bool deserialize(std::string_view data, double& obj);
template <> bool deserialize(std::string_view data, float& obj);
template <> bool deserialize(std::string_view data, std::string& obj);

} // namespace everest::lib::API
