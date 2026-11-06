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

SCENARIO("Serialize the trigger value of an error") {
    GIVEN("An error without a trigger value") {
        const auto error = make_error();

        THEN("The key is left out entirely") {
            const nlohmann::json j(error);
            CHECK_FALSE(j.contains("trigger_value"));
        }

        THEN("It survives a round trip") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            CHECK_FALSE(result.trigger_value.has_value());
        }
    }

    GIVEN("An error with a trigger value") {
        auto error = make_error();
        error.trigger_value = Everest::error::TriggerValue{"85.3 degC"};

        THEN("The value is nested under the attribute") {
            const nlohmann::json j(error);
            REQUIRE(j.contains("trigger_value"));
            CHECK(j.at("trigger_value").at("text_repr") == "85.3 degC");
        }

        THEN("It survives a round trip") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            REQUIRE(result.trigger_value.has_value());
            CHECK(result.trigger_value.value().text_repr == "85.3 degC");
        }
    }

    GIVEN("An error object written before the attribute existed") {
        nlohmann::json j(make_error());
        j.erase("trigger_value");

        THEN("It is read without a trigger value") {
            const auto result = j.get<Everest::error::Error>();
            CHECK_FALSE(result.trigger_value.has_value());
            CHECK(result.type == "evse_board_support/MREC3HighTemperature");
        }
    }

    GIVEN("A trigger value without a text representation") {
        nlohmann::json j(make_error());
        j["trigger_value"] = nlohmann::json::object();

        THEN("It is read without a trigger value") {
            const auto result = j.get<Everest::error::Error>();
            CHECK_FALSE(result.trigger_value.has_value());
        }
    }

    GIVEN("An empty text representation") {
        auto error = make_error();
        error.trigger_value = Everest::error::TriggerValue{""};

        THEN("The attribute is still reported, so an empty value differs from no value") {
            const auto result = nlohmann::json(error).get<Everest::error::Error>();
            REQUIRE(result.trigger_value.has_value());
            CHECK(result.trigger_value.value().text_repr.empty());
        }
    }
}
