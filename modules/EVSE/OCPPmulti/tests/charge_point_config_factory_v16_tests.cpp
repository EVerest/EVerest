// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file charge_point_config_factory_v16_tests.cpp
/// \brief Tests that the OCPP 1.6 config factory backs the configuration with the device model database and takes
/// over the EVerest device model database of earlier versions.

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/definitions.hpp>
#include <everest/ocpp_module_common/device_model/everest_device_model.hpp>
#include <everest/ocpp_module_common/device_model/external_owners.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_storage_interface.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

#include "charge_point_config_factory_v16.hpp"
#include "stubs/interfaces_stub.hpp"

namespace {

namespace fs = std::filesystem;

/// \brief An empty directory below the temporary directory that no other test binary or checkout shares
fs::path create_unique_directory(const std::string& prefix) {
    std::string pattern = (fs::temp_directory_path() / (prefix + "_XXXXXX")).string();
    if (mkdtemp(pattern.data()) == nullptr) {
        throw std::runtime_error("Could not create a temporary directory from " + pattern);
    }
    return pattern;
}

ocpp::v2::Component make_component(const std::string& name) {
    ocpp::v2::Component component;
    component.name = name;
    return component;
}

ocpp::v2::Variable make_variable(const std::string& name) {
    ocpp::v2::Variable variable;
    variable.name = name;
    return variable;
}

std::optional<std::string> evse_power_of(ocpp_module_common::device_model::EverestDeviceModel& everest_device_model,
                                         std::int32_t evse_id) {
    for (const auto& component : everest_device_model.get_components()) {
        if (component.component.name.get() != "EVSE" || !component.component.evse.has_value() ||
            component.component.evse->id != evse_id) {
            continue;
        }
        for (const auto& variable : component.variables) {
            if (variable.variable == ocpp::v2::EvseComponentVariables::Power) {
                const auto& value = variable.attributes.front().value;
                return value.has_value() ? std::optional<std::string>{value->get()} : std::nullopt;
            }
        }
    }
    return std::nullopt;
}

/// \brief One module configuration, "auth" of type "Auth" with the parameter connection_timeout
class FakeConfigService : public ocpp_module_common::device_model::ConfigServiceAccess {
public:
    FakeConfigService() {
        everest::config::ConfigurationParameter parameter;
        parameter.name = "connection_timeout";
        parameter.value = 60;
        parameter.characteristics.datatype = everest::config::Datatype::Integer;
        parameter.characteristics.mutability = everest::config::Mutability::ReadWrite;
        this->configs[{"auth", "Auth"}][Everest::config::MODULE_IMPLEMENTATION_ID].push_back(parameter);
        this->mappings["auth"] = ModuleTierMappings{};
    }

    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>
    get_module_configs() override {
        return this->configs;
    }

    std::map<std::string, ModuleTierMappings> get_mappings() override {
        return this->mappings;
    }

    Everest::config::SetConfigResult set_config_value(const everest::config::ConfigurationParameterIdentifier&,
                                                      const std::string&) override {
        return {Everest::config::ResponseStatus::Ok, {}, everest::config::SetConfigStatus::Accepted};
    }

    std::optional<std::function<void()>>
    register_config_update_handler(const std::string& /*module_id*/,
                                   Everest::config::ConfigServiceClient::ConfigUpdateHandler /*handler*/) override {
        this->handlers++;
        return [this]() { this->handlers--; };
    }

    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters> configs;
    std::map<std::string, ModuleTierMappings> mappings;
    int handlers{0};
};

class ChargePointConfigFactoryV16Test : public ::testing::Test {
protected:
    void SetUp() override {
        work_dir = create_unique_directory("ocppmulti_factory_v16_tests");
    }

    void TearDown() override {
        fs::remove_all(work_dir);
    }

    module::config_factory_v16::Ocpp16DeviceModelParams make_params() const {
        // Made absolute, as the factory resolves relative paths against the share path.
        return {
            (work_dir / "device_model_storage.db").string(),            // DeviceModelDatabasePath
            fs::absolute(LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR).string(), // DeviceModelDatabaseMigrationPath
            fs::absolute(LIBOCPP_COMPONENT_CONFIG_DIR).string(),        // DeviceModelConfigPath
            "",                                                         // Ocpp16KeyMappingsPath
            1,                                                          // Ocpp16NetworkConfigSlot
            false,                                                      // EnableLegacyConfigMigration
            "",                                                         // ChargePointConfigPath
            "",                                                         // UserConfigPath
            false,                                                      // FailOnDeviceModelConflicts
        };
    }

    // The shipped custom component configs define two EVSEs.
    static constexpr std::int32_t n_evse = 2;

    fs::path work_dir;
};

TEST_F(ChargePointConfigFactoryV16Test, OcppVariableResolves) {
    auto result = module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, {});
    ASSERT_NE(result.configuration, nullptr);
    auto& device_model = result.configuration->get_device_model();

    const auto& heartbeat_interval = ocpp::v2::ControllerComponentVariables::HeartbeatInterval;
    ASSERT_TRUE(heartbeat_interval.variable.has_value());
    std::string value;
    EXPECT_EQ(device_model.get_variable(heartbeat_interval.component, heartbeat_interval.variable.value(),
                                        ocpp::v2::AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_FALSE(value.empty());
}

// The EVerest device model database of earlier versions is taken over at the first start and renamed
TEST_F(ChargePointConfigFactoryV16Test, EverestDeviceModelDatabaseIsTakenOver) {
    const auto everest_db_path = work_dir / "everest_device_model_storage.db";
    {
        ocpp::v2::InitDeviceModelDb init(everest_db_path, fs::absolute(LIBOCPP_DEVICE_MODEL_MIGRATIONS_DIR));
        init.initialize_database({}, false, false);
        init.close_connection();
    }

    auto result =
        module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, everest_db_path);
    ASSERT_NE(result.configuration, nullptr);
    EXPECT_FALSE(fs::exists(everest_db_path));
    EXPECT_TRUE(fs::exists(work_dir / "everest_device_model_storage.db.migrated"));
}

// The shipped component configs define EVSE 1, which EVerest registers as well
TEST_F(ChargePointConfigFactoryV16Test, FailedStartDetachesEverestDeviceModel) {
    stubs::ModuleInterfaces interfaces;
    interfaces.add_evse_manager("evse_manager_1");
    ocpp_module_common::device_model::EverestDeviceModel everest_device_model(
        interfaces.r_evse_manager, interfaces.r_extensions_15118,
        std::map<int32_t, types::evse_board_support::HardwareCapabilities>{},
        std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>{}, std::map<int32_t, bool>{}, false,
        std::set<int32_t>{});
    auto params = make_params();
    params.FailOnDeviceModelConflicts = true;

    try {
        module::config_factory_v16::create_charge_point_configuration(work_dir, params, n_evse, {},
                                                                      &everest_device_model);
        FAIL() << "The conflict did not stop the start";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("FailOnDeviceModelConflicts"), std::string::npos) << e.what();
    }

    // A detached EVerest device model keeps the value without reporting it
    everest_device_model.update_power(1, 11000.0F);
    EXPECT_EQ(evse_power_of(everest_device_model, 1), std::to_string(11000.0F));
}

TEST_F(ChargePointConfigFactoryV16Test, ConfigurationViewsAreRegistered) {
    FakeConfigService config_service;
    auto result = module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, {},
                                                                                nullptr, &config_service);
    ASSERT_EQ(result.registrations.results().size(), 1u);
    EXPECT_EQ(result.registrations.results().front().owner,
              ocpp_module_common::device_model::VARIABLE_OWNER_EVEREST_CONFIG);
    EXPECT_EQ(config_service.handlers, 1);

    ocpp::v2::Component component = make_component("Auth");
    component.instance = "auth";
    std::string value;
    EXPECT_EQ(result.configuration->get_device_model().get_variable(component, make_variable("connection_timeout"),
                                                                    ocpp::v2::AttributeEnum::Actual, value, false,
                                                                    ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "60");

    result.registrations.end();
    EXPECT_EQ(config_service.handlers, 0);
}

TEST_F(ChargePointConfigFactoryV16Test, ExternalOwnersAreRegistered) {
    namespace dm_types = types::ocpp_device_model;
    class Connection : public ocpp_module_common::device_model::DeviceModelOwnerConnection {
    public:
        std::string module_id() const override {
            return "device_model_api";
        }
        void set_registration_result(const dm_types::RegistrationResult& result) override {
            this->results.push_back(result);
        }
        types::ocpp::SetVariableStatusEnumType set_value(const dm_types::WriteRequest& /*request*/) override {
            return types::ocpp::SetVariableStatusEnumType::Accepted;
        }
        std::vector<dm_types::RegistrationResult> results;
    };
    const auto connection = std::make_shared<Connection>();
    ocpp_module_common::device_model::ExternalOwners owners({connection});

    dm_types::VariableAttribute attribute;
    attribute.value = "40";
    dm_types::RegisteredVariable variable;
    variable.variable.name = "FanSpeed";
    variable.characteristics.data_type = dm_types::DataEnum::integer;
    variable.characteristics.supports_monitoring = true;
    variable.attributes.push_back(attribute);
    dm_types::RegisteredComponent component;
    component.component.name = "CoolingCtrlr";
    component.variables.push_back(variable);
    dm_types::Registration registration;
    registration.components.push_back(component);
    dm_types::Registrations registrations;
    registrations.registrations.push_back(registration);
    owners.set_startup_registrations(0, registrations);

    auto result = module::config_factory_v16::create_charge_point_configuration(work_dir, make_params(), n_evse, {},
                                                                                nullptr, nullptr, &owners);
    ASSERT_EQ(result.registrations.results().size(), 1u);
    EXPECT_EQ(result.registrations.results().front().owner, "device_model_api");
    ASSERT_EQ(connection->results.size(), 1u);
    EXPECT_EQ(connection->results.front().status, dm_types::RegistrationStatus::Accepted);

    std::string value;
    EXPECT_EQ(result.configuration->get_device_model().get_variable(
                  make_component("CoolingCtrlr"), make_variable("FanSpeed"), ocpp::v2::AttributeEnum::Actual, value,
                  false, ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "40");

    result.registrations.end();
    EXPECT_EQ(result.configuration->get_device_model().get_variable(
                  make_component("CoolingCtrlr"), make_variable("FanSpeed"), ocpp::v2::AttributeEnum::Actual, value,
                  false, ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::UnknownComponent);
}

// The configuration views are stopped before the device model that holds them is destroyed
TEST_F(ChargePointConfigFactoryV16Test, FailedStartStopsConfigurationViews) {
    stubs::ModuleInterfaces interfaces;
    interfaces.add_evse_manager("evse_manager_1");
    ocpp_module_common::device_model::EverestDeviceModel everest_device_model(
        interfaces.r_evse_manager, interfaces.r_extensions_15118,
        std::map<int32_t, types::evse_board_support::HardwareCapabilities>{},
        std::map<int32_t, std::vector<types::iso15118::EnergyTransferMode>>{}, std::map<int32_t, bool>{}, false,
        std::set<int32_t>{});
    FakeConfigService config_service;
    auto params = make_params();
    params.FailOnDeviceModelConflicts = true;

    EXPECT_THROW(module::config_factory_v16::create_charge_point_configuration(work_dir, params, n_evse, {},
                                                                               &everest_device_model, &config_service),
                 std::runtime_error);
    EXPECT_EQ(config_service.handlers, 0);
}

// DER is not supported in OCPP 1.6 mode: the start removes what a CSMS set on the DER controllers in OCPP 2.x mode
TEST_F(ChargePointConfigFactoryV16Test, StartRemovesMonitorsAndValuesOfTheDerControllers) {
    const auto params = make_params();
    module::config_factory_v16::create_charge_point_configuration(work_dir, params, n_evse, {});

    auto der_ctrlr = make_component("DCDERCtrlr");
    der_ctrlr.evse = ocpp::v2::EVSE{};
    der_ctrlr.evse->id = 1;
    const auto enabled = make_variable("Enabled");
    const auto& heartbeat_interval = ocpp::v2::ControllerComponentVariables::HeartbeatInterval;
    {
        ocpp::v2::DeviceModelStorageSqlite storage(params.DeviceModelDatabasePath);
        ASSERT_EQ(storage.set_stored_value({der_ctrlr, enabled, ocpp::v2::AttributeEnum::Actual, "false", "csms",
                                            ocpp_module_common::device_model::VARIABLE_OWNER_EVEREST}),
                  ocpp::v2::SetVariableStatusEnum::Accepted);
        ocpp::v2::SetMonitoringData der_monitor{};
        der_monitor.type = ocpp::v2::MonitorEnum::Delta;
        der_monitor.value = 1.0F;
        der_monitor.severity = 5;
        der_monitor.component = der_ctrlr;
        der_monitor.variable = enabled;
        ASSERT_TRUE(storage.set_monitoring_data(der_monitor, ocpp::v2::VariableMonitorType::CustomMonitor));
        auto heartbeat_monitor = der_monitor;
        heartbeat_monitor.type = ocpp::v2::MonitorEnum::UpperThreshold;
        heartbeat_monitor.value = 600.0F;
        heartbeat_monitor.component = heartbeat_interval.component;
        heartbeat_monitor.variable = heartbeat_interval.variable.value();
        ASSERT_TRUE(storage.set_monitoring_data(heartbeat_monitor, ocpp::v2::VariableMonitorType::CustomMonitor));
    }

    module::config_factory_v16::create_charge_point_configuration(work_dir, params, n_evse, {});

    ocpp::v2::DeviceModelStorageSqlite storage(params.DeviceModelDatabasePath);
    EXPECT_TRUE(storage.get_stored_values().empty());
    const auto monitors = storage.get_all_monitors();
    ASSERT_EQ(monitors.size(), 1u);
    EXPECT_EQ(monitors.front().component, heartbeat_interval.component);
    EXPECT_EQ(monitors.front().variable, heartbeat_interval.variable.value());
}

} // namespace
