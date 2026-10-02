// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

#include <everest/util/async/monitor.hpp>

#include <utils/config/types.hpp>
#include <utils/mqtt_abstraction.hpp>

namespace Everest {
namespace config {

class ConfigServiceInterface; // defined in config_service_interface.hpp

constexpr auto MODULE_IMPLEMENTATION_ID = "!module";
inline constexpr std::size_t mqtt_get_config_retries = 1;

/// \brief The type of request or response
enum class Type {
    Get,    ///< Identifies a get request or response
    Set,    ///< Identifies a set request or response
    Unknown ///< Used for unknown requests that could not be parsed
};

/// \brief Possible get request and response sub-types
enum class GetType {
    All,         ///< All module configurations that the requesting module has access to
    Module,      ///< The module configuration for the requesting module
    Value,       ///< A specific configuration value identified by a ConfigurationParameterIdentifier
    AllMappings, ///< All module mappings that the requesting module has access to
    Unknown      ///< Used for unknown requests that could not be parsed

    // TODO: Potential additions in the future:
    // Delta, // This would need tracking of when the last Request was made
};

/// \brief Represents a get request
struct GetRequest {
    GetType type = GetType::Unknown;                                             ///< The type of get request
    std::optional<everest::config::ConfigurationParameterIdentifier> identifier; ///< Used for GetType::Value

    // TODO: Potential additions in the future:
    // optional timestamp for GetType::Delta?
    // a list of requested modules?
};

/// \brief Represents a response to a get request
struct GetResponse {
    GetType type = GetType::Unknown; ///< The type of get response, the same as in the get request
    nlohmann::json data;             ///< Data associated with this reponse.
    // FIXME: use proper type(s) for data?
};

/// \brief Represents a set request
struct SetRequest {
    everest::config::ConfigurationParameterIdentifier
        identifier;    ///< An identifier for the configuration parameter to be set
    std::string value; ///< The string representation of the configuration value to be set
    // TODO: should value be a ConfigEntry variant type?
};

/// \brief Possible response status values
enum class ResponseStatus {
    Ok,          ///< Everything worked
    Error,       ///< There was an error during handling of the request
    AccessDenied ///< There was an access error during handling of the request
};

/// \brief Possible set response status values
enum class SetResponseStatus {
    Accepted,      ///< Configuration value was set successfully
    Rejected,      ///< Configuration value could not be set
    RebootRequired ///< Configuration value was set successfully but a reboot is required for modules to actually use
                   ///< this value
};

/// \brief Represents a response to a set request
struct SetResponse {
    SetResponseStatus status = SetResponseStatus::Rejected; ///< Status of the set response
    std::string status_info;                                ///< Can contain additional status information
};

/// \brief Represents a container for various requests that can be made to the ConfigService
struct Request {
    Type type = Type::Unknown;                                    ///< The type of request
    std::variant<std::monostate, GetRequest, SetRequest> request; ///< The request itself
    std::string origin; ///< The origin of the request, the module id of the requesting module
};

/// \brief Represents a container for various responses to requests made to the ConfigService
struct Response {
    ResponseStatus status = ResponseStatus::Error; ///< Status of the response
    std::string status_info;                       ///< Can contain additional status information
    std::optional<Type> type; ///< The type of the response, identical to the request, missing when status is Error
    std::variant<std::monostate, GetResponse, SetResponse> response; ///< The response itself
};

/// \brief Represents a container for getting a configuration parameter
struct GetConfigResult {
    ResponseStatus status = ResponseStatus::Error;                   ///< Status of the result
    std::string status_info;                                         ///< Can contain additional status information
    everest::config::ConfigurationParameter configuration_parameter; ///< The requested configuration parameter
};

/// \brief Represents a container for the result of setting a configuration parameter
struct SetConfigResult {
    ResponseStatus status = ResponseStatus::Error; ///< Status of the result
    std::string status_info;                       ///< Can contain additional status information
    everest::config::SetConfigStatus set_status =
        everest::config::SetConfigStatus::Rejected; ///< Specific status for the resut of setting the config parameter
};

/// \brief Represents a compound type to identify a specific module instance and its type
struct ModuleIdType {
    std::string module_id;   ///< The module id
    std::string module_type; ///< The associated module type

    /// \brief Orders by module_id first, then by module_type
    bool operator<(const ModuleIdType& rhs) const;
};

/// \brief An accepted change of a configuration parameter of another module
struct ConfigUpdateNotification {
    everest::config::ConfigurationParameterIdentifier identifier; ///< The changed configuration parameter
    std::string value;                                            ///< The string representation of the new value
    SetResponseStatus status = SetResponseStatus::Accepted; ///< Accepted if the module applied the value at runtime,
                                                            ///< RebootRequired if it applies on the next start
    std::optional<std::string> origin; ///< The module that requested the change, empty if it came from outside EVerest
};

struct ConfigChangeResult {
    SetResponseStatus status;
    std::string reason; ///< only meaningful when status == Rejected

    static ConfigChangeResult Accepted() {
        return {SetResponseStatus::Accepted, {}};
    }

    static ConfigChangeResult AcceptedRebootRequired() {
        return {SetResponseStatus::RebootRequired, {}};
    }

    static ConfigChangeResult Rejected(const std::string& reason) {
        return {SetResponseStatus::Rejected, reason};
    }
};

class ConfigServiceClient {
public:
    using ConfigChangeHandler = std::function<ConfigChangeResult(const std::string& new_value)>;
    using ConfigUpdateHandler = std::function<void(const ConfigUpdateNotification& notification)>;

    /// \brief ConfigService client using the provided \p mqtt_abstraction for the module identified by \p module_id
    /// \p module_names is a mapping of all module ids to module names/types for usage in get_module_configs()
    /// \p access holds the access rules of the module, checked by register_config_update_handler()
    ConfigServiceClient(std::shared_ptr<MQTTAbstraction> mqtt_abstraction, const std::string& module_id,
                        const std::map<std::string, std::string, std::less<>>& module_names,
                        const everest::config::Access& access);

    /// \brief Compiles and \returns all module configs that this module has access to
    std::map<ModuleIdType, everest::config::ModuleConfigurationParameters> get_module_configs();

    /// \brief Compiles and \returns all mappings of modules that this module has access to
    std::map<std::string, ModuleTierMappings> get_mappings();

    /// \brief Sets the config \p value associated with the \p identifier
    /// \returns a result containing status and potential error information
    SetConfigResult set_config_value(const everest::config::ConfigurationParameterIdentifier& identifier,
                                     const std::string& value);

    /// \brief Gets the config value associated with the \p identifier
    /// \returns a result containing the configuration item or an error
    GetConfigResult get_config_value(const everest::config::ConfigurationParameterIdentifier& identifier);

    /// \brief Registers the \p handler to be invoked when the configuration parameter \p param_name of the
    /// implementation identified by \p impl_id is changed
    void register_config_change_handler(const std::string& impl_id, const std::string_view param_name,
                                        ConfigChangeHandler handler);

    /// \brief Registers the \p handler to be invoked for every accepted change of a configuration parameter of the
    /// module \p module_id in the active slot.
    ///
    /// Changes are delivered only if the access block of this module allows reading the configuration of
    /// \p module_id, and only after the configuration service accepted them. Changes that this module requested
    /// itself are not delivered; their result is the reply to set_config_value(). Changes made before the
    /// registration are not delivered, so read the current values after registering.
    /// The handler runs on the MQTT dispatch thread, one notification after the other.
    /// \returns A function that unregisters the handler. Once it returned, the handler is not called any more. It may
    /// be called from a handler. std::nullopt if \p module_id is unknown or this module may not read its
    /// configuration; the handler is not registered then.
    std::optional<std::function<void()>> register_config_update_handler(const std::string& module_id,
                                                                        ConfigUpdateHandler handler);

private:
    std::shared_ptr<MQTTAbstraction> m_mqtt_abstraction;
    std::string m_origin;
    std::map<std::string, std::string, std::less<>> m_module_names;
    everest::config::Access m_access;
    // a key-value (parameter-name to handler) store for each implementation_id
    std::map<std::string, std::map<std::string, ConfigChangeHandler>> m_change_callbacks;
    // Guards m_change_callbacks: register_config_change_handler() may be called from any module
    // thread (e.g. from Python at runtime) while the MQTT dispatch thread reads the map in
    // mqtt_set_request().
    std::mutex m_change_callbacks_mutex;
    struct UpdateHandlers {
        // module id of the changed module -> handler id -> handler
        std::map<std::string, std::map<std::uint64_t, ConfigUpdateHandler>> handlers;
        std::uint64_t next_id{0};
    };
    everest::lib::util::monitor<UpdateHandlers> m_update_handlers;
    // Held while the handlers of a notification run, so that unregistering can wait for them
    std::recursive_mutex m_update_dispatch_mutex;

    void mqtt_set_request(const nlohmann::json& data);
    void mqtt_config_update(const nlohmann::json& data);
    bool is_update_handler_registered(const std::string& module_id, std::uint64_t id);
};

class MqttConfigServiceHandler {
public:
    /// \brief MQTT adapter that distributes relevant parts of the active configuration to modules that request them
    /// and have appropriate access rights. Access control is enforced using the Access rules embedded in each
    /// module's configuration (from the active config slot).
    /// \param mqtt_abstraction  MQTT transport for pub/sub and module-to-module set forwarding.
    /// \param config_svc        All domain operations — reads (via get_active_module_configurations) and writes
    ///                          (via set_config_parameters) are routed through this interface.
    /// Accepted changes of the active slot are sent to every other module whose access block allows reading the
    /// changed module, except to the module that requested the change.
    MqttConfigServiceHandler(MQTTAbstraction& mqtt_abstraction, ConfigServiceInterface& config_svc);
    ~MqttConfigServiceHandler();
    MqttConfigServiceHandler(const MqttConfigServiceHandler&) = delete;
    MqttConfigServiceHandler& operator=(const MqttConfigServiceHandler&) = delete;

    /// \brief This class provides an MQTT interface to modules and uses the ConfigServiceInterface to fulfill incoming
    /// requests. This function allows to request a config parameter change from a module. This function skips
    /// validating module_ids and characteristics on purpose. It should only be called with inputs known to work.
    /// \param cfg_param_id  The identifier for the parameter to be changed
    /// \param value         The new configuration parameter value as string
    std::optional<Everest::config::SetResponse>
    cmd_set_cfg_param(const everest::config::ConfigurationParameterIdentifier& cfg_param_id, const std::string& value);

private:
    MQTTAbstraction& m_mqtt_abstraction;
    std::shared_ptr<TypedHandler> m_get_config_token;
    ConfigServiceInterface& m_config_svc;
    std::function<void()> m_unregister_config_update_handler;
};

namespace conversions {

// strings should already be valid
template <typename T> T ConfigFromString(const std::string& value) = delete;
template <> bool ConfigFromString<bool>(const std::string& value);
template <> int ConfigFromString<int>(const std::string& value);
template <> double ConfigFromString<double>(const std::string& value);

std::string type_to_string(Type type);

Type string_to_type(const std::string& type);

std::string get_type_to_string(GetType type);

GetType string_to_get_type(const std::string& type);

std::string response_status_to_string(ResponseStatus status);

ResponseStatus string_to_response_status(const std::string& status);

std::string set_response_status_to_string(SetResponseStatus status);

SetResponseStatus string_to_set_response_status(const std::string& status);

everest::config::SetConfigStatus set_response_status_to_set_config_status(SetResponseStatus status);

SetResponseStatus set_config_status_to_set_response_status(everest::config::SetConfigStatus status);
} // namespace conversions

std::ostream& operator<<(std::ostream& os, const GetType& t);

void to_json(nlohmann::json& j, const GetRequest& r);

void from_json(const nlohmann::json& j, GetRequest& r);

void to_json(nlohmann::json& j, const SetRequest& r);

void from_json(const nlohmann::json& j, SetRequest& r);

void to_json(nlohmann::json& j, const GetResponse& r);

void from_json(const nlohmann::json& j, GetResponse& r);

void to_json(nlohmann::json& j, const SetResponse& r);

void from_json(const nlohmann::json& j, SetResponse& r);

void to_json(nlohmann::json& j, const Request& r);

void from_json(const nlohmann::json& j, Request& r);

void to_json(nlohmann::json& j, const Response& r);

void from_json(const nlohmann::json& j, Response& r);

void to_json(nlohmann::json& j, const SetConfigResult& r);

void from_json(const nlohmann::json& j, SetConfigResult& r);

void to_json(nlohmann::json& j, const GetConfigResult& r);

void from_json(const nlohmann::json& j, GetConfigResult& r);

void to_json(nlohmann::json& j, const ConfigChangeResult& r);

void from_json(const nlohmann::json& j, ConfigChangeResult& r);

void to_json(nlohmann::json& j, const ConfigUpdateNotification& n);

void from_json(const nlohmann::json& j, ConfigUpdateNotification& n);
} // namespace config
} // namespace Everest

NLOHMANN_JSON_NAMESPACE_BEGIN

template <> struct adl_serializer<everest::config::ConfigurationParameterIdentifier> {
    static void to_json(nlohmann::json& j, const everest::config::ConfigurationParameterIdentifier& c);
    static void from_json(const nlohmann::json& j, everest::config::ConfigurationParameterIdentifier& c);
};

NLOHMANN_JSON_NAMESPACE_END
