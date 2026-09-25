// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include "catalog.hpp"

using namespace telemetry_router;
using nlohmann::json;

namespace {

const auto FRAMEWORK_CATALOG = json::parse(R"({
    "evse_manager_1": {
        "module_type": "EvseManager",
        "mapping": {"evse": 1, "connector": 2},
        "elements": {
            "temperature": {"kind": "gauge", "type": "number", "unit": "Celsius", "description": "Temperature"},
            "state": {"kind": "state", "type": "string", "$ref": "/x#/State", "enum": ["Idle", "Charging"],
                      "description": "State"}
        }
    },
    "auth": {
        "module_type": "Auth",
        "elements": {
            "validations": {"kind": "counter", "type": "integer", "description": "Validations"}
        }
    }
})")
                                   .get<everest::telemetry::TelemetryCatalog>();

everest::telemetry::wire::Declare decode_declare(const json& payload) {
    const auto datagram = everest::telemetry::wire::encode_payload(payload);
    const auto decoded = everest::telemetry::wire::decode(datagram.data(), datagram.size());
    REQUIRE(decoded.message.has_value());
    return std::get<everest::telemetry::wire::Declare>(decoded.message.value());
}

everest::telemetry::wire::Declare declare(const std::string& module, json elements) {
    return decode_declare(
        {{"t", "d"}, {"module", module}, {"module_type", "ExternalDriver"}, {"elements", std::move(elements)}});
}

const json GAUGE = {{"kind", "gauge"}, {"type", "number"}, {"description", "A gauge"}};

} // namespace

TEST_CASE("The catalog is seeded from the framework telemetry catalog", "[telemetry_router]") {
    ElementCatalog catalog;
    catalog.seed(FRAMEWORK_CATALOG);

    const auto* producer = catalog.find_producer("evse_manager_1");
    REQUIRE(producer != nullptr);
    CHECK(producer->type == "EvseManager");
    CHECK(producer->evse == std::optional<int>(1));
    CHECK(producer->connector == std::optional<int>(2));
    CHECK_FALSE(producer->dynamic);
    CHECK(producer->elements.size() == 2);
    CHECK(catalog.find_element("evse_manager_1", "state")->enum_values == std::vector<std::string>{"Idle", "Charging"});
    CHECK_FALSE(catalog.find_producer("auth")->evse.has_value());
    CHECK(catalog.find_element("auth", "missing") == nullptr);
    CHECK(catalog.find_element("missing", "temperature") == nullptr);
}

TEST_CASE("Producers without a manifest declare their elements at runtime", "[telemetry_router]") {
    ElementCatalog catalog({2, 3});
    catalog.seed(FRAMEWORK_CATALOG);

    SECTION("Accepted and replaced") {
        auto result = catalog.declare(declare("ext:a", {{"voltage", GAUGE}, {"current", GAUGE}}));
        REQUIRE(result.accepted);
        CHECK(catalog.find_producer("ext:a")->dynamic);
        CHECK(catalog.find_producer("ext:a")->type == "ExternalDriver");
        CHECK(catalog.dynamic_element_count() == 2);

        result = catalog.declare(declare("ext:a", {{"power", GAUGE}}));
        REQUIRE(result.accepted);
        CHECK(catalog.find_element("ext:a", "voltage") == nullptr);
        CHECK(catalog.find_element("ext:a", "power") != nullptr);
        CHECK(catalog.dynamic_element_count() == 1);
    }
    SECTION("EVerest module ids cannot be declared") {
        const auto result = catalog.declare(declare("evse_manager_1", {{"voltage", GAUGE}}));
        CHECK_FALSE(result.accepted);
        CHECK_THAT(result.reason, Catch::Matchers::ContainsSubstring("EVerest module"));
        CHECK(catalog.find_element("evse_manager_1", "voltage") == nullptr);
    }
    SECTION("Limits") {
        REQUIRE(catalog.declare(declare("ext:a", {{"a", GAUGE}})).accepted);
        REQUIRE(catalog.declare(declare("ext:b", {{"b", GAUGE}})).accepted);
        CHECK_FALSE(catalog.declare(declare("ext:c", {{"c", GAUGE}})).accepted);
        CHECK_FALSE(catalog.declare(declare("ext:b", {{"b", GAUGE}, {"c", GAUGE}, {"d", GAUGE}})).accepted);
        CHECK(catalog.declare(declare("ext:b", {{"b", GAUGE}, {"c", GAUGE}})).accepted);
    }
    SECTION("Invalid declarations") {
        CHECK_FALSE(catalog.declare(declare("", {{"a", GAUGE}})).accepted);
        CHECK_FALSE(catalog.declare(declare("ext:a", json::object())).accepted);
        const auto partly_valid = catalog.declare(declare("ext:a", {{"a", GAUGE}, {"b", {{"kind", "gauge"}}}}));
        CHECK(partly_valid.accepted);
        CHECK(partly_valid.warnings.size() == 1);
        CHECK(catalog.find_producer("ext:a")->elements.size() == 1);
    }
}
