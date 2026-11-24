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
/// runtime. std::nullopt when the component configuration does not define the target; a variable that an owner
/// registers at runtime is not a mapping target.
using MappingTargetLookup = std::function<std::optional<MappingTargetInfo>(const Component&, const Variable&)>;

/// \brief Why a key cannot be mapped to a target
struct KeyMappingConflict {
    /// \brief The key or the target belongs to another key: a built-in key, an entry accepted before, or the built-in
    /// mapping of another key. Otherwise the mapping is not allowed at all.
    bool in_use;
    std::string reason;
};

/// \brief Why \p ocpp16_key cannot be mapped to \p cv next to the entries \p accepted before, whose targets
/// \p key_by_target indexes: every target stores exactly one OCPP 1.6 key, connection configuration and the variables
/// the OCPP 1.6 stack computes cannot be targets, and a built-in key can only be mapped when it is overridable (see
/// ocpp::v16::is_overridable_key).
std::optional<KeyMappingConflict>
find_key_mapping_conflict(const std::string& ocpp16_key, const std::pair<Component, Variable>& cv,
                          const Ocpp16KeyMappings& accepted,
                          const std::map<std::pair<Component, Variable>, std::string>& key_by_target);

/// \brief Why the target \p cv cannot hold the value of \p ocpp16_key, as found by \p lookup: it is not found, has no
/// Actual attribute or, for a built-in key, has another data type than the key's built-in variable.
std::optional<std::string> find_mapping_target_problem(const std::string& ocpp16_key,
                                                       const std::pair<Component, Variable>& cv,
                                                       const MappingTargetLookup& lookup);

/// \brief \p mappings without the entries whose target cannot hold the key's value: a target that the component
/// configuration does not define or that has no Actual attribute and, for an override, a target with another data type
/// than the key's built-in variable. An overridden key then keeps its built-in OCPP16LegacyCtrlr variable, a custom
/// key is not available.
///
/// The migration and the device model configuration both apply this, each with its own \p lookup, so they keep the
/// same mappings. With \p log_problems, every dropped entry is logged as an error.
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
