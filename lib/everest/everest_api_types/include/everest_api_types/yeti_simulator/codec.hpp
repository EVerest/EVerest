// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "API.hpp"
#include <iostream>
#include <optional>
#include <string>
#include <typeinfo>

namespace everest::lib::API::V1_0::types::yeti_simulator {

std::string serialize(Severity val);

std::string serialize(RaiseError const& val) noexcept;
std::string serialize(ClearError const& val) noexcept;

std::ostream& operator<<(std::ostream& os, Severity const& val);

std::ostream& operator<<(std::ostream& os, RaiseError const& val);
std::ostream& operator<<(std::ostream& os, ClearError const& val);

template <class T> T deserialize(std::string const& val);
// On parse failure, write a single-line diagnostic to stderr so the failure
// is observable in module logs. EVLOG_* is unavailable in this library; the
// stderr stream is captured by the framework logging sink at module level.
template <class T> std::optional<T> try_deserialize(std::string const& val) {
    try {
        return deserialize<T>(val);
    } catch (const std::exception& e) {
        std::cerr << "yeti_simulator::try_deserialize<" << typeid(T).name() << "> failed: " << e.what()
                  << " payload=" << val << std::endl;
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

} // namespace everest::lib::API::V1_0::types::yeti_simulator
