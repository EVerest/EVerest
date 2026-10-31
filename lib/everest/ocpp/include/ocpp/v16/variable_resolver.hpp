// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef OCPP_V16_VARIABLE_RESOLVER_HPP
#define OCPP_V16_VARIABLE_RESOLVER_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ocpp16_key_mappings.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp::v16 {

/// \brief How a canonical Component/Variable relates to the OCPP 1.6 configuration keys;
///        drives request routing in v16 mode.
enum class CVClass {
    /// Backed by a standard or custom-mapped 1.6 key; routed through the 1.6 stack (key
    /// validation, per-key side effects, change callbacks).
    KeyBacked,
    /// Connection/network-profile configuration (NetworkConfiguration/<slot>, the OCPPCommCtrlr
    /// selectors, the SecurityCtrlr fallbacks). Direct device-model access, but writes report
    /// RebootRequired: values are only picked up on the next (re)connect.
    ConnectionConfig,
    /// Computed by the 1.6 stack at runtime (SupportedFeatureProfiles, maxLimit report CVs);
    /// writes are rejected, reads served from the device model.
    ReadOnlyDerived,
    /// No relation to 1.6 keys; plain device-model access gated by the variable's mutability.
    Free,
};

/// \brief Whether the CV belongs to the connection/network-profile configuration managed by the
///        1.6 stack (see CVClass::ConnectionConfig): NetworkConfiguration/<slot>, the OCPPCommCtrlr
///        profile selectors and the SecurityCtrlr connection fallbacks.
///
/// Custom key mappings must not target these CVs: the key path writes the device model without
/// the RebootRequired/reconnect semantics the standardized keys and canonical CVs carry.
/// load_ocpp16_key_mappings_from_yaml rejects such mappings.
bool is_connection_config_cv(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable);

/// \brief Whether a custom mapping entry may override the built-in mapping of the known key \p key.
///
/// True for keys mapped to OCPP16LegacyCtrlr, except the keys whose value the OCPP 1.6 stack derives or
/// expands itself (NumberOfConnectors, SupportedFeatureProfiles, MeterPublicKeys, DefaultPriceText).
bool is_overridable_key(keys::valid_keys key);

/// \brief Result of a reverse (CV -> 1.6 key) lookup.
struct ReverseResult {
    std::optional<std::string> key;
    bool ambiguous{false};

    /// \brief The key is only trustworthy as a routing target when present and unambiguous.
    bool usable() const {
        return key.has_value() && !ambiguous;
    }
};

/// \brief Maps between OCPP 1.6 configuration keys and canonical OCPP 2.x Component/Variable pairs,
///        including operator-provided custom mappings.
///
/// Entries of the custom mapping file come in two kinds. An entry whose ocpp16_key is a known key overrides the
/// built-in mapping of ocpp/v16/known_keys.hpp, which is only allowed for keys stored in OCPP16LegacyCtrlr (see
/// is_overridable_key). With an override, the key reads and writes the override target, and the built-in
/// OCPP16LegacyCtrlr variable is no longer associated with the key. All other entries are custom keys.
class VariableResolver {
public:
    /// \brief Entries for known keys that are not overridable are ignored with a warning; the custom mapping file
    /// loader rejects them before they get here.
    explicit VariableResolver(const ocpp::v2::Ocpp16KeyMappings& custom_mappings = {});

    /// \brief Forward lookup: known keys first (built-in mapping or its override; keys with no v2 mapping and
    /// max-limit keys yield nullopt), then custom keys.
    std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> key_to_cv(const std::string& key) const;

    /// \brief Forward lookup of a known key: its override if present, else the built-in mapping. Unlike key_to_cv,
    /// max-limit keys yield the CV that carries their VariableCharacteristics.maxLimit.
    keys::DeviceModel_CV known_key_to_cv(keys::valid_keys key) const;

    /// \brief Reverse lookup: standard reverse table first, then the custom-mapping reverse index.
    ReverseResult cv_to_key(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const;

    /// \brief Reverse lookup of a known key: an override target maps to its key; a built-in target of an
    /// overridden key maps to no key.
    std::optional<std::string>
    cv_to_known_key(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                    ocpp::v2::AttributeEnum attribute = ocpp::v2::AttributeEnum::Actual) const;

    /// \brief Classify a CV for 1.6 key handling
    CVClass classify(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const;

    bool is_overridden(keys::valid_keys key) const;

    const std::map<keys::valid_keys, std::pair<ocpp::v2::Component, ocpp::v2::Variable>>& overrides() const {
        return overrides_by_key;
    }

    /// \brief The custom mapping entries for keys that are not known OCPP 1.6 keys.
    const ocpp::v2::Ocpp16KeyMappings& custom_keys() const {
        return custom_mappings;
    }

private:
    std::map<keys::valid_keys, std::pair<ocpp::v2::Component, ocpp::v2::Variable>> overrides_by_key;
    ocpp::v2::Ocpp16KeyMappings custom_mappings;
    // ocpp/v2/comparators.hpp ordering keeps instance/evse-qualified CVs distinct
    std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, keys::valid_keys> overrides_by_cv;
    std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, ReverseResult> custom_reverse;
};

} // namespace ocpp::v16

#endif // OCPP_V16_VARIABLE_RESOLVER_HPP
