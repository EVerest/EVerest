// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>

#include <nlohmann/json_fwd.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp::v2 {

using Ocpp16KeyMappings = std::map<std::string, std::pair<ocpp::v2::Component, ocpp::v2::Variable>>;

class Ocpp16KeyMappingsError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// \brief Schema for YAML-based OCPP 1.6 custom mapping files.
///
/// The schema is expressed as JSON schema draft-07 and validated against the parsed YAML content.
nlohmann::json get_ocpp16_key_mapping_schema();

/// \brief Load OCPP 1.6 custom mappings from YAML file, validate against schema and convert to mapping map.
///
/// An entry for a built-in OCPP 1.6 key overrides its built-in mapping (see ocpp::v16::VariableResolver).
/// \param mapping_file_path Path to YAML mapping file.
/// \returns Parsed mappings for use with patch_component_config_with_ocpp16.
/// \throws Ocpp16KeyMappingsError on file parsing, schema validation or conversion errors, on
///         duplicate keys, on mappings targeting connection-config CVs (see
///         ocpp::v16::is_connection_config_cv), which are managed via the standardized OCPP 1.6 keys, on
///         entries for built-in keys that cannot be overridden (see ocpp::v16::is_overridable_key),
///         and on entries whose target is the built-in mapping of another key or the target of another entry.
Ocpp16KeyMappings load_ocpp16_key_mappings_from_yaml(const std::filesystem::path& mapping_file_path);

} // namespace ocpp::v2
