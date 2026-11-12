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

    static std::string owner(DeviceModel& dm, const ComponentVariable& cv) {
        const auto meta_data = dm.get_variable_meta_data(cv.component, cv.variable.value());
        EXPECT_TRUE(meta_data.has_value());
        return meta_data.has_value() ? meta_data->owner : "missing";
    }
};

TEST_F(AvailabilityStateRegistrationTest, availability_states_the_component_config_does_not_define_are_registered) {
    // EVSE 1 and its connector come without AvailabilityState, as when EVerest registers the EVSEs
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, 1, std::nullopt,
                                                                 "AvailabilityState", std::nullopt));
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("Connector", std::nullopt, 1, 1, "AvailabilityState",
                                                                 std::nullopt));
    auto& dm = *device_model_test_helper.get_device_model();
    ASSERT_EQ(get(dm, evse(2)), "Unavailable");

    EXPECT_EQ(register_availability_states(dm, evse_connector_structure), 2);
    for (const auto& cv : {evse(1), connector(1)}) {
        EXPECT_EQ(owner(dm, cv), VARIABLE_OWNER_OCPP_STACK);
        EXPECT_EQ(get(dm, cv), "Available");
    }
    for (const auto& cv : {charging_station(), evse(2), connector(2)}) {
        EXPECT_EQ(owner(dm, cv), VARIABLE_OWNER_COMPONENT_CONFIG);
    }
    EXPECT_EQ(get(dm, evse(2)), "Unavailable");

    // libocpp writes its own variables and those of the component config
    for (const auto& cv : {charging_station(), evse(1), evse(2)}) {
        EXPECT_EQ(
            dm.set_read_only_value(cv.component, cv.variable.value(), AttributeEnum::Actual, "Occupied", "internal"),
            SetVariableStatusEnum::Accepted);
        EXPECT_EQ(get(dm, cv), "Occupied");
    }

    // Registering again keeps what libocpp registered
    EXPECT_EQ(register_availability_states(dm, evse_connector_structure), 2);
    EXPECT_EQ(owner(dm, evse(1)), VARIABLE_OWNER_OCPP_STACK);
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
        EXPECT_EQ(owner(*dm, cv), VARIABLE_OWNER_OCPP_STACK);
        EXPECT_EQ(get(*dm, cv), "Available");
    }
    EXPECT_NO_THROW(dm->check_integrity(evse_connector_structure));
}

TEST_F(AvailabilityStateRegistrationTest, runtime_owners_cannot_unregister_or_report_for_libocpp) {
    ASSERT_TRUE(device_model_test_helper.remove_variable_from_db("EVSE", std::nullopt, 1, std::nullopt,
                                                                 "AvailabilityState", std::nullopt));
    auto& dm = *device_model_test_helper.get_device_model();
    ASSERT_EQ(register_availability_states(dm, evse_connector_structure), 1);
    DeviceModelRegistryInterface& registry = dm;

    registry.unregister_components(VARIABLE_OWNER_OCPP_STACK);
    EXPECT_EQ(owner(dm, evse(1)), VARIABLE_OWNER_OCPP_STACK);

    EXPECT_EQ(registry.notify_value_changed(evse(1).component, evse(1).variable.value(), AttributeEnum::Actual,
                                            "Faulted", VARIABLE_OWNER_OCPP_STACK),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(get(dm, evse(1)), "Available");
}

} // namespace ocpp::v2
