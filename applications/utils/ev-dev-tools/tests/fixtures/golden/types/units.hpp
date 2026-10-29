// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TYPES_UNITS_TYPES_HPP
#define TYPES_UNITS_TYPES_HPP

//
// AUTO GENERATED - DO NOT EDIT!
// template version 6
//

#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace types {
namespace units {

// no enums defined for units

struct Power {
    float total; ///< Sum value
    std::optional<float> L1; ///< Phase 1 only

    /// \brief Conversion from a given Power \p k to a given json object \p j
    friend void to_json(json& j, const Power& k) {
        // the required parts of the type
        j = json{
            {"total", k.total},
        };

        // the optional parts of the type
        if (k.L1) {
            j.emplace("L1", *k.L1);
        }
    }

    /// \brief Conversion from a given json object \p j to a given Power \p k
    friend void from_json(const json& j, Power& k) {
        // the required parts of the type
        k.total = j.at("total");

        // the optional parts of the type
        auto it = j.find("L1");
        if (it != j.end()) {
            k.L1 = it->get<float>();
        }
    }

    /// \brief Compares objects of type Power for equality
    friend constexpr bool operator==(const Power& k, const Power& l) {
        const auto& lhs_tuple = std::tie(
            k.total,
            k.L1
        );
        const auto& rhs_tuple = std::tie(
            l.total,
            l.L1
        );
        return lhs_tuple == rhs_tuple;
    }

    /// \brief Compares objects of type Power for inequality
    friend constexpr bool operator!=(const Power& k, const Power& l) {
        return not operator==(k, l);
    }

    /// \brief Writes the string representation of the given Power \p k to the given output stream \p os
    /// \returns an output stream with the Power written to
    friend std::ostream& operator<<(std::ostream& os, const Power& k) {
        os << json(k).dump(4);
        return os;
    }
};

} // namespace units
} // namespace types

#endif // TYPES_UNITS_TYPES_HPP
