// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef TYPES_SAMPLE_TYPES_HPP
#define TYPES_SAMPLE_TYPES_HPP

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

#include <generated/types/units.hpp>

using nlohmann::json;

namespace types {
namespace sample {

enum class Mood {
    Happy,
    Sad,
    Indifferent,
};

/// \brief Converts the given Mood \p e to human readable string
/// \returns a string representation of the Mood
inline std::string mood_to_string(const Mood& e) {
    switch (e) {
    case Mood::Happy:
        return "Happy";
    case Mood::Sad:
        return "Sad";
    case Mood::Indifferent:
        return "Indifferent";
    }

    throw std::out_of_range("No known string conversion for provided enum of type Mood");
}

/// \brief Converts the given Mood \p e to human readable string
/// \returns a string_view representation of the Mood
inline constexpr std::string_view mood_to_string_view(const Mood& e) {
    switch (e) {
    case Mood::Happy:
        return "Happy";
    case Mood::Sad:
        return "Sad";
    case Mood::Indifferent:
        return "Indifferent";
    }

    throw std::out_of_range("No known string conversion for provided enum of type Mood");
}

/// \brief Converts the given std::string \p s to Mood
/// \returns a Mood from a string representation
inline Mood string_to_mood(const std::string& s) {
    if (s == "Happy") {
        return Mood::Happy;
    }
    if (s == "Sad") {
        return Mood::Sad;
    }
    if (s == "Indifferent") {
        return Mood::Indifferent;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type Mood");
}

/// \brief Writes the string representation of the given Mood \p mood to the given output stream \p os
/// \returns an output stream with the Mood written to
inline std::ostream& operator<<(std::ostream& os, const Mood& mood) {
    os << mood_to_string(mood);
    return os;
}

/// \brief Conversion from a given Mood \p e to a given json object \p j
inline void to_json(json& j, const Mood& e) {
    j = mood_to_string_view(e);
}

/// \brief Conversion from a given json object \p j to a given Mood \p e
inline void from_json(const json& j, Mood& e) {
    e = string_to_mood(j.get<std::string>());
}

enum class Kind {
    First,
    Second,
};

/// \brief Converts the given Kind \p e to human readable string
/// \returns a string representation of the Kind
inline std::string kind_to_string(const Kind& e) {
    switch (e) {
    case Kind::First:
        return "First";
    case Kind::Second:
        return "Second";
    }

    throw std::out_of_range("No known string conversion for provided enum of type Kind");
}

/// \brief Converts the given Kind \p e to human readable string
/// \returns a string_view representation of the Kind
inline constexpr std::string_view kind_to_string_view(const Kind& e) {
    switch (e) {
    case Kind::First:
        return "First";
    case Kind::Second:
        return "Second";
    }

    throw std::out_of_range("No known string conversion for provided enum of type Kind");
}

/// \brief Converts the given std::string \p s to Kind
/// \returns a Kind from a string representation
inline Kind string_to_kind(const std::string& s) {
    if (s == "First") {
        return Kind::First;
    }
    if (s == "Second") {
        return Kind::Second;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type Kind");
}

/// \brief Writes the string representation of the given Kind \p kind to the given output stream \p os
/// \returns an output stream with the Kind written to
inline std::ostream& operator<<(std::ostream& os, const Kind& kind) {
    os << kind_to_string(kind);
    return os;
}

/// \brief Conversion from a given Kind \p e to a given json object \p j
inline void to_json(json& j, const Kind& e) {
    j = kind_to_string_view(e);
}

/// \brief Conversion from a given json object \p j to a given Kind \p e
inline void from_json(const json& j, Kind& e) {
    e = string_to_kind(j.get<std::string>());
}

struct Reading {
    std::string taken_at; ///< When it was taken
    types::sample::Mood mood; ///< The mood at the time
    std::optional<types::units::Power> power; ///< Power at the time, in another unit
    std::optional<std::vector<types::sample::Mood>> history; ///< Earlier moods
    std::optional<std::string> label; ///< A free-form label
    std::optional<Kind> kind; ///< An enum written inline

    /// \brief Conversion from a given Reading \p k to a given json object \p j
    friend void to_json(json& j, const Reading& k) {
        // the required parts of the type
        j = json{
            {"taken_at", k.taken_at},
            {"mood", types::sample::mood_to_string_view(k.mood)},
        };

        // the optional parts of the type
        if (k.power) {
            j.emplace("power", *k.power);
        }
        if (k.history) {
            j.emplace("history", *k.history);
        }
        if (k.label) {
            j.emplace("label", *k.label);
        }
        if (k.kind) {
            j.emplace("kind", kind_to_string_view(*k.kind));
        }
    }

    /// \brief Conversion from a given json object \p j to a given Reading \p k
    friend void from_json(const json& j, Reading& k) {
        // the required parts of the type
        k.taken_at = j.at("taken_at");
        k.mood = types::sample::string_to_mood(j.at("mood"));

        // the optional parts of the type
        auto it = j.find("power");
        if (it != j.end()) {
            k.power = it->get<types::units::Power>();
        }
        it = j.find("history");
        if (it != j.end()) {
            k.history = j.at("history").get<std::vector<types::sample::Mood>>();
        }
        it = j.find("label");
        if (it != j.end()) {
            k.label = it->get<std::string>();
        }
        it = j.find("kind");
        if (it != j.end()) {
            k.kind = string_to_kind(it->get<std::string>());
        }
    }

    /// \brief Compares objects of type Reading for equality
    friend constexpr bool operator==(const Reading& k, const Reading& l) {
        const auto& lhs_tuple = std::tie(
            k.taken_at,
            k.mood,
            k.power,
            k.history,
            k.label,
            k.kind
        );
        const auto& rhs_tuple = std::tie(
            l.taken_at,
            l.mood,
            l.power,
            l.history,
            l.label,
            l.kind
        );
        return lhs_tuple == rhs_tuple;
    }

    /// \brief Compares objects of type Reading for inequality
    friend constexpr bool operator!=(const Reading& k, const Reading& l) {
        return not operator==(k, l);
    }

    /// \brief Writes the string representation of the given Reading \p k to the given output stream \p os
    /// \returns an output stream with the Reading written to
    friend std::ostream& operator<<(std::ostream& os, const Reading& k) {
        os << json(k).dump(4);
        return os;
    }
};

} // namespace sample
} // namespace types

#endif // TYPES_SAMPLE_TYPES_HPP
