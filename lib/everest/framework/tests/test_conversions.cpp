// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <chrono>
#include <iostream>

#include <utils/conversions.hpp>
#include <utils/date.hpp>
#include <utils/types.hpp>

SCENARIO("Check conversions", "[!throws]") {
    GIVEN("Valid CmdErrors") {
        THEN("It shouldn't throw") {
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::MessageParsingError) ==
                  "MessageParsingError");

            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::SchemaValidationError) ==
                  "SchemaValidationError");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::HandlerException) ==
                  "HandlerException");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::CmdTimeout) == "CmdTimeout");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::Shutdown) == "Shutdown");
            CHECK(Everest::conversions::cmd_error_type_to_string(Everest::CmdErrorType::NotReady) == "NotReady");
        }
    }

    GIVEN("Invalid CmdErrors") {
        THEN("It should throw") {
            CHECK_THROWS(Everest::conversions::cmd_error_type_to_string(static_cast<Everest::CmdErrorType>(-1)));
        }
    }

    GIVEN("Valid CmdError strings") {
        THEN("It shouldn't throw") {
            CHECK(Everest::conversions::string_to_cmd_error_type("MessageParsingError") ==
                  Everest::CmdErrorType::MessageParsingError);

            CHECK(Everest::conversions::string_to_cmd_error_type("SchemaValidationError") ==
                  Everest::CmdErrorType::SchemaValidationError);
            CHECK(Everest::conversions::string_to_cmd_error_type("HandlerException") ==
                  Everest::CmdErrorType::HandlerException);
            CHECK(Everest::conversions::string_to_cmd_error_type("CmdTimeout") == Everest::CmdErrorType::CmdTimeout);
            CHECK(Everest::conversions::string_to_cmd_error_type("Shutdown") == Everest::CmdErrorType::Shutdown);
            CHECK(Everest::conversions::string_to_cmd_error_type("NotReady") == Everest::CmdErrorType::NotReady);
        }
    }

    GIVEN("Invalid CmdError strings") {
        THEN("It should throw") {
            CHECK_THROWS(Everest::conversions::string_to_cmd_error_type("ThisIsAnInvalidCmdErrorString"));
        }
    }

    GIVEN("Valid CmdErrorError") {
        THEN("It shouldn't throw") {
            Everest::CmdResultError cmd_result_error = {Everest::CmdErrorType::Shutdown, "message", nullptr};
            json cmd_result_error_json = {{Everest::conversions::ERROR_TYPE, "Shutdown"},
                                          {Everest::conversions::ERROR_MSG, "message"}};
            Everest::CmdResultError cmd_result_error_from_json = cmd_result_error_json;
            CHECK(json(cmd_result_error) == cmd_result_error_json);
            CHECK(json(cmd_result_error_from_json) == cmd_result_error_json);
        }
    }

    GIVEN("Valid timestamp") {
        THEN("It should parse") {
            const auto now_utc = date::utc_clock::now();
            const auto now_str = Everest::Date::to_rfc3339(now_utc);
            const auto tp_from_slow = Everest::Date::from_rfc3339_slow(now_str);
            const auto tp_from_fast = Everest::Date::from_rfc3339(now_str);
            CHECK(tp_from_slow == tp_from_fast);
        }
    }

    GIVEN("ModuleTierMappings json with module mapping only") {
        THEN("It should parse without throwing") {
            const json j = {{"module", {{"evse", 1}, {"connector", 2}}}};
            ModuleTierMappings m;
            REQUIRE_NOTHROW(m = j.get<ModuleTierMappings>());
            REQUIRE(m.module.has_value());
            CHECK(m.module.value().evse == 1);
            REQUIRE(m.module.value().connector.has_value());
            CHECK(m.module.value().connector.value() == 2);
            CHECK(m.implementations.empty());
        }
    }

    GIVEN("ModuleTierMappings json with module and implementations") {
        THEN("It should parse without throwing") {
            const json j = {{"module", {{"evse", 1}}},
                            {"implementations", {{"main", {{"evse", 1}, {"connector", 1}}}, {"sink", {{"evse", 2}}}}}};
            ModuleTierMappings m;
            REQUIRE_NOTHROW(m = j.get<ModuleTierMappings>());
            REQUIRE(m.module.has_value());
            CHECK(m.module.value().evse == 1);
            CHECK_FALSE(m.module.value().connector.has_value());
            REQUIRE(m.implementations.count("main") == 1);
            REQUIRE(m.implementations.at("main").has_value());
            CHECK(m.implementations.at("main").value().evse == 1);
            REQUIRE(m.implementations.at("main").value().connector.has_value());
            CHECK(m.implementations.at("main").value().connector.value() == 1);
            REQUIRE(m.implementations.count("sink") == 1);
            REQUIRE(m.implementations.at("sink").has_value());
            CHECK(m.implementations.at("sink").value().evse == 2);
            CHECK_FALSE(m.implementations.at("sink").value().connector.has_value());
        }
    }

    GIVEN("Null ModuleTierMappings json") {
        THEN("It should yield an empty mapping") {
            const json j = nullptr;
            ModuleTierMappings m;
            REQUIRE_NOTHROW(m = j.get<ModuleTierMappings>());
            CHECK_FALSE(m.module.has_value());
            CHECK(m.implementations.empty());
        }
    }
}
