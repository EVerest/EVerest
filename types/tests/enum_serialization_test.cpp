// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <nlohmann/json.hpp>

#include <generated/types/display_message.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/grid_support.hpp>
#include <generated/types/iso15118.hpp>
#include <generated/types/text_message.hpp>

using json = nlohmann::json;
using namespace types::evse_manager;
using namespace types::iso15118;

namespace display_message = types::display_message;
namespace text_message = types::text_message;
namespace grid_support = types::grid_support;

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

// Types without any required property start `to_json` from an empty object. An
// array-typed optional emitted while `j` is still empty must carry its payload.
SCENARIO("Array optionals in types without required properties keep their payload", "[codec]") {

    GIVEN("A GetDisplayMessageRequest whose first declared optional is the array") {
        display_message::GetDisplayMessageRequest request;
        request.id = std::vector<int32_t>{1, 2, 3};

        WHEN("It is serialized to json") {
            const json j = request;

            THEN("The array holds the given ids") {
                REQUIRE(j.at("id").is_array());
                CHECK(j.at("id") == json::array({1, 2, 3}));
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(request).get<display_message::GetDisplayMessageRequest>();

            THEN("The value is preserved") {
                CHECK(back == request);
            }
        }
    }

    // `status_info` must stay unset so that `messages` is the first present optional.
    GIVEN("A GetDisplayMessageResponse with only the later array optional set") {
        text_message::MessageContent content;
        content.content = "Charging paused";

        display_message::DisplayMessage message;
        message.message = content;

        display_message::GetDisplayMessageResponse response;
        response.messages = std::vector<display_message::DisplayMessage>{message};

        WHEN("It is serialized to json") {
            const json j = response;

            THEN("The array holds the given message") {
                REQUIRE(j.at("messages").is_array());
                REQUIRE(j.at("messages").size() == 1);
                CHECK(j.at("messages").at(0).at("message").at("content") == "Charging paused");
            }

            THEN("No key is invented for the unset optional") {
                CHECK(j.find("status_info") == j.end());
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(response).get<display_message::GetDisplayMessageResponse>();

            THEN("The value is preserved") {
                CHECK(back == response);
            }
        }
    }

    GIVEN("A GetDisplayMessageRequest with every optional unset") {
        const display_message::GetDisplayMessageRequest request;

        WHEN("It is serialized to json") {
            const json j = request;

            THEN("The result is an empty object") {
                CHECK(j == json::object());
            }
        }
    }

    GIVEN("A GetDisplayMessageResponse with an earlier optional already present") {
        text_message::MessageContent content;
        content.content = "Charging paused";

        display_message::DisplayMessage message;
        message.message = content;

        display_message::GetDisplayMessageResponse response;
        response.status_info = "Accepted";
        response.messages = std::vector<display_message::DisplayMessage>{message};

        WHEN("It is serialized to json") {
            const json j = response;

            THEN("Both the earlier optional and the array survive") {
                CHECK(j.at("status_info") == "Accepted");
                REQUIRE(j.at("messages").size() == 1);
                CHECK(j.at("messages").at(0).at("message").at("content") == "Charging paused");
            }
        }
    }

    GIVEN("A DERChargingParameters with a vector-of-enum as its first optional") {
        types::iso15118::DERChargingParameters parameters;
        parameters.ev_supported_dercontrol = std::vector<grid_support::DirectiveType>{
            grid_support::DirectiveType::VoltVar, grid_support::DirectiveType::FreqWatt};

        WHEN("It is serialized to json") {
            const json j = parameters;

            THEN("The enum elements are strings, not integers") {
                REQUIRE(j.at("ev_supported_dercontrol").is_array());
                REQUIRE(j.at("ev_supported_dercontrol").size() == 2);
                CHECK(j.at("ev_supported_dercontrol").at(0).is_string());
                CHECK(j.at("ev_supported_dercontrol") == json::array({"VoltVar", "FreqWatt"}));
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(parameters).get<types::iso15118::DERChargingParameters>();

            THEN("The value is preserved") {
                CHECK(back == parameters);
            }
        }
    }

    // Declared after 30-odd other optionals, all left unset here so that it is the
    // one emitted while `j` is still empty.
    GIVEN("A DERChargingParameters with only a later vector-of-enum set") {
        types::iso15118::DERChargingParameters parameters;
        parameters.ev_islanding_detection_method =
            std::vector<types::iso15118::IslandingDetection>{types::iso15118::IslandingDetection::RoCoF};

        WHEN("It is serialized to json") {
            const json j = parameters;

            THEN("The enum elements are strings, not integers") {
                REQUIRE(j.at("ev_islanding_detection_method").is_array());
                REQUIRE(j.at("ev_islanding_detection_method").size() == 1);
                CHECK(j.at("ev_islanding_detection_method") == json::array({"RoCoF"}));
            }
        }

        WHEN("It is round-tripped through json") {
            const auto back = json(parameters).get<types::iso15118::DERChargingParameters>();

            THEN("The value is preserved") {
                CHECK(back == parameters);
            }
        }
    }
}
