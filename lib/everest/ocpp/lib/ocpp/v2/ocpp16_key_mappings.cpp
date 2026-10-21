// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/ocpp16_key_mappings.hpp>

#include <fstream>
#include <sstream>

#include <nlohmann/json-schema.hpp>
#include <ryml.hpp>
#include <ryml_std.hpp>

#include <everest/logging.hpp>
#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v16/variable_resolver.hpp>
#include <ocpp/v2/comparators.hpp>

namespace ocpp::v2 {

namespace {

[[noreturn]] void throw_yaml_error(c4::csubstr msg, const ryml::Location& loc) {
    std::stringstream error_message;
    error_message << "YAML parsing error: ";

    if (loc) {
        if (!loc.name.empty()) {
            error_message.write(loc.name.str, static_cast<std::streamsize>(loc.name.len));
            error_message << ':';
        }
        error_message << loc.line << ':';
        if (loc.col && loc.col != ryml::npos) {
            error_message << loc.col << ':';
        }
        if (loc.offset && loc.offset != ryml::npos) {
            error_message << " (" << loc.offset << "B):";
        }
    }

    error_message.write(msg.str, static_cast<std::streamsize>(msg.len));
    throw Ocpp16KeyMappingsError(error_message.str());
}

std::string describe(const Component& component, const Variable& variable) {
    std::string result = component.name.get();
    if (component.instance.has_value()) {
        result += '[' + component.instance->get() + ']';
    }
    result += '/' + variable.name.get();
    if (variable.instance.has_value()) {
        result += '[' + variable.instance->get() + ']';
    }
    return result;
}

/// Entries for known OCPP 1.6 keys override the built-in mapping. Only OCPP16LegacyCtrlr keys may be overridden,
/// and an override must not share its target with another key, since the target is the key's only storage.
void validate_overrides(const Ocpp16KeyMappings& mappings, const std::filesystem::path& mapping_file_path) {
    const auto error_prefix = "Invalid custom mapping in '" + mapping_file_path.string() + "': key '";

    for (const auto& [ocpp16_key, cv] : mappings) {
        const auto key = v16::keys::convert(ocpp16_key);
        if (!key.has_value()) {
            continue;
        }
        const auto& [component, variable] = cv;
        const auto builtin = v16::keys::convert_v2(key.value());

        if (!v16::is_overridable_key(key.value())) {
            const auto builtin_target =
                builtin.has_value() ? describe(builtin->first, builtin->second) : std::string{"no variable"};
            throw Ocpp16KeyMappingsError(
                error_prefix + ocpp16_key + "' is a built-in OCPP 1.6 key mapped to " + builtin_target +
                "; only built-in keys mapped to OCPP16LegacyCtrlr can be custom-mapped, except NumberOfConnectors, "
                "SupportedFeatureProfiles, MeterPublicKeys and DefaultPriceText");
        }

        if (const auto owner = v16::keys::convert_v2(component, variable, AttributeEnum::Actual);
            owner.has_value() && owner.value() != ocpp16_key) {
            throw Ocpp16KeyMappingsError(error_prefix + ocpp16_key + "' targets " + describe(component, variable) +
                                         ", which is the built-in mapping of OCPP 1.6 key '" + owner.value() + "'");
        }

        for (const auto& [other_key, other_cv] : mappings) {
            if (other_key != ocpp16_key && other_cv == cv) {
                throw Ocpp16KeyMappingsError(error_prefix + ocpp16_key + "' targets " + describe(component, variable) +
                                             ", which is also the target of key '" + other_key + "'");
            }
        }

        EVLOG_info << "OCPP 1.6 key " << ocpp16_key << " is custom-mapped to " << describe(component, variable)
                   << " instead of its built-in " << describe(builtin->first, builtin->second);
    }
}

[[noreturn]] void yaml_error_basic(c4::csubstr msg, const ryml::ErrorDataBasic& err, void*) {
    throw_yaml_error(msg, err.location);
}

[[noreturn]] void yaml_error_parse(c4::csubstr msg, const ryml::ErrorDataParse& err, void*) {
    // ymlloc refers to the parsed document, cpploc to ryml's own sources
    throw_yaml_error(msg, err.ymlloc);
}

[[noreturn]] void yaml_error_visit(c4::csubstr msg, const ryml::ErrorDataVisit& err, void*) {
    throw_yaml_error(msg, err.cpploc);
}

struct RymlCallbackInitializer {
    RymlCallbackInitializer() {
        // ryml >= 0.11 replaced the single error callback with one per error kind
        ryml::Callbacks callbacks;
        callbacks.set_error_basic(yaml_error_basic).set_error_parse(yaml_error_parse).set_error_visit(yaml_error_visit);
        ryml::set_callbacks(callbacks);
    }
};

nlohmann::ordered_json ryml_to_nlohmann_json(const c4::yml::ConstNodeRef& ryml_node) {
    if (ryml_node.is_map()) {
        auto object = nlohmann::ordered_json::object();
        for (const auto& child : ryml_node) {
            object[std::string(child.key().data(), child.key().len)] = ryml_to_nlohmann_json(child);
        }
        return object;
    }

    if (ryml_node.is_seq()) {
        auto array = nlohmann::ordered_json::array();
        for (const auto& child : ryml_node) {
            array.emplace_back(ryml_to_nlohmann_json(child));
        }
        return array;
    }

    if (ryml_node.empty() || ryml_node.val_is_null()) {
        return nullptr;
    }

    const auto& value = ryml_node.val();
    std::string value_string(value.data(), value.len);
    const auto value_quoted = ryml_node.is_val_quoted();

    if (!value_quoted) {
        if (ryml_node.val().is_integer()) {
            return std::stoi(value_string);
        }
        if (ryml_node.val().is_number()) {
            return std::stod(value_string);
        }
        if (value_string == "true") {
            return true;
        }
        if (value_string == "false") {
            return false;
        }
    }

    return value_string;
}

nlohmann::ordered_json load_yaml(const std::filesystem::path& path) {
    const static RymlCallbackInitializer ryml_callback_initializer;

    if (!std::filesystem::exists(path)) {
        throw Ocpp16KeyMappingsError("YAML mapping file not found: " + path.string());
    }

    std::ifstream ifs(path);
    const std::string content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));

    const auto tree = ryml::parse_in_arena(ryml::to_csubstr(content));
    return ryml_to_nlohmann_json(tree.rootref());
}

} // namespace

nlohmann::json get_ocpp16_key_mapping_schema() {
    return nlohmann::json::parse(R"json(
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "type": "object",
  "required": ["mappings"],
  "additionalProperties": false,
  "properties": {
    "mappings": {
      "type": "array",
      "items": {
        "type": "object",
        "required": ["ocpp16_key", "component", "variable"],
        "additionalProperties": false,
        "properties": {
          "ocpp16_key": {
            "type": "string",
            "minLength": 1
          },
          "component": {
            "type": "object",
            "required": ["name"],
            "additionalProperties": false,
            "properties": {
              "name": {
                "type": "string",
                "minLength": 1
              },
              "instance": {
                "type": "string",
                "minLength": 1
              },
              "evse": {
                "type": "object",
                "required": ["id"],
                "additionalProperties": false,
                "properties": {
                  "id": {
                    "type": "integer"
                  },
                  "connectorId": {
                    "type": "integer"
                  }
                }
              }
            }
          },
          "variable": {
            "type": "object",
            "required": ["name"],
            "additionalProperties": false,
            "properties": {
              "name": {
                "type": "string",
                "minLength": 1
              },
              "instance": {
                "type": "string",
                "minLength": 1
              }
            }
          }
        }
      }
    }
  }
}
)json");
}

Ocpp16KeyMappings load_ocpp16_key_mappings_from_yaml(const std::filesystem::path& mapping_file_path) {
    const auto yaml_json = load_yaml(mapping_file_path);

    nlohmann::json_schema::json_validator validator;
    validator.set_root_schema(get_ocpp16_key_mapping_schema());

    try {
        validator.validate(yaml_json);
    } catch (const std::exception& e) {
        throw Ocpp16KeyMappingsError("Invalid OCPP 1.6 key mapping file '" + mapping_file_path.string() +
                                     "': " + e.what());
    }

    // The schema validation ensures that the following usage of .at(...) is safe
    Ocpp16KeyMappings mappings;
    for (const auto& entry : yaml_json.at("mappings")) {
        const auto ocpp16_key = entry.at("ocpp16_key").get<std::string>();

        Component component;
        component.name = entry.at("component").at("name").get<std::string>();
        if (entry.at("component").contains("instance")) {
            component.instance = entry.at("component").at("instance").get<std::string>();
        }
        if (entry.at("component").contains("evse")) {
            EVSE evse;
            const auto& evse_json = entry.at("component").at("evse");
            evse.id = evse_json.at("id").get<std::int32_t>();
            if (evse_json.contains("connectorId")) {
                evse.connectorId = evse_json.at("connectorId").get<std::int32_t>();
            }
            component.evse = evse;
        }

        Variable variable;
        variable.name = entry.at("variable").at("name").get<std::string>();
        if (entry.at("variable").contains("instance")) {
            variable.instance = entry.at("variable").at("instance").get<std::string>();
        }

        // Connection config is managed by the standardized OCPP 1.6 keys (with their reboot/reconnect
        // semantics) or direct canonical CV access; a custom key would write it without either.
        if (v16::is_connection_config_cv(component, variable)) {
            throw Ocpp16KeyMappingsError(
                "Invalid custom mapping in '" + mapping_file_path.string() + "': key '" + ocpp16_key +
                "' targets connection configuration " + component.name.get() + "/" + variable.name.get() +
                "; connection configuration is managed via the standardized OCPP 1.6 keys and must not be "
                "custom-mapped");
        }

        if (!mappings.emplace(ocpp16_key, std::make_pair(component, variable)).second) {
            throw Ocpp16KeyMappingsError("Duplicate ocpp16_key in key mapping file '" + mapping_file_path.string() +
                                         "': " + ocpp16_key);
        }
    }

    validate_overrides(mappings, mapping_file_path);
    return mappings;
}

} // namespace ocpp::v2
