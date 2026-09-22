// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string_view>

#include <iso15118/enum_names.hpp>

using namespace iso15118;

namespace {

// Underlying value 1 is not an enumerator.
enum class Colour : std::uint8_t {
    Red = 0,
    Green = 2,
    Blue = 3,
};
constexpr std::size_t COLOUR_COUNT = 4;

constexpr std::string_view colour_name(Colour colour) {
    switch (colour) {
    case Colour::Red:
        return "Red";
    case Colour::Green:
        return "Green";
    case Colour::Blue:
        return "Blue";
    }
    return {};
}

struct Visited {
    std::array<Colour, COLOUR_COUNT> values{};
    std::size_t count{0};
};

constexpr Visited visit_colours() {
    Visited visited;
    for_each_enum_value<Colour, COLOUR_COUNT>(colour_name, [&visited](Colour colour) {
        visited.values.at(visited.count) = colour;
        ++visited.count;
    });
    return visited;
}

constexpr std::string_view blue_also_green(Colour colour) {
    return colour == Colour::Blue ? std::string_view{"Green"} : colour_name(colour);
}

constexpr bool round_trips() {
    for (const auto colour : {Colour::Red, Colour::Green, Colour::Blue}) {
        if (enum_from_name<Colour, COLOUR_COUNT>(colour_name(colour), colour_name) != colour) {
            return false;
        }
    }
    return true;
}

} // namespace

SCENARIO("Enum lookups derive from the switch that names the enum") {
    constexpr auto visited = visit_colours();

    THEN("Iteration skips a value with an empty name") {
        STATIC_REQUIRE(visited.count == 3);
        STATIC_REQUIRE(visited.values.at(0) == Colour::Red);
        STATIC_REQUIRE(visited.values.at(1) == Colour::Green);
        STATIC_REQUIRE(visited.values.at(2) == Colour::Blue);
    }

    THEN("An empty name is rejected before the scan reaches the unnamed value") {
        STATIC_REQUIRE_FALSE(enum_from_name<Colour, COLOUR_COUNT>("", colour_name).has_value());
    }

    THEN("An unknown name is rejected") {
        STATIC_REQUIRE_FALSE(enum_from_name<Colour, COLOUR_COUNT>("Purple", colour_name).has_value());
    }

    THEN("The first value with a matching name wins") {
        STATIC_REQUIRE(enum_from_name<Colour, COLOUR_COUNT>("Green", blue_also_green) == Colour::Green);
    }

    THEN("Every enumerator round-trips through its name") {
        STATIC_REQUIRE(round_trips());
    }
}
