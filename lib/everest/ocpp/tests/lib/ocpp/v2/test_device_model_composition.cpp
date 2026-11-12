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
    std::map<std::string, OwnerRegistration> registrations;

    void SetUp() override {
        for (const auto& component : {controller, evse_1, evse_2, connector_1_1, connector_1_2}) {
            this->component_config[component][this->enabled] = VariableMetaData{};
        }
    }

    void register_variable(const std::string& owner, const Component& component, const Variable& variable) {
        auto& registration = this->registrations[owner];
        registration.owner = owner;
        registration.variables[component][variable] = VariableMetaData{};
    }
};

} // namespace

TEST_F(DeviceModelCompositionTest, without_registrations_the_component_config_is_the_model) {
    const auto model = compose_device_model(this->component_config, this->registrations);

    for (const auto& [component, variables] : this->component_config) {
        const auto* composed = find(model, component, this->enabled);
        ASSERT_NE(composed, nullptr) << component;
        EXPECT_EQ(composed->meta_data, &this->component_config.at(component).at(this->enabled));
        EXPECT_EQ(composed->registration, nullptr);
    }
}

TEST_F(DeviceModelCompositionTest, a_component_combines_component_config_and_registered_variables) {
    this->register_variable("EVEREST_CONFIG", this->controller, this->power);

    const auto model = compose_device_model(this->component_config, this->registrations);

    auto& registration = this->registrations.at("EVEREST_CONFIG");
    const auto* from_config = find(model, this->controller, this->enabled);
    ASSERT_NE(from_config, nullptr);
    EXPECT_EQ(from_config->registration, nullptr);
    EXPECT_EQ(from_config->meta_data, &this->component_config.at(this->controller).at(this->enabled));
    const auto* registered = find(model, this->controller, this->power);
    ASSERT_NE(registered, nullptr);
    EXPECT_EQ(registered->registration, &registration);
    EXPECT_EQ(registered->meta_data, &registration.variables.at(this->controller).at(this->power));
}

} // namespace ocpp::v2
