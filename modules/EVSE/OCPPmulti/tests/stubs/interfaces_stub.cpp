// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "interfaces_stub.hpp"
#include <everest/logging.hpp>
#include <utils/error/error_database.hpp>
#include <utils/error/error_manager_impl.hpp>
#include <utils/error/error_manager_req.hpp>
#include <utils/error/error_manager_req_global.hpp>

#include <memory>

namespace {

// if "[info]    Type <type name> is not known, ignore subscription"
// is seen then add <type name> to the known_errors map

// type, description
const std::map<Everest::error::ErrorType, std::string> known_errors{
    {"evse_manager/Inoperative", "EVSE manager Inoperative"}};
} // namespace

namespace stubs {

std::optional<json> ModuleAdapter::auth_call_set_connection_timeout(const Requirement& req, const json& args) {
    // auth interface
    // set_connection_timeout:
    //   description: Sets the connection timeout
    //   arguments:
    //     connection_timeout:
    //       description: Connection timeout in seconds
    //       type: integer
    //       minimum: 10
    //       maximum: 300

    EVLOG_debug << "Call set_connection_timeout: " << args.dump();
    return {};
}

std::optional<json> ModuleAdapter::auth_call_set_master_pass_group_id(const Requirement& req, const json& args) {
    // auth interface
    // set_master_pass_group_id:
    //   description: >-
    //     Sets the master pass group id. IdTokens that have this id as parent_id_token belong to the Master Pass Group.
    //     This means they can stop any ongoing transaction, but cannot start transactions. This can, for example, be
    //     used by law enforcement personal to stop any ongoing transaction when an EV has to be towed away. If
    //     master_pass_group_id is an empty string, it is not used.
    //   arguments:
    //     master_pass_group_id:
    //       description: The master pass group id
    //       type: string
    //       maxLength: 36

    EVLOG_debug << "Call set_master_pass_group_id: " << args.dump();
    return {};
}

std::optional<json> ModuleAdapter::evse_manager_call_external_ready_to_start_charging(const Requirement& req,
                                                                                      const json& args) {
    // evse_manager interface
    // external_ready_to_start_charging:
    //   description: >-
    //     There are situations where another module needs to do some initialization after evse manager is in principle
    //     ready to start charging. This command can be used (optimally in combination with a configuration option) to
    //     delay charging ready until the external module is done with its initialization
    //   result:
    //     description: Returns true if the signal was used by the evse manager implementation
    //     type: boolean

    EVLOG_debug << "Call external_ready_to_start_charging: " << args.dump();
    json::value_type result = true;
    EVLOG_debug << "result:                                " << result.dump();
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_get_evse(const Requirement& req, const json& args) {
    // evse_manager interface
    // get_evse - no arguments
    // response - /evse_manager#/Evse
    //
    // Evse:
    //   description: Type that defines properties of an EVSE including its connectors
    //   type: object
    //   required:
    //     - id
    //     - connectors
    //   properties:
    //     id:
    //       description: ID of the EVSE
    //       type: integer
    //       minimum: 1
    //       maximum: 128
    //     connectors:
    //       description: List of connectors of this EVSE
    //       type: array
    //       items:
    //         description: A single connector
    //         type: object
    //         $ref: /evse_manager#/Connector
    //       minItems: 1
    //       maxItems: 128
    //
    // Connector:
    //   description: >-
    //     Type for a connector which is an independently operated and managed electrical outlet of an EVSE. It
    //     corresponds to a single physical connector
    //   type: object
    //   additionalProperties: false
    //   required:
    //     - id
    //   properties:
    //     id:
    //       description: Id of the connector. Connectors should be numbered starting with 1 counting upwards
    //       type: integer
    //       minimum: 1
    //     type:
    //       description: Type of the connector
    //       type: string
    //       $ref: /evse_manager#/ConnectorTypeEnum

    EVLOG_debug << "Call get_evse: " << args.dump();
    json result = R"(
    {
        "id": 0,
        "connectors":[
            {"id":1}
        ]
    }
    )"_json;
    result["id"] = req.index + 1;
    EVLOG_debug << "result:        " << result.dump();
    return result;
}

std::optional<json> ModuleAdapter::evse_manager_call_set_plug_and_charge_configuration(const Requirement& req,
                                                                                       const json& args) {
    // evse_manager interface
    // set_plug_and_charge_configuration:
    //   description: >-
    //     Sets the configuration required for ISO15118 to handle contract authorization.
    //   arguments:
    //     plug_and_charge_configuration:
    //       description: The plug and charge configuration object
    //       type: object
    //       $ref: /evse_manager#/PlugAndChargeConfiguration
    EVLOG_debug << "Call call_set_plug_and_charge_configuration: " << args.dump();
    return {};
}

std::optional<json> ModuleAdapter::system_call_get_boot_reason(const Requirement& req, const json& args) {
    // system interface
    // get_boot_reason:
    //   description: Call to get the boot reason of the system
    //   result:
    //     description: Returns the boot reason of the system
    //     type: string
    //     $ref: /system#/BootReason
    //
    // BootReason:
    //   description: Enum defining the boot reason
    //   type: string
    //   enum:
    //     - ApplicationReset
    //     - FirmwareUpdate
    //     - LocalReset
    //     - PowerUp
    //     - RemoteReset
    //     - ScheduledReset
    //     - Triggered
    //     - Unknown
    //     - Watchdog

    EVLOG_debug << "Call get_boot_reason: " << args.dump();
    json::value_type result = "PowerUp";
    EVLOG_debug << "result:               " << result.dump();
    return result;
}

void ModuleAdapter::error_raised(const Everest::error::Error& error) {
    EVLOG_debug << "publish_raised_error";
}

void ModuleAdapter::error_cleared(const Everest::error::Error& error) {
    EVLOG_debug << "publish_cleared_error";
}

void ModuleAdapter::subscribe_error(const Everest::error::ErrorType& type, const Everest::error::ErrorCallback&,
                                    const Everest::error::ErrorCallback&) {
    EVLOG_debug << "Subscribe error: " << type;
}

ModuleAdapter::ModuleAdapter() : m_error_type_map(std::make_shared<Everest::error::ErrorTypeMap>()) {
    m_error_type_map->load_error_types_map(known_errors);
    for (const auto& item : known_errors) {
        m_supported_errors.push_back(item.first);
    }

    m_error_manager = std::make_shared<Everest::error::ErrorManagerImpl>(
        m_error_type_map, m_error_database, m_supported_errors,
        [this](const Everest::error::Error& error) { error_raised(error); },
        [this](const Everest::error::Error& error) { error_cleared(error); });

    m_error_manager_req = std::make_shared<Everest::error::ErrorManagerReq>(
        m_error_type_map, m_error_database, m_supported_errors,
        [this](const Everest::error::ErrorType& type, const Everest::error::ErrorCallback& raised,
               const Everest::error::ErrorCallback& cleared) { subscribe_error(type, raised, cleared); });

    m_error_manager_req_global = std::make_shared<Everest::error::ErrorManagerReqGlobal>(
        m_error_type_map, m_error_database, [this](auto&&... args) {});

    m_mqtt_abstraction = std::make_shared<MqttStub>();
    m_config_client =
        std::make_shared<Everest::config::ConfigServiceClient>(m_mqtt_abstraction, "ocpp", m_module_names);

    m_call_implementations.insert(
        {"external_ready_to_start_charging", &ModuleAdapter::evse_manager_call_external_ready_to_start_charging});
    m_call_implementations.insert({"get_boot_reason", &ModuleAdapter::system_call_get_boot_reason});
    m_call_implementations.insert({"get_evse", &ModuleAdapter::evse_manager_call_get_evse});
    m_call_implementations.insert({"set_connection_timeout", &ModuleAdapter::auth_call_set_connection_timeout});
    m_call_implementations.insert({"set_master_pass_group_id", &ModuleAdapter::auth_call_set_master_pass_group_id});
    m_call_implementations.insert(
        {"set_plug_and_charge_configuration", &ModuleAdapter::evse_manager_call_set_plug_and_charge_configuration});
}

Result ModuleAdapter::call_fn(const Requirement& req, const std::string& fn, Parameters args) {
    Result result;
    if (const auto it = m_call_implementations.find(fn); it != m_call_implementations.end()) {
        result = std::invoke(it->second, this, std::cref(req), std::cref(args));
    } else {
        EVLOG_warning << "Call <not implemented>: " << fn << ": " << args;
    }
    return result;
}

void ModuleAdapter::publish_fn(const std::string&, const std::string&, Value) {
    EVLOG_debug << "publish_fn";
}

void ModuleAdapter::subscribe_fn(const Requirement& req, const std::string& fn, ValueCallback callback) {
    const auto topic = to_topic(req, fn);
    EVLOG_debug << "Subscribe: " << topic;
    m_callbacks[topic] = std::move(callback);
}

std::shared_ptr<Everest::error::ErrorManagerImpl> ModuleAdapter::get_error_manager_impl_fn(const std::string&) {
    EVLOG_debug << "get_error_manager_impl_fn called";
    return m_error_manager;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_error_state_monitor_impl_fn(const std::string&) {
    EVLOG_debug << "get_error_state_monitor_impl_fn called";
    return std::make_shared<Everest::error::ErrorStateMonitor>(m_error_database);
}

std::shared_ptr<Everest::error::ErrorManagerReqGlobal> ModuleAdapter::get_global_error_manager_fn() {
    EVLOG_debug << "get_global_error_manager_fn called";
    return m_error_manager_req_global;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_global_error_state_monitor_fn() {
    EVLOG_debug << "get_global_error_state_monitor_fn called";
    return {};
}

std::shared_ptr<Everest::error::ErrorFactory> ModuleAdapter::get_error_factory_fn(const std::string&) {
    EVLOG_debug << "get_error_factory_fn called";
    return std::make_shared<Everest::error::ErrorFactory>(std::make_shared<Everest::error::ErrorTypeMap>());
}

std::shared_ptr<Everest::error::ErrorManagerReq> ModuleAdapter::get_error_manager_req_fn(const Requirement&) {
    EVLOG_debug << "get_error_manager_req_fn called";
    return m_error_manager_req;
}

std::shared_ptr<Everest::error::ErrorStateMonitor> ModuleAdapter::get_error_state_monitor_req_fn(const Requirement&) {
    EVLOG_debug << "get_error_state_monitor_req_fn called";
    return std::make_shared<Everest::error::ErrorStateMonitor>(Everest::error::ErrorStateMonitor(m_error_database));
}

void ModuleAdapter::ext_mqtt_publish_fn(const std::string&, const std::string&) {
    EVLOG_debug << "ext_mqtt_publish_fn called";
}

std::function<void()> ModuleAdapter::ext_mqtt_subscribe_fn(const std::string&, StringHandler) {
    EVLOG_debug << "ext_mqtt_subscribe_fn called";
    return nullptr;
}

std::function<void()> ModuleAdapter::ext_mqtt_subscribe_pair_fn(const std::string& topic,
                                                                const StringPairHandler& handler) {
    EVLOG_debug << "ext_mqtt_subscribe_pair_fn called";
    return {};
}

void ModuleAdapter::telemetry_publish_fn(const std::string&, const std::string&, const std::string&,
                                         const Everest::TelemetryMap&) {
    EVLOG_debug << "telemetry_publish_fn called";
}

std::optional<ModuleTierMappings> ModuleAdapter::get_mapping_fn() {
    EVLOG_debug << "get_mapping_fn called";
    return {};
}

std::shared_ptr<Everest::config::ConfigServiceClient> ModuleAdapter::get_config_service_client_fn() {
    EVLOG_debug << "get_config_service_client_fn called";
    return m_config_client;
}

void ModuleAdapter::publish(const Requirement& req, const std::string& fn, json args) {
    const auto topic = to_topic(req, fn);
    if (auto it = m_callbacks.find(topic); it != m_callbacks.end()) {
        EVLOG_debug << "Publish: " << topic << ": " << args;
        it->second(std::move(args));
    } else {
        EVLOG_warning << "Publish: " << topic << ": <not subscribed>";
    }
}

} // namespace stubs
