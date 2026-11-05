// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
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

/// \brief "Component[instance] (EVSE id, connector id)/Variable[instance]" of a mapping target, for messages.
std::string describe_mapping_target(const Component& component, const Variable& variable);

/// \brief What the key mapping rules need to know about a mapping target.
struct MappingTargetInfo {
    DataEnum data_type;
    bool has_actual_attribute; ///< the key's value is stored in the Actual attribute
};

/// \brief Looks a mapping target up where it lives: the component configs during the migration, the device model at
/// runtime. std::nullopt when the target does not exist there.
using MappingTargetLookup = std::function<std::optional<MappingTargetInfo>(const Component&, const Variable&)>;

/// \brief \p mappings without the overrides whose target cannot hold the key's value: a target that does not exist, has
/// no Actual attribute or has another data type than the key's built-in variable. Such keys keep their built-in
/// OCPP16LegacyCtrlr variable.
///
/// The migration and the device model configuration both apply this, each with its own \p lookup, so they keep the
/// same overrides. With \p log_problems, every dropped override is logged as an error, and so is every custom key whose
/// target does not exist or has no Actual attribute; such a custom key is kept but has no value.
Ocpp16KeyMappings usable_key_mappings(const Ocpp16KeyMappings& mappings, const MappingTargetLookup& lookup,
                                      bool log_problems);

/// \brief Load OCPP 1.6 custom mappings from YAML file, validate against schema and convert to mapping map.
///
/// An entry for a built-in OCPP 1.6 key overrides its built-in mapping (see ocpp::v16::VariableResolver).
///
/// Entries that cannot be used are ignored with an error log, so the remaining entries still apply: entries for a key
/// that an earlier entry already maps, entries targeting connection-config CVs (see
/// ocpp::v16::is_connection_config_cv), which are managed via the standardized OCPP 1.6 keys, entries for built-in
/// keys that cannot be overridden (see ocpp::v16::is_overridable_key), and entries whose target is the built-in
/// mapping of another key or the target of an earlier entry.
/// \param mapping_file_path Path to YAML mapping file.
/// \returns Parsed mappings for use with patch_component_config_with_ocpp16.
/// \throws Ocpp16KeyMappingsError on file parsing, schema validation or conversion errors.
Ocpp16KeyMappings load_ocpp16_key_mappings_from_yaml(const std::filesystem::path& mapping_file_path);

} // namespace ocpp::v2
