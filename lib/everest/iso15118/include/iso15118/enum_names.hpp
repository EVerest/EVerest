// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace iso15118 {

// An exhaustive switch with no default is the only name table; these helpers derive from it by
// scanning the underlying values 0..Count-1. An empty name means the value is not an enumerator.

template <typename Enum, std::size_t Count, typename NameOf, typename Visit>
constexpr void for_each_enum_value(NameOf name_of, Visit visit) {
    for (std::size_t value = 0; value < Count; ++value) {
        const auto candidate = static_cast<Enum>(value);
        if (not name_of(candidate).empty()) {
            visit(candidate);
        }
    }
}

template <typename Enum, std::size_t Count, typename NameOf>
constexpr std::optional<Enum> enum_from_name(std::string_view name, NameOf name_of) {
    if (name.empty()) {
        return std::nullopt;
    }
    for (std::size_t value = 0; value < Count; ++value) {
        const auto candidate = static_cast<Enum>(value);
        if (name_of(candidate) == name) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace iso15118
