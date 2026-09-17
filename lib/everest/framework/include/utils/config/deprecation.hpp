// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <utils/config/types.hpp>

namespace everest::config {

enum class DeprecationKind {
    Module,
    ConfigEntry
};

/// \brief A deprecation declared by the "deprecated" section of a manifest, either for a whole module or for a
/// single config entry
struct DeprecationNotice {
    DeprecationKind kind{DeprecationKind::Module};
    std::string component; ///< Resolved name of the deprecated component, never empty
    std::string module_id;
    std::string module_name;
    std::optional<std::string> implementation_id; ///< Only set for config entries of an implementation
    std::optional<std::string> config_entry;      ///< Only set for config entries
    std::string deprecated_in;
    std::string earliest_removal;
    std::optional<std::string> migration_guide;
    std::optional<std::string> note;
};

/// \brief Parses the "deprecated" section of \p owner, using \p fallback_component when it declares no component
/// \returns the parsed notice or std::nullopt if \p owner declares no deprecation
std::optional<DeprecationNotice> parse_deprecation(const nlohmann::json& owner, const std::string& fallback_component);

/// \brief Renders \p notice as a multi line warning. Single source of the wording for all reporters
std::string format_deprecation_notice(const DeprecationNotice& notice);

/// \brief Collects the module level deprecation declared by \p manifest
std::optional<DeprecationNotice> collect_module_deprecation(const nlohmann::json& manifest,
                                                            const std::string& module_id,
                                                            const std::string& module_name);

/// \brief Collects the deprecations of the config entries declared by \p config_map_schema
///
/// A notice is only collected for entries that are actually configured: \p defaulted_entries lists the entries whose
/// value was taken from the manifest default. Those are only known when the config was parsed from YAML, which
/// \p origin_authoritative indicates; otherwise an entry counts as configured when it has no manifest default or its
/// value differs from that default.
///
/// \param implementation_id the implementation the config belongs to, std::nullopt for the module config
std::vector<DeprecationNotice> collect_config_deprecations(
    const nlohmann::json& config_map_schema, const std::vector<ConfigurationParameter>& configuration_parameters,
    const std::set<std::string>& defaulted_entries, bool origin_authoritative, const std::string& module_id,
    const std::string& module_name, const std::optional<std::string>& implementation_id);

} // namespace everest::config
