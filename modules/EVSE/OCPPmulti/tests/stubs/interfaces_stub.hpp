// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ModuleAdapterStub.hpp>
#include <OCPPmulti.hpp>
#include <string_view>
#include <utils/error/error_manager_req_global.hpp>
#include <utils/mqtt_abstraction.hpp>
#include <utils/types.hpp>

#include <nlohmann/json.hpp>

#include <list>
#include <memory>
#include <mutex>
#include <string>

namespace stubs {

struct MqttStub : public Everest::MQTTAbstraction {
    using QOS = Everest::QOS;
    using MQTTRequest = Everest::MQTTRequest;

    std::string m_everest_prefix{"everest"};
    std::string m_external_prefix{"external"};

    bool connect() override {
        return true;
    }
    void disconnect() override {
    }
    void stop_message_handling() override {
    }
    void publish(const std::string& topic, const nlohmann::json& json) override {
    }
    void publish(const std::string& topic, const nlohmann::json& json, QOS qos, bool retain = false) override {
    }
    void publish(const std::string& topic, const std::string& data) override {
    }
    void publish(const std::string& topic, const std::string& data, QOS qos, bool retain = false) override {
    }
    void subscribe(const std::string& topic) override {
    }
    void subscribe(const std::string& topic, QOS qos) override {
    }
    void unsubscribe(const std::string& topic) override {
    }
    void clear_retained_topics() override {
    }
    nlohmann::json get(const std::string& topic, QOS qos, std::size_t retries = 0) override {
        return {};
    }
    nlohmann::json get(const MQTTRequest& request, std::size_t retries = 0) override {
        return {};
    }
    const std::string& get_everest_prefix() const override {
        return m_everest_prefix;
    }
    const std::string& get_external_prefix() const override {
        return m_external_prefix;
    }
    std::shared_future<void> spawn_main_loop_thread() override {
        return {};
    }
    std::shared_future<void> get_main_loop_future() override {
        return {};
    }
    void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS qos) override {
    }
    void unregister_handler(const std::string& topic, const Token& token) override {
    }
};

class ModuleAdapter : public module::stub::ModuleAdapterStub {
public:
    using var_cb_t =
        std::function<void(const std::string_view& interface, const std::string_view& variable, const json& data)>;

private:
    using var_cb_list_t = std::list<std::pair<std::string, var_cb_t>>;
    using call_function_t = std::optional<json> (ModuleAdapter::*)(const Requirement&, const json& args);

    std::list<Everest::error::ErrorType> m_supported_errors;
    std::shared_ptr<Everest::error::ErrorTypeMap> m_error_type_map;
    std::list<std::pair<std::string, ValueCallback>> m_callbacks; // there can be multiple subscribes to a topic
    std::shared_ptr<Everest::error::ErrorDatabaseMap> m_error_database;
    std::shared_ptr<Everest::error::ErrorManagerImpl> m_error_manager;
    std::shared_ptr<Everest::error::ErrorManagerReq> m_error_manager_req;
    std::shared_ptr<Everest::error::ErrorManagerReqGlobal> m_error_manager_req_global;
    std::map<std::string, call_function_t> m_call_implementations;
    std::shared_ptr<Everest::MQTTAbstraction> m_mqtt_abstraction;
    std::shared_ptr<Everest::config::ConfigServiceClient> m_config_client;
    std::map<std::string, std::string, std::less<>> m_module_names;

    static std::string to_topic(const Requirement& req, const std::string& fn) {
        std::string result = req.id;
        result.push_back('/');
        result += std::to_string(req.index);
        result.push_back('/');
        result += fn;
        return result;
    }

    static std::string to_key(const std::string_view& interface, const std::string_view& variable, int instance) {
        std::string result{interface};
        result.push_back('/');
        if (instance >= 0) {
            result += std::to_string(instance);
            result.push_back('/');
        }
        result += variable;
        return result;
    }

    std::optional<json> get_cmd_response(const std::optional<json>& default_response);
    void publish_fn(const std::string& interface, const std::string& variable, int instance, const json& value);

    var_cb_list_t m_subscribe_var_callbacks;
    // guarded by m_cmd_response_mutex: commands may be invoked from module threads concurrently
    std::mutex m_cmd_response_mutex;
    std::list<json> m_cmd_response_list;

protected:
    // ------------------------------------------------------------------------
    // supported interface calls

    virtual std::optional<json> auth_call_set_connection_timeout(const Requirement& req, const json& args);
    virtual std::optional<json> auth_call_set_master_pass_group_id(const Requirement& req, const json& args);
    virtual std::optional<json> charger_information_call_get_charger_information(const Requirement& req,
                                                                                 const json& args);
    virtual std::optional<json> display_message_call_clear_display_message(const Requirement& req, const json& args);
    virtual std::optional<json> display_message_call_get_display_messages(const Requirement& req, const json& args);
    virtual std::optional<json> display_message_call_set_display_message(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_enable_disable(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_external_ready_to_start_charging(const Requirement& req,
                                                                                   const json& args);
    virtual std::optional<json> evse_manager_call_force_unlock(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_get_evse(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_pause_charging(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_resume_charging(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_set_der_available(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_set_plug_and_charge_configuration(const Requirement& req,
                                                                                    const json& args);
    virtual std::optional<json> evse_manager_call_stop_transaction(const Requirement& req, const json& args);
    virtual std::optional<json> evse_manager_call_update_allowed_energy_transfer_modes(const Requirement& req,
                                                                                       const json& args);
    virtual std::optional<json> external_energy_limits_call_set_external_limits(const Requirement& req,
                                                                                const json& args);
    virtual std::optional<json> iso15118_extensions_call_set_get_certificate_response(const Requirement& req,
                                                                                      const json& args);
    virtual std::optional<json> ocpp_data_transfer_call_data_transfer(const Requirement& req, const json& args);
    virtual std::optional<json> reservation_call_cancel_reservation(const Requirement& req, const json& args);
    virtual std::optional<json> reservation_call_exists_reservation(const Requirement& req, const json& args);
    virtual std::optional<json> reservation_call_reserve_now(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_allow_firmware_installation(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_configure_network(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_get_boot_reason(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_is_reset_allowed(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_reset(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_set_system_time(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_update_firmware(const Requirement& req, const json& args);
    virtual std::optional<json> system_call_upload_logs(const Requirement& req, const json& args);

    // ------------------------------------------------------------------------
    // callbacks

    void error_raised(const Everest::error::Error& error);
    void error_cleared(const Everest::error::Error& error);
    void subscribe_error(const Everest::error::ErrorType& type, const Everest::error::ErrorCallback&,
                         const Everest::error::ErrorCallback&);

public:
    ModuleAdapter();

    // ------------------------------------------------------------------------
    // overrides from ModuleAdapterStub

    Result call_fn(const Requirement&, const std::string& fn, Parameters args) override;
    void publish_fn(const std::string&, const std::string&, Value) override;
    void subscribe_fn(const Requirement& req, const std::string& fn, ValueCallback callback) override;
    std::shared_ptr<Everest::error::ErrorManagerImpl> get_error_manager_impl_fn(const std::string&) override;
    std::shared_ptr<Everest::error::ErrorStateMonitor> get_error_state_monitor_impl_fn(const std::string&) override;
    std::shared_ptr<Everest::error::ErrorManagerReqGlobal> get_global_error_manager_fn() override;
    std::shared_ptr<Everest::error::ErrorStateMonitor> get_global_error_state_monitor_fn() override;
    std::shared_ptr<Everest::error::ErrorFactory> get_error_factory_fn(const std::string&) override;
    std::shared_ptr<Everest::error::ErrorManagerReq> get_error_manager_req_fn(const Requirement&) override;
    std::shared_ptr<Everest::error::ErrorStateMonitor> get_error_state_monitor_req_fn(const Requirement&) override;
    void ext_mqtt_publish_fn(const std::string&, const std::string&) override;
    std::function<void()> ext_mqtt_subscribe_fn(const std::string&, StringHandler) override;
    std::function<void()> ext_mqtt_subscribe_pair_fn(const std::string& topic,
                                                     const StringPairHandler& handler) override;
    void telemetry_publish_fn(const std::string&, const std::string&, const std::string&,
                              const Everest::TelemetryMap&) override;
    std::optional<ModuleTierMappings> get_mapping_fn() override;
    std::shared_ptr<Everest::config::ConfigServiceClient> get_config_service_client_fn() override;

    // ------------------------------------------------------------------------
    // helper functions

    // publish to OCPP module required interfaces
    void publish(const Requirement& req, const std::string& fn, json args);

    // add to list of cmd responses
    void add_cmd_result(const json& data);

    // subscribe to variables published on the interface (see publish_fn)
    void subscribe_var(const std::string_view& interface, const std::string_view& variable, int instance, var_cb_t cb);
};

struct auth_token_validatorImplStub : public auth_token_validatorImplBase {
    using auth_token_validatorImplBase::auth_token_validatorImplBase;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, ready, (), (override));

    MOCK_METHOD(types::authorization::ValidationResult, handle_validate_token,
                (types::authorization::ProvidedIdToken & provided_token), (override));
};

struct auth_token_providerImplStub : public auth_token_providerImplBase {
    using auth_token_providerImplBase::auth_token_providerImplBase;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, ready, (), (override));
};

struct ocpp_data_transferImplStub : public ocpp_data_transferImplBase {
    using ocpp_data_transferImplBase::ocpp_data_transferImplBase;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, ready, (), (override));

    MOCK_METHOD(types::ocpp::DataTransferResponse, handle_data_transfer, (types::ocpp::DataTransferRequest & request),
                (override));
};

struct ocppImplStub : public ocppImplBase {
    using ocppImplBase::ocppImplBase;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, ready, (), (override));

    MOCK_METHOD(bool, handle_stop, (), (override));
    MOCK_METHOD(bool, handle_restart, (), (override));
    MOCK_METHOD(void, handle_security_event, (types::ocpp::SecurityEvent & event), (override));
    MOCK_METHOD(std::vector<types::ocpp::GetVariableResult>, handle_get_variables,
                (std::vector<types::ocpp::GetVariableRequest> & requests), (override));
    MOCK_METHOD(std::vector<types::ocpp::SetVariableResult>, handle_set_variables,
                (std::vector<types::ocpp::SetVariableRequest> & requests, std::string& source), (override));
    MOCK_METHOD(types::ocpp::ChangeAvailabilityResponse, handle_change_availability,
                (types::ocpp::ChangeAvailabilityRequest & request), (override));
    MOCK_METHOD(void, handle_monitor_variables, (std::vector<types::ocpp::ComponentVariable> & component_variables),
                (override));
};

struct session_costImplStub : public session_costImplBase {
    using session_costImplBase::session_costImplBase;

    MOCK_METHOD(void, init, (), (override));
    MOCK_METHOD(void, ready, (), (override));
};

class ModuleInterfaces {
private:
    ModuleAdapter m_adapter;
    Requirement m_requirement{"ocpp", 0};
    ModuleInfo m_module_info{"name", {/*authors*/}, "Apache-2.0", "ocpp", {"./etc", "./libexec", "./OCPP201"},
                             false,  false,         std::nullopt};

public:
    using provides_t = ocpp_multi::GenericOcppInterface::provides_t;
    using requires_t = ocpp_multi::GenericOcppInterface::requires_t;
    using var_cb_t = ModuleAdapter::var_cb_t;

    auth_token_validatorImplStub p_auth_validator{&m_adapter, "auth_validator"};
    auth_token_providerImplStub p_auth_provider{&m_adapter, "auth_provider"};
    ocpp_data_transferImplStub p_data_transfer{&m_adapter, "data_transfer"};
    ocppImplStub p_ocpp_generic{&m_adapter, "ocpp_generic"};
    session_costImplStub p_session_cost{&m_adapter, "session_cost"};

    authIntf r_auth{&m_adapter, m_requirement, "auth", std::nullopt};
    std::vector<std::unique_ptr<charger_informationIntf>> r_charger_information;
    std::vector<std::unique_ptr<ocpp_data_transferIntf>> r_data_transfer;
    std::vector<std::unique_ptr<display_messageIntf>> r_display_message;
    std::vector<std::unique_ptr<external_energy_limitsIntf>> r_evse_energy_sink;
    std::vector<std::unique_ptr<evse_managerIntf>> r_evse_manager;
    std::vector<std::unique_ptr<iso15118_extensionsIntf>> r_extensions_15118;
    std::vector<std::unique_ptr<grid_supportIntf>> r_grid_support;
    std::vector<std::unique_ptr<reservationIntf>> r_reservation;
    evse_securityIntf r_security{&m_adapter, m_requirement, "security", std::nullopt};
    systemIntf r_system{&m_adapter, m_requirement, "system", std::nullopt};

    const provides_t& get_provides() const {
        return m_provides;
    }

    const requires_t& get_requires() const {
        return m_requires;
    }

    const ModuleInfo& get_module_info() const {
        return m_module_info;
    }

    void add_charger_information(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_charger_information.size();
        r_charger_information.emplace_back(
            std::make_unique<charger_informationIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    void add_data_transfer(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_data_transfer.size();
        r_data_transfer.emplace_back(
            std::make_unique<ocpp_data_transferIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    void add_display_message(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_display_message.size();
        r_display_message.emplace_back(std::make_unique<display_messageIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    void add_evse_energy_sink(const std::string& module_id, int evse) {
        auto req = m_requirement;
        req.index = r_evse_energy_sink.size();
        r_evse_energy_sink.emplace_back(
            std::make_unique<external_energy_limitsIntf>(&m_adapter, req, module_id, Mapping{evse}));
    }

    void add_evse_manager(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_evse_manager.size();
        r_evse_manager.emplace_back(std::make_unique<evse_managerIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    void add_extensions_15118(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_extensions_15118.size();
        r_extensions_15118.emplace_back(
            std::make_unique<iso15118_extensionsIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    void add_grid_support(const std::string& module_id, int evse) {
        auto req = m_requirement;
        req.index = r_grid_support.size();
        r_grid_support.emplace_back(std::make_unique<grid_supportIntf>(&m_adapter, req, module_id, Mapping{evse}));
    }

    void add_reservation(const std::string& module_id) {
        auto req = m_requirement;
        req.index = r_reservation.size();
        r_reservation.emplace_back(std::make_unique<reservationIntf>(&m_adapter, req, module_id, std::nullopt));
    }

    auto get_config_service_client() {
        return m_adapter.get_config_service_client();
    }

    // ------------------------------------------------------------------------
    // helpers

    void add_cmd_result(const json& data) {
        m_adapter.add_cmd_result(data);
    }

    void subscribe_global_all_errors(const Everest::error::ErrorCallback& callback,
                                     const Everest::error::ErrorCallback& clear_callback) {
        m_adapter.get_global_error_manager()->subscribe_global_all_errors(callback, clear_callback);
    }

    // ------------------------------------------------------------------------
    // Subscribe to variables published by the OCPP module

    // subscribe for all interfaces and variables
    void subscribe_var(var_cb_t cb) {
        m_adapter.subscribe_var("", "", -1, std::move(cb));
    }

    // subscribe for specific interface and variable
    void subscribe_var(const std::string_view& interface, const std::string_view& variable, var_cb_t cb) {
        m_adapter.subscribe_var(interface, variable, -1, std::move(cb));
    }

    // subscribe for specific interface and variable and instance
    void subscribe_var(const std::string_view& interface, const std::string_view& variable, int instance, var_cb_t cb) {
        m_adapter.subscribe_var(interface, variable, instance, std::move(cb));
    }

    // ------------------------------------------------------------------------
    // Publish to interfaces used by the OCPP module

    void publish(const Requirement& req, const std::string_view& fn, const json& args) {
        m_adapter.publish(m_requirement, std::string{fn}, args);
    }

    void publish(std::size_t index, const std::string_view& fn, const json& args) {
        auto req = m_requirement;
        req.index = index;
        m_adapter.publish(req, std::string{fn}, args);
    }

    void publish_ready(std::size_t index, bool value) {
        json::value_type arg = value;
        publish(index, "ready", arg);
    }

private:
    provides_t m_provides{p_auth_validator, p_auth_provider, p_data_transfer, p_ocpp_generic, p_session_cost};
    requires_t m_requires{r_auth,         r_charger_information, r_data_transfer, r_display_message, r_evse_energy_sink,
                          r_evse_manager, r_extensions_15118,    r_grid_support,  r_reservation,     r_security,
                          r_system};
};

} // namespace stubs
