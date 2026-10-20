// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <device_model_test_helper.hpp>

#include <ocpp/v2/availability_state_registration.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>

namespace ocpp::v2 {

class AvailabilityStateRegistrationTest : public ::testing::Test {
protected:
    DeviceModelTestHelper device_model_test_helper;
    const std::map<std::int32_t, std::int32_t> evse_connector_structure{{1, 1}, {2, 1}};

    static ComponentVariable charging_station() {
        return ControllerComponentVariables::ChargingStationAvailabilityState;
    }

    static ComponentVariable evse(const std::int32_t evse_id) {
        return EvseComponentVariables::get_component_variable(evse_id, EvseComponentVariables::AvailabilityState);
    }

    static ComponentVariable connector(const std::int32_t evse_id) {
        return ConnectorComponentVariables::get_component_variable(evse_id, 1,
                                                                   ConnectorComponentVariables::AvailabilityState);
    }

    static std::string get(DeviceModel& dm, const ComponentVariable& cv) {
        std::string value;
        EXPECT_EQ(dm.get_variable(cv.component, cv.variable.value(), AttributeEnum::Actual, value, false,
                                  UnsetValue::NotSupported),
                  GetVariableStatusEnum::Accepted);
        return value;
    }

    static std::optional<std::string> source(DeviceModel& dm, const ComponentVariable& cv) {
        const auto meta_data = dm.get_variable_meta_data(cv.component, cv.variable.value());
        EXPECT_TRUE(meta_data.has_value());
        return meta_data.has_value() ? meta_data->source : std::nullopt;
    }
};

TEST_F(AvailabilityStateRegistrationTest, charging_station_evses_and_connectors_get_their_availability_state) {
    // EVSE 1 and its connector come without AvailabilityState, as when EVerest registers the EVSEs
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, 1, std::nullopt,
                                                                 "AvailabilityState", std::nullopt));
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("Connector", std::nullopt, 1, 1, "AvailabilityState",
                                                                 std::nullopt));
    auto& dm = *device_model_test_helper.get_device_model();

    EXPECT_EQ(register_availability_states(dm, evse_connector_structure), 5);
    for (const auto& cv : {charging_station(), evse(1), connector(1), evse(2), connector(2)}) {
        EXPECT_EQ(source(dm, cv), VARIABLE_SOURCE_OCPP_RUNTIME);
        EXPECT_EQ(get(dm, cv), "Available");
    }

    // libocpp writes its own variables
    for (const auto& cv : {charging_station(), evse(1), evse(2)}) {
        EXPECT_EQ(
            dm.set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual, "Occupied", "internal"),
            SetVariableStatusEnum::Accepted);
        EXPECT_EQ(get(dm, cv), "Occupied");
    }
}

TEST_F(AvailabilityStateRegistrationTest, component_config_definitions_are_overridden_and_kept_as_fallback) {
    auto& dm = *device_model_test_helper.get_device_model();
    ASSERT_NE(source(dm, evse(2)), VARIABLE_SOURCE_OCPP_RUNTIME);
    ASSERT_EQ(get(dm, evse(2)), "Unavailable");

    EXPECT_EQ(register_availability_states(dm, evse_connector_structure), 5);
    EXPECT_EQ(source(dm, evse(2)), VARIABLE_SOURCE_OCPP_RUNTIME);
    EXPECT_EQ(get(dm, evse(2)), "Available");

    // A DeviceModel built without the registration reads the component config
    dm.unregister_components(VARIABLE_SOURCE_OCPP_RUNTIME);
    EXPECT_NE(source(dm, evse(2)), VARIABLE_SOURCE_OCPP_RUNTIME);
    EXPECT_EQ(get(dm, evse(2)), "Unavailable");
}

TEST_F(AvailabilityStateRegistrationTest, availability_state_of_another_owner_is_not_written_by_libocpp) {
    auto& dm = *device_model_test_helper.get_device_model();
    RegisteredComponent component;
    component.component = evse(1).component;
    RegisteredVariable variable;
    variable.variable = EvseComponentVariables::AvailabilityState;
    variable.characteristics.dataType = DataEnum::OptionList;
    variable.characteristics.supportsMonitoring = true;
    VariableAttribute attribute;
    attribute.mutability = MutabilityEnum::ReadOnly;
    attribute.value = "Available";
    variable.attributes.push_back(attribute);
    component.variables.push_back(variable);
    ASSERT_TRUE(dm.register_components("EVEREST", {component}, nullptr).rejected.empty());

    // The charging station is registered, the EVSE stays with its owner
    EXPECT_EQ(register_availability_states(dm, {{1, 0}}), 1);
    EXPECT_EQ(source(dm, charging_station()), VARIABLE_SOURCE_OCPP_RUNTIME);
    EXPECT_EQ(source(dm, evse(1)), "EVEREST");
    EXPECT_EQ(dm.set_read_only_value(evse(1).component, evse(1).variable.value(), AttributeEnum::Actual, "Occupied",
                                     "internal"),
              SetVariableStatusEnum::Rejected);
}

TEST_F(AvailabilityStateRegistrationTest, created_device_model_passes_the_integrity_check_without_config_definitions) {
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("ChargingStation", std::nullopt, std::nullopt,
                                                                 std::nullopt, "AvailabilityState", std::nullopt));
    for (const std::uint32_t evse_id : {1, 2}) {
        ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, evse_id, std::nullopt,
                                                                     "AvailabilityState", std::nullopt));
        ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("Connector", std::nullopt, evse_id, 1,
                                                                     "AvailabilityState", std::nullopt));
    }

    const auto dm = create_device_model_with_availability_states(
        evse_connector_structure, std::make_unique<DeviceModelStorageSqlite>(DEVICE_MODEL_DB_IN_MEMORY_PATH));

    for (const auto& cv : {charging_station(), evse(1), connector(1), evse(2), connector(2)}) {
        EXPECT_EQ(source(*dm, cv), VARIABLE_SOURCE_OCPP_RUNTIME);
        EXPECT_EQ(get(*dm, cv), "Available");
    }
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
}

} // namespace ocpp::v2
