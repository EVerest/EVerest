// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <ocpp/v2/device_model_composition.hpp>

namespace ocpp::v2 {

namespace {

Component make_component(const std::string& name, const std::optional<std::int32_t>& evse_id = std::nullopt,
                         const std::optional<std::int32_t>& connector_id = std::nullopt) {
    Component component;
    component.name = name;
    if (evse_id.has_value()) {
        EVSE evse;
        evse.id = evse_id.value();
        evse.connectorId = connector_id;
        component.evse = evse;
    }
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

const ComposedVariable* find(const ComposedModel& model, const Component& component, const Variable& variable) {
    const auto component_it = model.find(component);
    if (component_it == model.end()) {
        return nullptr;
    }
    const auto variable_it = component_it->second.find(variable);
    return variable_it == component_it->second.end() ? nullptr : &variable_it->second;
}

class DeviceModelCompositionTest : public ::testing::Test {
protected:
    const Component controller = make_component("SampledDataCtrlr");
    const Component evse_1 = make_component("EVSE", 1);
    const Component evse_2 = make_component("EVSE", 2);
    const Component connector_1_1 = make_component("Connector", 1, 1);
    const Component connector_1_2 = make_component("Connector", 1, 2);
    const Variable enabled = make_variable("Enabled");
    const Variable power = make_variable("Power");

    DeviceModelMap component_config;
    std::map<std::string, Registration> registrations;

    void SetUp() override {
        for (const auto& component : {controller, evse_1, evse_2, connector_1_1, connector_1_2}) {
            this->component_config[component][this->enabled] = VariableMetaData{};
        }
    }

    void register_variable(const std::string& source, const Component& component, const Variable& variable) {
        auto& registration = this->registrations[source];
        registration.source = source;
        registration.variables[component][variable] = VariableMetaData{};
    }
};

} // namespace

TEST_F(DeviceModelCompositionTest, without_registrations_the_component_config_is_the_model) {
    const auto composition = compose_device_model(this->component_config, this->registrations);

    EXPECT_TRUE(composition.hidden.empty());
    for (const auto& [component, variables] : this->component_config) {
        const auto* composed = find(composition.model, component, this->enabled);
        ASSERT_NE(composed, nullptr) << component;
        EXPECT_EQ(composed->meta_data, &this->component_config.at(component).at(this->enabled));
        EXPECT_EQ(composed->registration, nullptr);
    }
}

TEST_F(DeviceModelCompositionTest, a_registered_variable_replaces_the_component_config_variable) {
    this->register_variable("EVEREST_CONFIG", this->controller, this->enabled);
    this->register_variable("EVEREST_CONFIG", this->controller, this->power);

    const auto composition = compose_device_model(this->component_config, this->registrations);

    auto& registration = this->registrations.at("EVEREST_CONFIG");
    const auto* overriding = find(composition.model, this->controller, this->enabled);
    ASSERT_NE(overriding, nullptr);
    EXPECT_EQ(overriding->registration, &registration);
    EXPECT_EQ(overriding->meta_data, &registration.variables.at(this->controller).at(this->enabled));
    const auto* added = find(composition.model, this->controller, this->power);
    ASSERT_NE(added, nullptr);
    EXPECT_EQ(added->registration, &registration);
}

TEST_F(DeviceModelCompositionTest, component_config_of_unregistered_evses_and_connectors_is_hidden) {
    this->register_variable("EVEREST", this->evse_1, this->power);
    this->register_variable("EVEREST", this->connector_1_1, this->power);

    const auto composition = compose_device_model(this->component_config, this->registrations);

    EXPECT_NE(find(composition.model, this->controller, this->enabled), nullptr);
    EXPECT_NE(find(composition.model, this->evse_1, this->enabled), nullptr);
    EXPECT_NE(find(composition.model, this->connector_1_1, this->enabled), nullptr);
    EXPECT_EQ(composition.model.count(this->evse_2), 0);
    EXPECT_EQ(composition.model.count(this->connector_1_2), 0);
    const std::vector<std::pair<Component, Variable>> expected_hidden{{this->evse_2, this->enabled},
                                                                      {this->connector_1_2, this->enabled}};
    EXPECT_EQ(composition.hidden.size(), expected_hidden.size());
    for (const auto& hidden : expected_hidden) {
        EXPECT_NE(std::find(composition.hidden.begin(), composition.hidden.end(), hidden), composition.hidden.end())
            << hidden.first;
    }
}

} // namespace ocpp::v2
