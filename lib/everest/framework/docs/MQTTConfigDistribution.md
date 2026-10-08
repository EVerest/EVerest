# Module configuration distributed via MQTT

Since everest-framework 0.19.0 the module configuration is parsed once
by the manager and then distributed to the modules via MQTT.
This is achieved by parsing the MQTT settings from the config,
spawning the modules and passing these MQTT settings to them.
The modules themselves then ask for their module config via MQTT,
which is in turn provided to them from the manager.
After the modules have received their config, their init() function is called.
Afterwards they signal ready to the manager.
The manager sends out the global ready signal
once it has received all Module ready signals.

Since the config-service refactor the module configurations are no longer
parsed from the YAML config on every boot: the manager seeds a SQLite config
database from the YAML config once, loads the active config slot from the
database via the ConfigServiceCore and validates it against the manifests with
ManagerConfig. Distribution to the modules is unchanged. See ConfigService.md
for the config slot handling and the runtime configuration service.

The following sequence diagram illustrates this startup process

```mermaid
sequenceDiagram
    create participant manager
    create participant ManagerSettings
    manager-)ManagerSettings: ManagerSettings(prefix, config_path)
    ManagerSettings-->>manager: return ms
    create participant ConfigServiceCore
    manager-)ConfigServiceCore: ConfigServiceCore(ms, config database)
    ConfigServiceCore-->>manager: module configs of the active config slot
    create participant MQTTAbstraction
    manager-)MQTTAbstraction: MQTTAbstraction(ms.mqtt_settings)
    MQTTAbstraction-->>manager: return mqtt_abstraction
    create participant ManagerConfig
    manager-)ManagerConfig: ManagerConfig(ms, module configs)
    activate manager
    manager->>manager: start_modules()
    manager->>MQTTAbstraction: publish(interfaces, types, schemas, manifests, module_names, settings, retain=true)
    loop For every module
        manager->>manager: spawn_modules(Module)
        create participant Module
        manager->>Module: spawn Module
        Module->>MQTTAbstraction: get(Config)
        MQTTAbstraction->>manager: get(Config of Module)
        manager-->>MQTTAbstraction: publish(module configs, mappings)
        MQTTAbstraction-->>Module: publish(module configs, mappings)
        Module->>Module: init
        Module->>MQTTAbstraction: publish(ready)
        MQTTAbstraction->>manager: publish(ready of Module)
    end
    manager->>MQTTAbstraction: publish global ready
```

Class diagram

```mermaid
classDiagram
ConfigBase <|-- ManagerConfig
ConfigBase <|-- Config
ConfigParseSettings <|-- ManagerSettings
MQTTSettings *-- ConfigBase
ConfigParseSettings <-- ManagerConfig : references

note for ConfigBase "
Baseclass containing module configs, manifests, interfaces,
types and functions to access this information which
needs to be available in all derived classes
"

class ConfigParseSettings{
    +fs::path schemas_dir
    +fs::path interfaces_dir
    +fs::path types_dir
    +fs::path errors_dir
    +fs::path modules_dir
    +fs::path configs_dir
    +fs::path config_file
    +nlohmann::json config
    +bool validate_schema
}

class ManagerSettings{
    +fs::path db_dir
    +fs::path www_dir
    +int controller_port
    +int controller_rpc_timeout_ms
    +std::string run_as_user
    +std::string version_information
    +MQTTSettings mqtt_settings
    +RuntimeSettings runtime_settings
    +ManagerSettings(const std::string& prefix, const std::string& config)
    +ManagerSettings(const std::string& prefix, const std::string& config, const std::string& db_path)
    +ManagerSettings(WithoutConfig, const std::string& prefix, const std::string& db_path)
}

class MQTTSettings{
    +std::string broker_socket_path
    +std::string broker_host
    +std::uint16_t broker_port
    +std::string everest_prefix
    +std::string external_prefix
    +bool uses_socket()
}

class ConfigBase{
    #const MQTTSettings mqtt_settings
    +ConfigBase(const MQTTSettings& mqtt_settings)
}

class ManagerConfig{
    -const ConfigParseSettings ps
    +ManagerConfig(const ManagerSettings& ms)
    +ManagerConfig(const ManagerSettings& ms, ModuleConfigurations preloaded_configs)
    +ManagerConfig(const ConfigParseSettings& ps)
    +ManagerConfig(const ConfigParseSettings& ps, ModuleConfigurations preloaded_configs)
    -load_and_validate_manifest(ModuleConfig& module_config)
    -nlohmann::json resolve_interface(std::string_view intf_name)
    -nlohmann::json load_interface_file(std::string_view intf_name)
    -resolve_all_requirements()
    -parse(ModuleConfigurations& module_configs)
}

class Config{
    +Config(const MQTTSettings& mqtt_settings, const nlohmann::json& config)
    +bool module_provides(std::string_view module_name, std::string_view impl_id)
    +const nlohmann::json& get_module_cmds(std::string_view module_name, std::string_view impl_id)
    +std::vector~Fulfillment~ resolve_requirement(std::string_view module_id, std::string_view requirement_id) const
    +std::list~Requirement~ get_requirements(std::string_view module_id) const
    +RequirementInitialization get_requirement_initialization(std::string_view module_id) const
    +ModuleConfigs get_module_configs(std::string_view module_id) const
    +ModuleInfo get_module_info(std::string_view module_id) const
    +std::optional~TelemetryConfig~ get_telemetry_config()
    +nlohmann::json get_interface_definition(std::string_view interface_name) const
}
```
