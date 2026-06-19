// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <generated/types/evse_manager.hpp>

using json = nlohmann::json;
using namespace types::evse_manager;
using namespace types::iso15118;

// Regression test for enum serialization in generated types.
SCENARIO("Enums nested in a vector serialize as strings", "[codec]") {

    GIVEN("A type from `evse_manager` containing a std::vector<enum>") {
        ChargingPausedEVSEReasons reasons;
        reasons.reasons = {PauseChargingEVSEReasonEnum::NoEnergy, PauseChargingEVSEReasonEnum::UserPause};

        WHEN("It is serialized to json") {
            const json j = reasons;

            THEN("The enum elements are strings, not integers") {
                REQUIRE(j.at("reasons").is_array());
                CHECK(j.at("reasons").at(0).is_string());
                CHECK(j.at("reasons") == json::array({"NoEnergy", "UserPause"}));
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(reasons).get<ChargingPausedEVSEReasons>();

            THEN("The value is preserved") {
                CHECK(back == reasons);
            }
        }
    }

    GIVEN("A type from `iso15118` containing a std::vector<enum>") {
        SupportedAppProtocols protocols;
        protocols.app_protocols = {SupportedAppProtocol::ISO15118D2};

        WHEN("It is serialized to json") {
            const json j = protocols;

            THEN("The enum elements are strings, not integers") {
                REQUIRE(j.at("app_protocols").is_array());
                CHECK(j.at("app_protocols").at(0).is_string());
                CHECK(j.at("app_protocols") == json::array({"ISO15118D2"}));
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(protocols).get<SupportedAppProtocols>();

            THEN("The value is preserved") {
                CHECK(back == protocols);
            }
        }
    }

    GIVEN("A a non empty payload with the string form") {
        const json j = json::parse(R"({"reasons":["NoEnergy"]})");

        WHEN("It is deserialized") {
            const auto reasons = j.get<ChargingPausedEVSEReasons>();

            THEN("The string is parsed into the enum") {
                REQUIRE(reasons.reasons.size() == 1);
                CHECK(reasons.reasons.at(0) == PauseChargingEVSEReasonEnum::NoEnergy);
            }
        }
    }

    GIVEN("A an empty payload with the string form") {
        const json j = json::parse(R"({"app_protocols":[]})");

        WHEN("It is deserialized") {
            const auto reasons = j.get<SupportedAppProtocols>();

            THEN("The string is parsed into the enum") {
                CHECK(reasons.app_protocols.size() == 0);
            }
        }
    }
}

// Guards the pre-existing scalar-enum path so the fix does not regress it.
SCENARIO("Scalar enum struct fields serialize as strings", "[codec]") {

    GIVEN("A SessionEvent with a scalar enum field") {
        SessionEvent event;
        event.uuid = "uuid";
        event.timestamp = "2026-06-17T03:43:09.149Z";
        event.event = SessionEventEnum::ChargingPausedEVSE;

        WHEN("It is serialized to json") {
            const json j = event;

            THEN("The event is the string variant") {
                CHECK(j.at("event") == "ChargingPausedEVSE");
            }
        }
    }
}
