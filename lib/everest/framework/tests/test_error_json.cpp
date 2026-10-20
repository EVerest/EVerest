// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <utils/error.hpp>
#include <utils/error/error_json.hpp>

namespace {

Everest::error::Error make_error() {
    return {"evse_board_support/MREC3HighTemperature",
            "SubType",
            "a message",
            "a description",
            ImplementationIdentifier("bsp_1", "main", Mapping(1, 2)),
            "com.example",
            Everest::error::Severity::High,
            Everest::Date::from_rfc3339("2026-10-13T10:00:00.000Z"),
            Everest::error::UUID("the-uuid"),
            Everest::error::State::Active};
}

} // namespace

SCENARIO("Serialize the caused by value of an error") {
    GIVEN("An error without a caused by value") {
        const auto error = make_error();

        THEN("The key is left out entirely") {
            const nlohmann::json j(error);
            CHECK_FALSE(j.contains("caused_by_value"));
        }

        THEN("It survives a round trip") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            CHECK_FALSE(result.caused_by_value.has_value());
        }
    }

    GIVEN("An error with a caused by value") {
        auto error = make_error();
        error.caused_by_value = Everest::error::CausedByValue{"85.3 degC"};

        THEN("The value is nested under the attribute") {
            const nlohmann::json j(error);
            REQUIRE(j.contains("caused_by_value"));
            CHECK(j.at("caused_by_value").at("actual_value") == "85.3 degC");
        }

        THEN("It survives a round trip") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            REQUIRE(result.caused_by_value.has_value());
            CHECK(result.caused_by_value.value().actual_value == "85.3 degC");
        }
    }

    GIVEN("An error object written before the attribute existed") {
        nlohmann::json j(make_error());
        j.erase("caused_by_value");

        THEN("It is read without a caused by value") {
            const auto result = j.get<Everest::error::Error>();
            CHECK_FALSE(result.caused_by_value.has_value());
            CHECK(result.type == "evse_board_support/MREC3HighTemperature");
        }
    }

    GIVEN("An empty actual value") {
        auto error = make_error();
        error.caused_by_value = Everest::error::CausedByValue{""};

        THEN("The attribute is still reported, so an empty value differs from no value") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            REQUIRE(result.caused_by_value.has_value());
            CHECK(result.caused_by_value.value().actual_value.empty());
        }
    }
}
