// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "everest_api_types/ocpp_device_model/codec.hpp"
#include "everest_api_types/utilities/codec.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>

namespace {

using namespace everest::lib::API::V1_0::types::ocpp_device_model;
using everest::lib::API::deserialize;
using nlohmann::json;

json registration() {
    return json::parse(R"({
        "owner": "meter_1",
        "components": [{
            "component": {"name": "Powermeter", "instance": "1", "evse": {"id": 1}},
            "variables": [{
                "variable": {"name": "CurrentLimit", "instance": "L1"},
                "characteristics": {"data_type": "decimal", "supports_monitoring": true, "unit": "A",
                                    "values_list": "a,b", "max_elements": 1},
                "attributes": [{"type": "Actual", "value": "32", "mutability": "ReadWrite"}],
                "monitors": [{"type": "UpperThreshold", "value": 40, "severity": 9,
                              "monitor_type": "HardWiredMonitor"}]
            }]
        }]
    })");
}

json& variable_of(json& reg) {
    return reg.at("components").at(0).at("variables").at(0);
}

bool parses(json const& reg) {
    Registration parsed;
    return deserialize(reg.dump(), parsed);
}

std::string chars(std::size_t count) {
    return std::string(count, 'x');
}

TEST(ocpp_device_model, registration_parses) {
    EXPECT_TRUE(parses(registration()));
}

TEST(ocpp_device_model, strings_are_limited_to_the_lengths_of_the_interface) {
    struct Limit {
        json::json_pointer field;
        std::size_t max_length;
    };
    const Limit limits[] = {
        {"/components/0/component/name"_json_pointer, 50},
        {"/components/0/component/instance"_json_pointer, 50},
        {"/components/0/variables/0/variable/name"_json_pointer, 50},
        {"/components/0/variables/0/variable/instance"_json_pointer, 50},
        {"/components/0/variables/0/characteristics/unit"_json_pointer, 16},
        {"/components/0/variables/0/characteristics/values_list"_json_pointer, 1000},
        {"/components/0/variables/0/attributes/0/value"_json_pointer, 2500},
    };
    for (auto const& limit : limits) {
        SCOPED_TRACE(limit.field.to_string());
        auto reg = registration();
        reg[limit.field] = chars(limit.max_length);
        EXPECT_TRUE(parses(reg));
        reg[limit.field] = chars(limit.max_length + 1);
        EXPECT_FALSE(parses(reg));
    }
}

TEST(ocpp_device_model, written_and_reported_values_are_limited) {
    const auto request = json::parse(R"({"owner": "meter_1", "component": {"name": "Powermeter"},
        "variable": {"name": "CurrentLimit"}, "attribute_type": "Actual", "value": "16", "source": "csms"})");
    auto change = json::parse(R"({"component": {"name": "Powermeter"}, "variable": {"name": "CurrentLimit"},
        "value": "16"})");
    WriteRequest parsed_request;
    ValueChange parsed_change;
    EXPECT_TRUE(deserialize(request.dump(), parsed_request));
    EXPECT_TRUE(deserialize(change.dump(), parsed_change));

    auto long_request = request;
    long_request["value"] = chars(2501);
    EXPECT_FALSE(deserialize(long_request.dump(), parsed_request));
    change["value"] = chars(2501);
    EXPECT_FALSE(deserialize(change.dump(), parsed_change));
}

TEST(ocpp_device_model, unknown_properties_are_rejected) {
    auto reg = registration();
    reg["extra"] = 1;
    EXPECT_FALSE(parses(reg));

    reg = registration();
    variable_of(reg)["characteristics"]["extra"] = 1;
    EXPECT_FALSE(parses(reg));
}

TEST(ocpp_device_model, owner_must_not_be_empty) {
    auto reg = registration();
    reg["owner"] = "";
    EXPECT_FALSE(parses(reg));
    reg.erase("owner");
    EXPECT_TRUE(parses(reg));
}

TEST(ocpp_device_model, severity_and_max_elements_are_bounded) {
    auto reg = registration();
    variable_of(reg)["monitors"][0]["severity"] = 10;
    EXPECT_FALSE(parses(reg));

    reg = registration();
    variable_of(reg)["monitors"][0]["severity"] = -1;
    EXPECT_FALSE(parses(reg));

    reg = registration();
    variable_of(reg)["characteristics"]["max_elements"] = 0;
    EXPECT_FALSE(parses(reg));
}

} // namespace
