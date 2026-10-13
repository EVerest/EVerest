// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <utils/config/mqtt_settings.hpp>
#include <utils/mqtt_abstraction_impl.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace Everest;

namespace {

// The instance is never connected; set_lwt() only operates on the local mosquitto
// handle, so no broker is required.
MQTTSettings test_settings() {
    return create_mqtt_settings("localhost", 1883, "everest/", "external/");
}

} // namespace

TEST_CASE("set_lwt rejects an empty topic", "[mqtt][lwt]") {
    MQTTAbstractionImpl impl{test_settings()};

    CHECK_FALSE(impl.set_lwt("", std::string{"payload"}));

    SECTION("json overload") {
        CHECK_FALSE(impl.set_lwt("", nlohmann::json{{"key", "value"}}));
    }

    SECTION("a rejected call does not consume the single LWT slot") {
        CHECK(impl.set_lwt("some/topic", std::string{"payload"}));
    }
}

TEST_CASE("set_lwt succeeds before connect", "[mqtt][lwt]") {
    MQTTAbstractionImpl impl{test_settings()};

    SECTION("string overload") {
        CHECK(impl.set_lwt("some/topic", std::string{"payload"}));
    }

    SECTION("json overload") {
        CHECK(impl.set_lwt("some/topic", nlohmann::json{{"key", "value"}}));
    }
}

TEST_CASE("set_lwt can only be set once", "[mqtt][lwt]") {
    MQTTAbstractionImpl impl{test_settings()};

    REQUIRE(impl.set_lwt("some/topic", std::string{"payload"}));

    SECTION("same topic, string overload") {
        CHECK_FALSE(impl.set_lwt("some/topic", std::string{"other payload"}));
    }

    SECTION("different topic, string overload") {
        CHECK_FALSE(impl.set_lwt("other/topic", std::string{"payload"}));
    }

    SECTION("json overload") {
        CHECK_FALSE(impl.set_lwt("other/topic", nlohmann::json{{"key", "value"}}));
    }
}
