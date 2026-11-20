// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <everest/ocpp_module_common/device_model/config_service_binding.hpp>
#include <everest/ocpp_module_common/device_model/configuration_mapping.hpp>
#include <everest/ocpp_module_common/device_model/device_model_registrations.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/device_model_storage_sqlite.hpp>
#include <ocpp/v2/init_device_model_db.hpp>

#include "unique_directory.hpp"

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
        if (this->deny_subscription) {
            return std::nullopt;
        }
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
                 const std::string& name, const std::string& value,
                 Everest::config::SetResponseStatus status = Everest::config::SetResponseStatus::Accepted) {
        Everest::config::ConfigUpdateNotification notification;
        notification.status = status;
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
    bool deny_subscription{false};
    Everest::config::SetConfigResult result{
        Everest::config::ResponseStatus::Ok, {}, everest::config::SetConfigStatus::Accepted};
};

class FakeRegistry : public ocpp::v2::DeviceModelRegistryInterface {
public:
    ocpp::v2::ComponentRegistrationResult
    register_components(const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
                        std::shared_ptr<ocpp::v2::VariableValueProvider> provider) override {
        this->owner = owner;
        this->components = components;
        this->provider = provider.get();
        return {};
    }

    void unregister_components(const std::string& owner) override {
        this->unregistered.push_back(owner);
    }

    SetVariableStatusEnum notify_value_changed(const Component& component, const Variable& variable,
                                               AttributeEnum /*attribute*/, const std::string& value,
                                               const std::string& owner) override {
        this->notified.push_back({component, variable, value, owner});
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
        std::string owner;
    };

    std::string owner;
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

    EXPECT_EQ(registry.owner, dm::VARIABLE_OWNER_EVEREST_CONFIG);
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

namespace {

dm::ConfigParameterBinding timeout_mapping() {
    dm::ConfigParameterBinding mapping;
    mapping.parameter.module_id = "auth";
    mapping.parameter.configuration_parameter_name = "connection_timeout";
    mapping.component.name = "TxCtrlr";
    mapping.variable.name = "EVConnectionTimeOut";
    return mapping;
}

} // namespace

TEST(ConfigurationMappingTest, ExplicitMappingReplacesAutomaticView) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto mapping = timeout_mapping();
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry, {mapping});
    ASSERT_NE(registry.find("TxCtrlr", "EVConnectionTimeOut"), nullptr);
    EXPECT_EQ(registry.find("Auth", "connection_timeout"), nullptr);
    EXPECT_NE(registry.find("Auth", "main.secret"), nullptr);

    EXPECT_EQ(binding->set_value(mapping.component, mapping.variable, AttributeEnum::Actual, "90", "csms"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(config_service.writes.size(), 1u);
    EXPECT_EQ(config_service.writes.front().first.module_id, "auth");
    EXPECT_EQ(config_service.writes.front().first.configuration_parameter_name, "connection_timeout");
    config_service.publish("auth", std::nullopt, "connection_timeout", "120");
    ASSERT_EQ(registry.notified.size(), 1u);
    EXPECT_EQ(registry.notified.front().component, mapping.component);
    EXPECT_EQ(registry.notified.front().variable, mapping.variable);
    EXPECT_EQ(registry.notified.front().value, "120");
    EXPECT_EQ(config_service.writes.size(), 1u);
}

TEST(ConfigurationMappingTest, ImplementationAndScopedTargetKeepTheirIdentities) {
    FakeConfigService config_service;
    auto mapping = timeout_mapping();
    mapping.parameter.module_implementation_id = "main";
    mapping.parameter.configuration_parameter_name = "secret";
    mapping.component.instance = "controller";
    ocpp::v2::EVSE evse;
    evse.id = 1;
    evse.connectorId = 2;
    mapping.component.evse = evse;
    mapping.variable.instance = "credential";
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry, {mapping}, false);
    ASSERT_EQ(registry.components.size(), 1u);
    EXPECT_EQ(registry.components.front().component, mapping.component);
    EXPECT_EQ(registry.components.front().variables.front().variable, mapping.variable);
    EXPECT_EQ(binding->set_value(mapping.component, mapping.variable, AttributeEnum::Actual, "updated", "csms"),
              SetVariableStatusEnum::Accepted);
    ASSERT_EQ(config_service.writes.size(), 1u);
    EXPECT_EQ(config_service.writes.front().first.module_implementation_id, "main");
    EXPECT_EQ(config_service.writes.front().first.configuration_parameter_name, "secret");
}

TEST(ConfigurationMappingTest, AutomaticExposureCanBeDisabled) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry, {timeout_mapping()}, false);
    ASSERT_EQ(registry.components.size(), 1u);
    EXPECT_NE(registry.find("TxCtrlr", "EVConnectionTimeOut"), nullptr);
    EXPECT_EQ(registry.find("Auth", "main.secret"), nullptr);
    EXPECT_EQ(registry.find("EvseManager", "session_logging"), nullptr);
}

TEST(ConfigurationMappingTest, ExplicitMappingRequiresChangeNotifications) {
    FakeConfigService config_service;
    config_service.deny_subscription = true;
    FakeRegistry registry;
    EXPECT_THROW(dm::ConfigServiceBinding::create(config_service, registry, {timeout_mapping()}, false),
                 std::invalid_argument);
    EXPECT_EQ(registry.provider, nullptr);
}

TEST(ConfigurationMappingTest, RejectedExplicitRegistrationCleansUpTheProvider) {
    class RejectingRegistry : public FakeRegistry {
    public:
        ocpp::v2::ComponentRegistrationResult
        register_components(const std::string& owner, const std::vector<ocpp::v2::RegisteredComponent>& components,
                            std::shared_ptr<ocpp::v2::VariableValueProvider> provider) override {
            FakeRegistry::register_components(owner, components, std::move(provider));
            const auto mapping = timeout_mapping();
            return {{{mapping.component, mapping.variable, ocpp::v2::RegistrationRejectionReason::AlreadyDefined,
                      "other"}}};
        }
    } registry;
    FakeConfigService config_service;
    EXPECT_THROW(dm::ConfigServiceBinding::create(config_service, registry, {timeout_mapping()}, false),
                 std::invalid_argument);
    ASSERT_EQ(registry.unregistered.size(), 1u);
    for (const auto& [module, handlers] : config_service.handlers) {
        EXPECT_TRUE(handlers.empty());
    }
}

TEST(ConfigurationMappingTest, RejectsUnknownSourcesAndDuplicateSourcesOrTargets) {
    FakeConfigService config_service;
    const auto build = [&](const std::vector<dm::ConfigParameterBinding>& mappings) {
        return dm::build_configuration_views(config_service.configs, config_service.mappings, mappings, true);
    };
    auto mapping = timeout_mapping();
    auto other = mapping;
    other.parameter.configuration_parameter_name = "missing";
    EXPECT_THROW(build({other}), std::invalid_argument);
    other = mapping;
    other.variable.name = "Other";
    EXPECT_THROW(build({mapping, other}), std::invalid_argument);
    other = mapping;
    other.parameter.module_implementation_id = "main";
    other.parameter.configuration_parameter_name = "secret";
    other.component.name = "txctrlr";
    other.variable.name = "evconnectiontimeout";
    EXPECT_THROW(build({mapping, other}), std::invalid_argument);
    other = mapping;
    other.component = make_component("Auth", "auth");
    other.variable.name = "main.secret";
    EXPECT_THROW(build({other}), std::invalid_argument);
    config_service.configs.at({"auth", "Auth"})[Everest::config::MODULE_IMPLEMENTATION_ID]
        .front()
        .characteristics.datatype = everest::config::Datatype::Unknown;
    EXPECT_THROW(build({mapping}), std::out_of_range);
}

TEST(ConfigurationMappingTest, LoadsScopedYamlAndRejectsInvalidFiles) {
    const auto schema_path =
        std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR) / "configuration_mapping.schema.json";
    const auto work_dir = test_support::create_unique_directory("ocpp_configuration_mapping");
    const auto path = work_dir / "mapping.yaml";
    const auto write = [&](const std::string& yaml) { std::ofstream(path) << yaml; };
    write(R"(mappings:
  - everest:
      module_id: connector_1
      implementation_id: main
      parameter: enabled
    ocpp:
      component:
        name: ISO15118Ctrlr
        instance: controller
        evse:
          id: 1
          connectorId: 2
      variable:
        name: Enabled
        instance: pnc
)");
    const auto mappings = dm::load_configuration_mappings(path, schema_path);
    ASSERT_EQ(mappings.size(), 1u);
    EXPECT_EQ(mappings.front().parameter.module_implementation_id, "main");
    EXPECT_EQ(mappings.front().component.instance, "controller");
    ASSERT_TRUE(mappings.front().component.evse.has_value());
    EXPECT_EQ(mappings.front().component.evse->id, 1);
    EXPECT_EQ(mappings.front().component.evse->connectorId, 2);
    EXPECT_EQ(mappings.front().variable.instance, "pnc");
    EXPECT_TRUE(dm::load_configuration_mappings({}, work_dir / "missing.schema.json").empty());
    EXPECT_THROW(dm::load_configuration_mappings(work_dir / "missing.yaml", schema_path), std::invalid_argument);
    for (const auto& yaml :
         {"mappings: {}", "mappings: []\nunknown: true", "mappings: [{}]", "mappings: [", R"(mappings:
  - everest: {module_id: auth, parameter: connection_timeout}
    ocpp: {component: {name: TxCtrlr, evse: {id: 0}}, variable: {name: EVConnectionTimeOut}}
)",
          R"(mappings:
  - everest: {module_id: auth, parameter: connection_timeout, typo: true}
    ocpp: {component: {name: TxCtrlr}, variable: {name: EVConnectionTimeOut}}
)"}) {
        write(yaml);
        EXPECT_THROW(dm::load_configuration_mappings(path, schema_path), std::invalid_argument) << yaml;
    }
    std::filesystem::remove_all(work_dir);
}

TEST(ConfigurationMappingTest, CheckedInExampleLoadsWithSchemaFile) {
    const auto schemas = std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR);
    const auto mappings = dm::load_configuration_mappings(schemas / "configuration_mapping.example.yaml",
                                                          schemas / "configuration_mapping.schema.json");
    ASSERT_EQ(mappings.size(), 1u);
    EXPECT_EQ(mappings.front().parameter.module_id, "auth");
    EXPECT_EQ(mappings.front().parameter.configuration_parameter_name, "connection_timeout");
    EXPECT_EQ(mappings.front().component.name, "TxCtrlr");
    EXPECT_EQ(mappings.front().variable.name, "EVConnectionTimeOut");
}

TEST(ConfigurationMappingTest, StationMappingsKeepEvseConfigurationIndependent) {
    const auto schemas = std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR);
    const auto mappings = dm::load_configuration_mappings(schemas / "configuration_mapping.yaml",
                                                          schemas / "configuration_mapping.schema.json");
    ASSERT_EQ(mappings.size(), 8u);
    FakeConfigService config_service;
    config_service.configs.at({"auth", "Auth"})[Everest::config::MODULE_IMPLEMENTATION_ID].push_back(
        make_parameter("master_pass_group_id", std::string(""), everest::config::Datatype::String,
                       everest::config::Mutability::ReadWrite));
    for (const auto& module_id : {"evse_manager_1", "evse_manager_2"}) {
        auto& parameters =
            config_service.configs[{module_id, "EvseManager"}][Everest::config::MODULE_IMPLEMENTATION_ID];
        for (const auto& parameter : {"payment_enable_contract", "central_contract_validation_allowed",
                                      "contract_certificate_installation_enabled"}) {
            parameters.push_back(make_parameter(parameter, true, everest::config::Datatype::Boolean,
                                                everest::config::Mutability::ReadWrite));
        }
    }
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry, mappings, false);
    EXPECT_NE(registry.find("TxCtrlr", "EVConnectionTimeOut"), nullptr);
    EXPECT_NE(registry.find("AuthCtrlr", "MasterPassGroupId"), nullptr);
    for (const auto& mapping : mappings) {
        const auto& parameter = mapping.parameter.configuration_parameter_name;
        const std::string value = parameter == "connection_timeout"     ? "90"
                                  : parameter == "master_pass_group_id" ? "Managers"
                                                                        : "false";
        EXPECT_EQ(binding->set_value(mapping.component, mapping.variable, AttributeEnum::Actual, value, "csms"),
                  SetVariableStatusEnum::Accepted);
        ASSERT_FALSE(config_service.writes.empty());
        EXPECT_EQ(config_service.writes.back().first.module_id, mapping.parameter.module_id);
        EXPECT_EQ(config_service.writes.back().first.configuration_parameter_name, parameter);
        const auto notifications = registry.notified.size();
        config_service.publish(mapping.parameter.module_id, std::nullopt, parameter, value);
        ASSERT_EQ(registry.notified.size(), notifications + 1);
        EXPECT_EQ(registry.notified.back().component, mapping.component);
        EXPECT_EQ(registry.notified.back().variable, mapping.variable);
        EXPECT_EQ(registry.notified.back().value, value);
    }
    EXPECT_EQ(config_service.writes.size(), mappings.size());
}

TEST(ConfigurationMappingTest, MissingOrInvalidSchemaFailsWithItsPath) {
    const auto work_dir = test_support::create_unique_directory("ocpp_configuration_mapping_invalid_schema");
    const auto schema_path = work_dir / "schema.json";
    const auto mapping_path =
        std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR) / "configuration_mapping.example.yaml";
    const auto expect_failure = [&] {
        try {
            dm::load_configuration_mappings(mapping_path, schema_path);
            ADD_FAILURE() << "Accepted missing or invalid schema";
        } catch (const std::invalid_argument& e) {
            EXPECT_NE(std::string(e.what()).find(schema_path.string()), std::string::npos);
        }
    };
    expect_failure();
    for (const auto& schema : {"{", R"({"type": "not-a-json-schema-type"})"}) {
        std::ofstream(schema_path) << schema;
        expect_failure();
    }
    std::filesystem::remove_all(work_dir);
}

TEST(ConfigurationMappingTest, UsesSuppliedSchemaInsteadOfDocumentReference) {
    const auto work_dir = test_support::create_unique_directory("ocpp_configuration_mapping_supplied_schema");
    const auto schema_path = work_dir / "schema.json";
    const auto mapping_path = work_dir / "mapping.yaml";
    std::ofstream(mapping_path) << R"({"$schema": "missing.schema.json", "mappings": []})";
    std::ofstream(schema_path) << "true";
    EXPECT_TRUE(dm::load_configuration_mappings(mapping_path, schema_path).empty());
    std::ofstream(schema_path) << "false";
    EXPECT_THROW(dm::load_configuration_mappings(mapping_path, schema_path), std::invalid_argument);
    std::filesystem::remove_all(work_dir);
}

TEST(ConfigurationMappingTest, LoaderEnforcesSchemaBeforeConvertingIdentifiers) {
    const auto schema_path =
        std::filesystem::path(CUSTOM_ERROR_MAPPING_SCHEMAS_DIR) / "configuration_mapping.schema.json";
    const auto work_dir = test_support::create_unique_directory("ocpp_configuration_mapping_schema");
    const auto path = work_dir / "mapping.yaml";
    const nlohmann::json valid = {
        {"mappings",
         {{{"everest", {{"module_id", "auth"}, {"parameter", "connection_timeout"}}},
           {"ocpp", {{"component", {{"name", "TxCtrlr"}}}, {"variable", {{"name", "EVConnectionTimeOut"}}}}}}}}};
    const auto write = [&](const nlohmann::json& document) { std::ofstream(path) << document.dump(); };
    auto document = valid;
    document["$schema"] = "configuration_mapping.schema.json";
    write(document);
    EXPECT_EQ(dm::load_configuration_mappings(path, schema_path).size(), 1u);
    write({{"mappings", nlohmann::json::array()}});
    EXPECT_TRUE(dm::load_configuration_mappings(path, schema_path).empty());

    document = valid;
    document["configuration"] = document.at("mappings");
    document.erase("mappings");
    write(document);
    EXPECT_THROW(dm::load_configuration_mappings(path, schema_path), std::invalid_argument);
    for (const auto& [endpoint, legacy] : {std::pair{"everest", "source"}, std::pair{"ocpp", "target"}}) {
        document = valid;
        auto& mapping = document["mappings"][0];
        mapping[legacy] = mapping.at(endpoint);
        mapping.erase(endpoint);
        write(document);
        EXPECT_THROW(dm::load_configuration_mappings(path, schema_path), std::invalid_argument) << legacy;
    }

    const std::vector<std::pair<std::string, nlohmann::json>> invalid_values = {
        {"/mappings", nlohmann::json::object()},
        {"/mappings/0/everest/module_id", ""},
        {"/mappings/0/everest/parameter", false},
        {"/mappings/0/everest/implementation_id", nullptr},
        {"/mappings/0/ocpp/component/name", std::string(51, 'x')},
        {"/mappings/0/ocpp/component/instance", ""},
        {"/mappings/0/ocpp/component/evse", {{"id", 0}}},
        {"/mappings/0/ocpp/component/evse", {{"id", -1}}},
        {"/mappings/0/ocpp/component/evse", {{"id", 2147483648LL}}},
        {"/mappings/0/ocpp/component/evse", {{"id", 1.5}}},
        {"/mappings/0/ocpp/component/evse", {{"id", "1"}}},
        {"/mappings/0/ocpp/component/evse", {{"connectorId", 1}}},
        {"/mappings/0/ocpp/component/evse", {{"id", 1}, {"connectorId", 0}}},
        {"/mappings/0/ocpp/component/evse", {{"id", 1}, {"unknown", true}}},
        {"/mappings/0/ocpp/variable/name", ""},
        {"/mappings/0/ocpp/variable/instance", std::string(51, 'x')},
        {"/mappings/0/ocpp/variable/typo", "bad"},
        {"/mappings/0/ocpp/typo", true},
        {"/mappings/0/typo", true},
        {"/unknown", true},
    };
    for (const auto& [pointer, value] : invalid_values) {
        document = valid;
        document[nlohmann::json::json_pointer(pointer)] = value;
        write(document);
        try {
            dm::load_configuration_mappings(path, schema_path);
            ADD_FAILURE() << "Accepted invalid mapping at " << pointer << ": " << value;
        } catch (const std::invalid_argument& e) {
            EXPECT_NE(std::string(e.what()).find(path.string()), std::string::npos);
        }
    }
    std::filesystem::remove_all(work_dir);
}

TEST(ConfigurationMappingTest, RemovesGlobalPncValuesFromExistingDatabaseAndPreservesScopedValues) {
    const auto work_dir = test_support::create_unique_directory("ocpp_scoped_pnc");
    const auto database = work_dir / "device_model.db";
    auto [component, variables] = ocpp::v2::parse_component_config_from_string(R"({
      "name": "ISO15118Ctrlr", "properties": {
        "PnCEnabled": {"variable_name": "PnCEnabled", "characteristics": {"dataType": "boolean", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "true"}]},
        "CentralContractValidationAllowed": {"variable_name": "CentralContractValidationAllowed", "characteristics": {"dataType": "boolean", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "true"}]},
        "ContractCertificateInstallationEnabled": {"variable_name": "ContractCertificateInstallationEnabled", "characteristics": {"dataType": "boolean", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "true"}]},
        "Enabled": {"variable_name": "Enabled", "characteristics": {"dataType": "boolean", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "true"}]}
      }})");
    auto scoped = component;
    scoped.evse_id = 2;
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> components{{component, variables},
                                                                                            {scoped, variables}};
    {
        ocpp::v2::InitDeviceModelDb db(database, DEVICE_MODEL_MIGRATIONS_DIR);
        db.initialize_database(components, true, false);
    }
    dm::remove_unscoped_pnc_variables(components);
    ASSERT_EQ(components.at(component).size(), 1u);
    EXPECT_EQ(components.at(component).front().name, "Enabled");
    EXPECT_EQ(components.at(scoped).size(), 4u);
    {
        ocpp::v2::InitDeviceModelDb db(database, DEVICE_MODEL_MIGRATIONS_DIR);
        db.initialize_database(components, false, false);
    }
    ocpp::v2::DeviceModel model(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(database));
    for (const auto& name :
         {"PnCEnabled", "CentralContractValidationAllowed", "ContractCertificateInstallationEnabled"}) {
        EXPECT_FALSE(model.get_variable_meta_data({"ISO15118Ctrlr"}, {name}));
        const auto cv = ocpp::v2::ISO15118ComponentVariables::get_component_variable(2, {name});
        EXPECT_TRUE(model.get_variable_meta_data(cv.component, *cv.variable));
    }
    FakeConfigService config_service;
    auto mapping = timeout_mapping();
    mapping.component.name = "ISO15118Ctrlr";
    mapping.variable.name = "PnCEnabled";
    EXPECT_THROW(dm::build_configuration_views(config_service.configs, {}, {mapping}, false), std::invalid_argument);
    std::filesystem::remove_all(work_dir);
}

TEST(ConfigurationMappingTest, RemovesOnlyCompatibleExplicitTargetsFromComponentConfig) {
    FakeConfigService config_service;
    auto [component, variables] = ocpp::v2::parse_component_config_from_string(R"({
      "name": "TxCtrlr", "properties": {
        "EVConnectionTimeOut": {
          "variable_name": "EVConnectionTimeOut",
          "characteristics": {"dataType": "integer", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "30"}]
        }
      }
    })");
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> components{{component, variables}};
    const auto mapping = timeout_mapping();
    components.at(component).front().characteristics.dataType = ocpp::v2::DataEnum::string;
    EXPECT_THROW(dm::remove_mapped_configuration_variables(components, {mapping}, config_service.configs),
                 std::invalid_argument);
    EXPECT_EQ(components.at(component).size(), 1u);
    components.at(component) = variables;
    components.at(component).front().attributes.clear();
    EXPECT_THROW(dm::remove_mapped_configuration_variables(components, {mapping}, config_service.configs),
                 std::invalid_argument);
    components.at(component) = variables;
    dm::remove_mapped_configuration_variables(components, {mapping}, config_service.configs);
    EXPECT_TRUE(components.at(component).empty());
    EXPECT_EQ(components.size(), 1u);
    dm::remove_mapped_configuration_variables(components, {mapping}, config_service.configs);
}

TEST(ConfigurationMappingTest, ValuesReportsAndMonitorsUseConfigServiceAcrossRestarts) {
    const auto work_dir = test_support::create_unique_directory("ocpp_mapped_config");
    const auto db_path = work_dir / "device_model.db";
    const auto mapping = timeout_mapping();
    FakeConfigService config_service;
    auto [component, variables] = ocpp::v2::parse_component_config_from_string(R"({
      "name": "TxCtrlr", "properties": {
        "EVConnectionTimeOut": {
          "variable_name": "EVConnectionTimeOut",
          "characteristics": {"dataType": "integer", "supportsMonitoring": true},
          "attributes": [{"type": "Actual", "mutability": "ReadWrite", "value": "30"}]
        }
      }
    })");
    std::map<ocpp::v2::ComponentKey, std::vector<ocpp::v2::DeviceModelVariable>> components{{component, variables}};
    {
        ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
        init_db.initialize_database(components, true, false);
    }
    {
        ocpp::v2::DeviceModelStorageSqlite storage(db_path);
        EXPECT_EQ(storage.set_variable_attribute_value(mapping.component, mapping.variable, AttributeEnum::Actual,
                                                       "200", "csms"),
                  SetVariableStatusEnum::Accepted);
    }
    dm::remove_mapped_configuration_variables(components, {mapping}, config_service.configs);
    {
        ocpp::v2::InitDeviceModelDb init_db(db_path, DEVICE_MODEL_MIGRATIONS_DIR);
        init_db.initialize_database(components, false, false);
    }
    std::optional<std::int64_t> monitor_id;
    for (int boot = 0; boot < 2; ++boot) {
        ocpp::v2::DeviceModel device_model(std::make_unique<ocpp::v2::DeviceModelStorageSqlite>(db_path));
        const auto binding = dm::ConfigServiceBinding::create(config_service, device_model, {mapping}, false);
        ASSERT_TRUE(binding->registration_result().rejected.empty());
        std::string value;
        ASSERT_EQ(device_model.get_variable(mapping.component, mapping.variable, AttributeEnum::Actual, value, false,
                                            ocpp::v2::UnsetValue::NotSupported),
                  ocpp::v2::GetVariableStatusEnum::Accepted);
        EXPECT_EQ(value, "60");
        const auto report = device_model.get_base_report_data(ocpp::v2::ReportBaseEnum::FullInventory);
        ASSERT_EQ(report.size(), 1u);
        EXPECT_EQ(report.front().variableAttribute.front().value, "60");

        if (boot == 0) {
            ocpp::v2::SetMonitoringData monitor;
            monitor.component = mapping.component;
            monitor.variable = mapping.variable;
            monitor.type = ocpp::v2::MonitorEnum::Delta;
            monitor.value = 10;
            monitor.severity = 3;
            const auto results = device_model.set_monitors({monitor});
            ASSERT_EQ(results.size(), 1u);
            ASSERT_EQ(results.front().status, ocpp::v2::SetMonitoringStatusEnum::Accepted);
            monitor_id = results.front().id;
        } else {
            const auto metadata = device_model.get_variable_meta_data(mapping.component, mapping.variable);
            ASSERT_TRUE(metadata.has_value());
            EXPECT_EQ(metadata->monitors.count(monitor_id.value()), 1u);
        }
        int notifications = 0;
        device_model.register_variable_listener([&](const auto& monitors, const auto&, const auto&, const auto&,
                                                    const auto&, const auto& previous, const auto& current) {
            ++notifications;
            EXPECT_EQ(monitors.count(monitor_id.value()), 1u);
            EXPECT_EQ(previous, "60");
            EXPECT_EQ(current, "120");
        });
        config_service.publish("auth", std::nullopt, "connection_timeout", "120");
        EXPECT_EQ(notifications, 1);
        EXPECT_TRUE(config_service.writes.empty());
        config_service.result.set_status = everest::config::SetConfigStatus::RebootRequired;
        EXPECT_EQ(device_model.set_value(mapping.component, mapping.variable, AttributeEnum::Actual, "150", "csms"),
                  SetVariableStatusEnum::RebootRequired);
        config_service.writes.clear();
        device_model.get_variable(mapping.component, mapping.variable, AttributeEnum::Actual, value, false,
                                  ocpp::v2::UnsetValue::NotSupported);
        EXPECT_EQ(value, "120");
        binding->stop();
        ocpp::v2::DeviceModelStorageSqlite storage(db_path);
        EXPECT_EQ(storage.get_device_model().count(mapping.component), 0u);
        EXPECT_TRUE(storage.get_stored_values().empty());
    }
    std::filesystem::remove_all(work_dir);
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
    EXPECT_EQ(registry.notified.at(0).owner, dm::VARIABLE_OWNER_EVEREST_CONFIG);
    EXPECT_EQ(registry.notified.at(1).variable, make_variable("main.secret"));
}

TEST(ConfigServiceBindingTest, ChangeThatWaitsForARestartIsNotReported) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);

    config_service.publish("auth", std::nullopt, "connection_timeout", "120",
                           Everest::config::SetResponseStatus::RebootRequired);

    EXPECT_TRUE(registry.notified.empty());
}

TEST(ConfigServiceBindingTest, StopUnregistersAndEndsNotifications) {
    FakeConfigService config_service;
    FakeRegistry registry;
    const auto binding = dm::ConfigServiceBinding::create(config_service, registry);
    EXPECT_GT(config_service.handler_count(), 0u);
    binding->stop();
    EXPECT_EQ(registry.unregistered, std::vector<std::string>{dm::VARIABLE_OWNER_EVEREST_CONFIG});
    EXPECT_EQ(config_service.handler_count(), 0u);

    config_service.publish("auth", std::nullopt, "connection_timeout", "120");
    EXPECT_TRUE(registry.notified.empty());
}

// The DeviceModel shares the ownership of the binding until stop(), so a binding that was not stopped is destroyed
// together with the DeviceModel, which the destructor must not call back into
TEST(ConfigServiceBindingTest, DestructionUnregistersTheHandlersButNotTheVariables) {
    FakeConfigService config_service;
    FakeRegistry registry;
    dm::ConfigServiceBinding::create(config_service, registry).reset();

    EXPECT_EQ(config_service.handler_count(), 0u);
    EXPECT_TRUE(registry.unregistered.empty());
}

TEST(ConfigServiceBindingTest, DestructionAfterStopUnregistersOnce) {
    FakeConfigService config_service;
    FakeRegistry registry;
    auto binding = dm::ConfigServiceBinding::create(config_service, registry);
    binding->stop();
    binding.reset();

    EXPECT_EQ(config_service.handler_count(), 0u);
    EXPECT_EQ(registry.unregistered, std::vector<std::string>{dm::VARIABLE_OWNER_EVEREST_CONFIG});
}

TEST(DeviceModelRegistrationsTest, EndStopsTheConfigurationViews) {
    FakeConfigService config_service;
    FakeRegistry registry;
    dm::DeviceModelRegistrations registrations;
    registrations.register_config_service(config_service, registry);
    ASSERT_EQ(registrations.results().size(), 1u);
    EXPECT_EQ(registrations.results().front().owner, dm::VARIABLE_OWNER_EVEREST_CONFIG);

    registrations.end();

    EXPECT_EQ(config_service.handler_count(), 0u);
    EXPECT_EQ(registry.unregistered, std::vector<std::string>{dm::VARIABLE_OWNER_EVEREST_CONFIG});
    EXPECT_TRUE(registrations.results().empty());
    registrations.end();
    EXPECT_EQ(registry.unregistered.size(), 1u);
}

TEST(DeviceModelRegistrationsTest, DestructionAndReplacementEndTheRegistrations) {
    FakeConfigService config_service;
    FakeRegistry registry;
    {
        dm::DeviceModelRegistrations registrations;
        registrations.register_config_service(config_service, registry);
        dm::DeviceModelRegistrations moved = std::move(registrations);
        EXPECT_GT(config_service.handler_count(), 0u);
        moved = dm::DeviceModelRegistrations{};
        EXPECT_EQ(config_service.handler_count(), 0u);
        EXPECT_EQ(registry.unregistered.size(), 1u);
        moved.register_config_service(config_service, registry);
    }
    EXPECT_EQ(config_service.handler_count(), 0u);
    EXPECT_EQ(registry.unregistered.size(), 2u);
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
    EXPECT_EQ(registry.notified.at(0).owner, dm::VARIABLE_OWNER_EVEREST_CONFIG);
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

TEST(ConfigServiceBindingTest, ModuleIdsThatDifferOnlyInCaseAreRejected) {
    FakeConfigService config_service;
    config_service.configs[{"AUTH", "Auth"}] = config_service.configs.at({"auth", "Auth"});
    config_service.mappings["AUTH"] = ModuleTierMappings{};
    FakeRegistry registry;

    EXPECT_THROW(dm::ConfigServiceBinding::create(config_service, registry), std::runtime_error);
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
    const auto work_dir = test_support::create_unique_directory("ocpp_module_common_config_service_binding");
    const auto db_path = work_dir / "device_model.db";
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

    // The local channel writes a ReadWrite view through the configuration service as well
    EXPECT_EQ(device_model.set_local_value(auth, timeout, AttributeEnum::Actual, "80", "api"),
              SetVariableStatusEnum::Accepted);
    EXPECT_EQ(config_service.writes.size(), 2u);
    device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false, ocpp::v2::UnsetValue::NotSupported);
    EXPECT_EQ(value, "80");

    // Applied by the module at its next start: the device model keeps the value in effect
    config_service.result.set_status = everest::config::SetConfigStatus::RebootRequired;
    EXPECT_EQ(device_model.set_value(auth, timeout, AttributeEnum::Actual, "100", "csms"),
              SetVariableStatusEnum::RebootRequired);
    EXPECT_EQ(config_service.writes.size(), 3u);
    device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false, ocpp::v2::UnsetValue::NotSupported);
    EXPECT_EQ(value, "80");

    // WriteOnly values are never read back
    EXPECT_EQ(device_model.get_variable(auth, make_variable("main.secret"), AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::Rejected);

    binding->stop();
    EXPECT_EQ(device_model.get_variable(auth, timeout, AttributeEnum::Actual, value, false,
                                        ocpp::v2::UnsetValue::NotSupported),
              ocpp::v2::GetVariableStatusEnum::UnknownComponent);
    std::filesystem::remove_all(work_dir);
}
