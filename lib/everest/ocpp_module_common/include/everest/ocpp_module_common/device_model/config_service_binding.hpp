// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <everest/util/async/monitor.hpp>
#include <ocpp/v2/device_model_registry_interface.hpp>
#include <utils/config/types.hpp>
#include <utils/mqtt_config_service.hpp>

namespace ocpp_module_common::device_model {

/// \brief Owner of the variables that are stored in an EVerest module configuration
inline constexpr auto VARIABLE_OWNER_EVEREST_CONFIG = "EVEREST_CONFIG";

/// \brief The operations of the configuration service that ConfigServiceBinding uses
class ConfigServiceAccess {
public:
    virtual ~ConfigServiceAccess() = default;
    virtual std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>
    get_module_configs() = 0;
    virtual std::map<std::string, ModuleTierMappings> get_mappings() = 0;
    virtual Everest::config::SetConfigResult
    set_config_value(const everest::config::ConfigurationParameterIdentifier& identifier, const std::string& value) = 0;
    /// \returns A function that unregisters \p handler, std::nullopt if the configuration of \p module_id may not be
    ///          read
    virtual std::optional<std::function<void()>>
    register_config_update_handler(const std::string& module_id,
                                   Everest::config::ConfigServiceClient::ConfigUpdateHandler handler) = 0;
};

/// \brief ConfigServiceAccess through the framework's configuration service client
std::unique_ptr<ConfigServiceAccess>
make_config_service_access(std::shared_ptr<Everest::config::ConfigServiceClient> config_service_client);

/// \brief A module configuration parameter backing an OCPP variable
struct ConfigParameterBinding {
    everest::config::ConfigurationParameterIdentifier parameter;
    ocpp::v2::Component component;
    ocpp::v2::Variable variable;
};

/// \brief Builds the device model view of the module configurations: one component per module, named by the module
/// type with the module id as instance and the EVSE and connector of the module mapping, and one variable per
/// configuration parameter, named `<implementation id>.<parameter>` for implementation parameters. Modules and
/// parameters whose names do not fit the OCPP identifier strings are skipped.
std::vector<ConfigParameterBinding> build_module_configuration_views(
    const std::map<Everest::config::ModuleIdType, everest::config::ModuleConfigurationParameters>& module_configs,
    const std::map<std::string, ModuleTierMappings>& mappings);

/// \brief The characteristics of the OCPP variable that represents \p characteristics. Throws std::out_of_range for
/// a parameter of unknown type.
ocpp::v2::VariableCharacteristics
to_variable_characteristics(const everest::config::ConfigurationParameterCharacteristics& characteristics);

/// \brief Registers OCPP variables whose values are stored in EVerest module configurations under
/// VARIABLE_OWNER_EVEREST_CONFIG and serves as their provider: a write is forwarded to the configuration service, and
/// changes made at the configuration service are reported to the device model.
class ConfigServiceBinding final : public ocpp::v2::VariableValueProvider,
                                   public std::enable_shared_from_this<ConfigServiceBinding> {
public:
    /// \brief Registers the module configuration views with \p registry, with the binding as their provider, which
    /// \p registry shares the ownership of until stop(). \p config_service must outlive the binding. Call stop()
    /// before \p registry is destroyed.
    static std::shared_ptr<ConfigServiceBinding> create(ConfigServiceAccess& config_service,
                                                        ocpp::v2::DeviceModelRegistryInterface& registry);

    ConfigServiceBinding(ConfigServiceAccess& config_service, ocpp::v2::DeviceModelRegistryInterface& registry);
    /// \brief Stops reporting changes, but leaves the registry alone: while the registry shares the ownership, the
    /// binding is destroyed only after stop() or together with the registry.
    ~ConfigServiceBinding() override;
    ConfigServiceBinding(const ConfigServiceBinding&) = delete;
    ConfigServiceBinding& operator=(const ConfigServiceBinding&) = delete;
    ConfigServiceBinding(ConfigServiceBinding&&) = delete;
    ConfigServiceBinding& operator=(ConfigServiceBinding&&) = delete;

    /// \brief Stops reporting changes and unregisters the variables, which releases the registry's share of the
    /// binding. Waits for a report and a write in progress. The registry must still exist.
    void stop();

    /// \brief What the registry answered to the registration of the configuration parameter views
    ocpp::v2::ComponentRegistrationResult registration_result();

    ocpp::v2::SetVariableStatusEnum set_value(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                                              ocpp::v2::AttributeEnum attribute, const std::string& value,
                                              const std::string& source, ocpp::v2::WriteId write_id) override;

private:
    struct ParameterKey {
        std::string module_id;
        std::string implementation_id;
        std::string name;
        bool operator<(const ParameterKey& rhs) const;
    };

    struct State {
        /// \brief Becomes true once the variables are registered; notifications before that are kept in pending
        bool registered{false};
        std::map<ParameterKey, std::string> pending;
        std::map<ParameterKey, std::vector<ConfigParameterBinding>> bindings;
        std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, everest::config::ConfigurationParameterIdentifier>
            parameters;
        /// \brief The id of the last write to each view that the configuration service applied, which the reports
        /// of later changes are based on
        std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, ocpp::v2::WriteId> applied_writes;
        ocpp::v2::ComponentRegistrationResult registration_result;
    };

    static ParameterKey key_of(const everest::config::ConfigurationParameterIdentifier& identifier);

    void start();
    void unregister_update_handlers();
    void on_config_update(const Everest::config::ConfigUpdateNotification& notification);
    /// \brief Reports \p value of the parameter \p key to the device model for every variable bound to it
    void notify(const ParameterKey& key, const std::string& value);
    std::optional<everest::config::ConfigurationParameterIdentifier> parameter_of(const ocpp::v2::Component& component,
                                                                                  const ocpp::v2::Variable& variable);

    ConfigServiceAccess& config_service;
    ocpp::v2::DeviceModelRegistryInterface& registry;
    everest::lib::util::monitor<State> state;
    std::vector<std::function<void()>> unregister_update_handler_functions;
    bool stopped{false};
};

} // namespace ocpp_module_common::device_model
