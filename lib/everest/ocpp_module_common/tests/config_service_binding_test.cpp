// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

namespace {

namespace dm = ocpp_module_common::device_model;
using ocpp::v2::AttributeEnum;
using ocpp::v2::Component;
using ocpp::v2::SetVariableStatusEnum;
using ocpp::v2::Variable;

everest::config::ConfigurationParameter make_parameter(const std::string& name, everest::config::ConfigEntry value,
                                                       everest::config::Datatype datatype,
                                                       everest::config::Mutability mutability) {
    everest::config::ConfigurationParameter parameter;
    parameter.name = name;
    parameter.value = std::move(value);
    parameter.characteristics.datatype = datatype;
    parameter.characteristics.mutability = mutability;
    return parameter;
}

class FakeConfigService : public dm::ConfigServiceAccess {
public:
    FakeConfigService() {
        everest::config::ModuleConfigurationParameters auth;
        auto timeout = make_parameter("connection_timeout", 60, everest::config::Datatype::Integer,
                                      everest::config::Mutability::ReadWrite);
        timeout.characteristics.unit = "s";
        timeout.characteristics.min_value = 10;
        timeout.characteristics.max_value = 300;
        auth[Everest::config::MODULE_IMPLEMENTATION_ID].push_back(timeout);
        auth["main"].push_back(make_parameter("secret", std::string("hidden"), everest::config::Datatype::String,
                                              everest::config::Mutability::WriteOnly));
        this->configs[{"auth", "Auth"}] = auth;

        everest::config::ModuleConfigurationParameters evse_manager;
        evse_manager[Everest::config::MODULE_IMPLEMENTATION_ID].push_back(make_parameter(
            "session_logging", true, everest::config::Datatype::Boolean, everest::config::Mutability::ReadWrite));
        this->configs[{"connector_1", "EvseManager"}] = evse_manager;

        this->mappings["auth"] = ModuleTierMappings{};
        ModuleTierMappings evse_mapping;
        evse_mapping.module = Mapping(1);
        this->mappings["connector_1"] = evse_mapping;
    }

    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>
    get_module_configs() override {
        return this->configs;
    }

    std::map<std::string, ModuleTierMappings> get_mappings() override {
        return this->mappings;
    }

    Everest::config::SetConfigResult
    set_config_value(const everest::config::ConfigurationParameterIdentifier& identifier,
                     const std::string& value) override {
        this->writes.push_back({identifier, value});
        return this->result;
    }

    std::optional<std::function<void()>>
    register_config_update_handler(const std::string& module_id,
                                   Everest::config::ConfigServiceClient::ConfigUpdateHandler handler) override {
        const auto id = this->next_handler_id++;
        this->handlers[module_id].emplace(id, std::move(handler));
        return [this, module_id, id]() { this->handlers[module_id].erase(id); };
    }

    std::size_t handler_count() const {
        std::size_t count = 0;
        for (const auto& [module_id, module_handlers] : this->handlers) {
            count += module_handlers.size();
        }
        return count;
    }

    void publish(const std::string& module_id, const std::optional<std::string>& implementation_id,
                 const std::string& name, const std::string& value) {
        Everest::config::ConfigUpdateNotification notification;
        notification.identifier.module_id = module_id;
        notification.identifier.module_implementation_id = implementation_id;
        notification.identifier.configuration_parameter_name = name;
        notification.value = value;
        for (const auto& [id, handler] : this->handlers[module_id]) {
            handler(notification);
        }
    }

    std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters> configs;
    std::map<std::string, ModuleTierMappings> mappings;
    std::vector<std::pair<everest::config::ConfigurationParameterIdentifier, std::string>> writes;
    std::map<std::string, std::map<int, Everest::config::ConfigServiceClient::ConfigUpdateHandler>> handlers;
    int next_handler_id{0};
    Everest::config::SetConfigResult result{
        Everest::config::ResponseStatus::Ok, {}, everest::config::SetConfigStatus::Accepted};
};

class FakeRegistry : public ocpp::v2::DeviceModelRegistryInterface {
public:
    ocpp::v2::ComponentRegistrationResult
    register_components(const std::string& source, const std::vector<ocpp::v2::RegisteredComponent>& components,
                        ocpp::v2::VariableValueProvider* provider) override {
        this->source = source;
        this->components = components;
        this->provider = provider;
        return {};
    }

    void unregister_components(const std::string& source) override {
        this->unregistered.push_back(source);
    }

    SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                               AttributeEnum /*attribute*/, const std::string& value,
                                               const std::string& source) override {
        this->notified.push_back({component, variable, value, source});
        return SetVariableStatusEnum::Accepted;
    }

    const ocpp::v2::RegisteredVariable* find(const std::string& component_name, const std::string& variable_name) {
        for (const auto& component : this->components) {
            if (component.component.name.get() != component_name) {
                continue;
            }
            for (const auto& variable : component.variables) {
                if (variable.variable.name.get() == variable_name) {
                    return &variable;
                }
            }
        }
        return nullptr;
    }

    struct Notified {
        Component component;
        Variable variable;
        std::string value;
        std::string source;
    };

    std::string source;
    std::vector<ocpp::v2::RegisteredComponent> components;
    ocpp::v2::VariableValueProvider* provider{nullptr};
    std::vector<std::string> unregistered;
    std::vector<Notified> notified;
};

Component make_component(const std::string& name, const std::string& instance) {
    Component component;
    component.name = name;
    component.instance = instance;
    return component;
}

Variable make_variable(const std::string& name) {
    Variable variable;
    variable.name = name;
    return variable;
}

} // namespace

TEST(ConfigServiceBindingTest, RegistersOneComponentPerModuleUnderEverestConfig) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    EXPECT_EQ(registry.source, dm::VARIABLE_SOURCE_EVEREST_CONFIG);
    EXPECT_EQ(registry.provider, binding.get());
    ASSERT_EQ(registry.components.size(), 2u);

    const auto* timeout = registry.find("Auth", "connection_timeout");
    ASSERT_NE(timeout, nullptr);
    EXPECT_EQ(timeout->characteristics.dataType, ocpp::v2::DataEnum::integer);
    EXPECT_TRUE(timeout->characteristics.supportsMonitoring);
    EXPECT_EQ(timeout->characteristics.unit, "s");
    EXPECT_EQ(timeout->characteristics.minLimit, 10);
    EXPECT_EQ(timeout->characteristics.maxLimit, 300);
    ASSERT_EQ(timeout->attributes.size(), 1u);
    EXPECT_EQ(timeout->attributes.at(0).value.value().get(), "60");
    EXPECT_EQ(timeout->attributes.at(0).mutability, ocpp::v2::MutabilityEnum::ReadWrite);
    EXPECT_EQ(timeout->attributes.at(0).persistent, true);

    const auto* secret = registry.find("Auth", "main.secret");
    ASSERT_NE(secret, nullptr);
    EXPECT_FALSE(secret->characteristics.supportsMonitoring);
    EXPECT_EQ(secret->attributes.at(0).mutability, ocpp::v2::MutabilityEnum::WriteOnly);

    for (const auto& component : registry.components) {
        if (component.component.name.get() == "EvseManager") {
            EXPECT_EQ(component.component.instance, "connector_1");
            ASSERT_TRUE(component.component.evse.has_value());
            EXPECT_EQ(component.component.evse->id, 1);
        } else {
            EXPECT_EQ(component.component.instance, "auth");
            EXPECT_FALSE(component.component.evse.has_value());
        }
    }
}

TEST(ConfigServiceBindingTest, WriteGoesToTheConfigurationService) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("main.secret"), AttributeEnum::Actual,
                                 "new", "csms"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(config_service.writes.size(), 1u);
    EXPECT_EQ(config_service.writes.at(0).first.module_id, "auth");
    EXPECT_EQ(config_service.writes.at(0).first.module_implementation_id, "main");
    EXPECT_EQ(config_service.writes.at(0).first.configuration_parameter_name, "secret");
    EXPECT_EQ(config_service.writes.at(0).second, "new");

    config_service.result.set_status = everest::config::SetConfigStatus::RebootRequired;
    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("connection_timeout"),
                                 AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::RebootRequired);
    config_service.result.set_status = everest::config::SetConfigStatus::Rejected;
    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("connection_timeout"),
                                 AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Rejected);
    config_service.result.status = Everest::config::ResponseStatus::AccessDenied;
    config_service.result.set_status = everest::config::SetConfigStatus::Accepted;
    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("connection_timeout"),
                                 AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Rejected);

    // Other attributes and unknown variables never reach the configuration service
    const auto writes = config_service.writes.size();
    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("connection_timeout"),
                                 AttributeEnum::Target, "90", "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(binding->set_value(make_component("Auth", "auth"), make_variable("unknown"), AttributeEnum::Actual, "1",
                                 "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_EQ(config_service.writes.size(), writes);
}

TEST(ConfigServiceBindingTest, ChangeAtTheConfigurationServiceIsReported) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    config_service.publish("auth", std::nullopt, "connection_timeout", "120");
    config_service.publish("auth", "main", "secret", "other");
    config_service.publish("auth", std::nullopt, "unknown", "1");

    ASSERT_EQ(registry.notified.size(), 2u);
    EXPECT_EQ(registry.notified.at(0).component, make_component("Auth", "auth"));
    EXPECT_EQ(registry.notified.at(0).variable, make_variable("connection_timeout"));
    EXPECT_EQ(registry.notified.at(0).value, "120");
    EXPECT_EQ(registry.notified.at(0).source, dm::VARIABLE_SOURCE_EVEREST_CONFIG);
    EXPECT_EQ(registry.notified.at(1).variable, make_variable("main.secret"));
}

TEST(ConfigServiceBindingTest, StopUnregistersAndEndsNotifications) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);
    EXPECT_GT(config_service.handler_count(), 0u);
    binding->stop();
    EXPECT_EQ(registry.unregistered, std::vector<std::string>{dm::VARIABLE_SOURCE_EVEREST_CONFIG});
    EXPECT_EQ(config_service.handler_count(), 0u);

    config_service.publish("auth", std::nullopt, "connection_timeout", "120");
    EXPECT_TRUE(registry.notified.empty());
}

TEST(ConfigServiceBindingTest, DestructionStopsTheBinding) {
    FakeConfigService config_service;
    FakeRegistry registry;
    dm::ConfigServiceBinding::create(config_service, registry).reset();

    EXPECT_EQ(config_service.handler_count(), 0u);
    EXPECT_EQ(registry.unregistered, std::vector<std::string>{dm::VARIABLE_SOURCE_EVEREST_CONFIG});
}

TEST(ConfigServiceBindingTest, ChangeDuringStartIsReportedAfterRegistration) {
    // The change arrives between subscribing and registering: it is reported once the views are registered
    class ChangingConfigService : public FakeConfigService {
    public:
        std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>
        get_module_configs() override {
            if (++this->reads == 2) {
                this->publish("auth", std::nullopt, "connection_timeout", "120");
            }
            return FakeConfigService::get_module_configs();
        }
        int reads{0};
    } config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    ASSERT_EQ(registry.notified.size(), 1u);
    EXPECT_EQ(registry.notified.at(0).value, "120");
    EXPECT_EQ(registry.notified.at(0).source, dm::VARIABLE_SOURCE_EVEREST_CONFIG);
    EXPECT_NE(registry.find("Auth", "connection_timeout"), nullptr);
}

TEST(ConfigServiceBindingTest, ParametersOfUnknownTypeAreSkipped) {
    FakeConfigService config_service;
    config_service.configs[{"auth", "Auth"}][Everest::config::MODULE_IMPLEMENTATION_ID].push_back(make_parameter(
        "broken", std::string("x"), everest::config::Datatype::Unknown, everest::config::Mutability::ReadWrite));
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    EXPECT_EQ(registry.find("Auth", "broken"), nullptr);
    EXPECT_NE(registry.find("Auth", "connection_timeout"), nullptr);
}

TEST(ConfigServiceBindingTest, NamesThatDoNotFitOcppAreSkipped) {
    FakeConfigService config_service;
    const std::string too_long(51, 'x');
    config_service.configs[{"auth", "Auth"}]["main"].push_back(make_parameter(
        too_long, std::string("x"), everest::config::Datatype::String, everest::config::Mutability::ReadWrite));
    config_service.configs[{too_long, "Auth"}][Everest::config::MODULE_IMPLEMENTATION_ID].push_back(make_parameter(
        "connection_timeout", 60, everest::config::Datatype::Integer, everest::config::Mutability::ReadWrite));
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    EXPECT_EQ(registry.find("Auth", "main." + too_long), nullptr);
    EXPECT_NE(registry.find("Auth", "connection_timeout"), nullptr);
    EXPECT_NE(registry.find("EvseManager", "session_logging"), nullptr);
    for (const auto& component : registry.components) {
        EXPECT_NE(component.component.instance.value().get(), too_long);
    }
}

TEST(ConfigServiceBindingTest, NegativeIntegerLimitsAreNotRepresented) {
    everest::config::ConfigurationParameterCharacteristics characteristics;
    characteristics.datatype = everest::config::Datatype::Integer;
    characteristics.mutability = everest::config::Mutability::ReadWrite;
    characteristics.min_value = -10;
    characteristics.max_value = 10;
    const auto integer = dm::to_variable_characteristics(characteristics);
    EXPECT_FALSE(integer.minLimit.has_value());
    EXPECT_FALSE(integer.maxLimit.has_value());

    characteristics.datatype = everest::config::Datatype::Decimal;
    const auto decimal = dm::to_variable_characteristics(characteristics);
    EXPECT_EQ(decimal.minLimit, -10);
    EXPECT_EQ(decimal.maxLimit, 10);
}

// The views in a real device model: values are read from memory, written through the configuration service, and
// changes at the configuration service reach GetVariables
TEST(ConfigServiceBindingTest, ViewsInTheDeviceModel) {
    const auto db_path = std::filesystem::temp_directory_path() / "ocpp_module_common_config_service_binding.db";
    std::filesystem::remove(db_path);
    {
        ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
        init_db.initialize_database({}, true, false);
        init_db.close_connection();
    }
    ocpp::v2::DeviceModel device_model(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
    FakeConfigService config_service;
    const auto binding = dm::ConfigServiceBinding::create(config_service, device_model);

    const auto auth = make_component("Auth", "auth");
    const auto timeout = make_variable("connection_timeout");
    std::string value;
    ASSERT_EQ(device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Accepted);
    EXPECT_EQ(value, "60");

    // Outside the limits: rejected without reaching the configuration service
    EXPECT_EQ(device_model.set_value(auth, timeout, AttributeEnum::Actual, "5", "csms"),
              SetVariableStatusEnum::Rejected);
    EXPECT_TRUE(config_service.writes.empty());

    EXPECT_EQ(device_model.set_value(auth, timeout, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(config_service.writes.size(), 1u);
    device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false, ocpp::v2::UnsetValue::NotSupported);
    EXPECT_EQ(value, "90");

    config_service.publish("auth", std::nullopt, "connection_timeout", "120");
    device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false, ocpp::v2::UnsetValue::NotSupported);
    EXPECT_EQ(value, "120");
    EXPECT_EQ(config_service.writes.size(), 1u);

    // WriteOnly values are never read back
    EXPECT_EQ(device_model.get_variable(auth, make_variable("main.secret"), AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Rejected);

    binding->stop();
    EXPECT_EQ(device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::UnknownComponent);
    std::filesystem::remove(db_path);
}
