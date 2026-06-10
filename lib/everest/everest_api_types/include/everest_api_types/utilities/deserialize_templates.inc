// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Include this fragment inside an everest_api_types type namespace.

template <class T> T deserialize(std::string_view val);

template <class T> std::optional<T> try_deserialize(std::string_view val) noexcept {
    try {
        return deserialize<T>(val);
    } catch (...) {
        return std::nullopt;
    }
}

template <class T> bool adl_deserialize(std::string_view json_data, T& obj) noexcept {
    try {
        obj = deserialize<T>(json_data);
        return true;
    } catch (...) {
        return false;
    }
}
