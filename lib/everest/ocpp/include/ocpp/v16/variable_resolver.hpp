// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef OCPP_V16_VARIABLE_RESOLVER_HPP
#define OCPP_V16_VARIABLE_RESOLVER_HPP

#include <map>
#include <optional>
#include <shared_mutex>
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
/// load_ocpp16_key_mappings_from_yaml ignores such mappings.
bool is_connection_config_cv(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable);

/// \brief Whether the CV holds a value the 1.6 stack computes at runtime (see CVClass::ReadOnlyDerived):
///        SupportedFeatureProfiles and the CVs that carry the maxLimit of a max-limit key.
///
/// Custom key mappings must not target these CVs: the 1.6 stack overwrites their value, and OCPPmulti rejects
/// OCPP 2.x writes to them. load_ocpp16_key_mappings_from_yaml ignores such mappings.
bool is_read_only_derived_cv(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable);

/// \brief Whether a custom mapping entry may override the built-in mapping of the known key \p key.
///
/// True for keys mapped to OCPP16LegacyCtrlr, except the keys whose value the OCPP 1.6 stack derives or
/// expands itself (NumberOfConnectors, SupportedFeatureProfiles, MeterPublicKeys, DefaultPriceText).
bool is_overridable_key(keys::valid_keys key);

/// \brief Maps between OCPP 1.6 configuration keys and canonical OCPP 2.x Component/Variable pairs,
///        including operator-provided custom mappings.
///
/// Entries of the custom mapping file come in two kinds. An entry whose ocpp16_key is a known key overrides the
/// built-in mapping of ocpp/v16/known_keys.hpp, which is only allowed for keys stored in OCPP16LegacyCtrlr (see
/// is_overridable_key). With an override, the key reads and writes the override target, and the built-in
/// OCPP16LegacyCtrlr variable is no longer associated with the key. All other entries are custom keys.
///
/// The keys that device model owners declare for their variables at runtime form a second layer of the same two kinds,
/// which replace_file_mappings() leaves alone. Every key maps to at most one CV and every CV back to at most one key,
/// across both layers. Lookups and changes may happen concurrently.
class VariableResolver {
public:
    /// \brief Entries for known keys that are not overridable are ignored with a warning. An entry whose target is the
    /// built-in mapping of another key or the target of an earlier entry (in key order) is ignored with an error, so
    /// every CV maps back to at most one key. The custom mapping file loader drops such entries before they get here.
    explicit VariableResolver(const ocpp::v2::Ocpp16KeyMappings& custom_mappings = {});
    VariableResolver(const VariableResolver&) = delete;
    VariableResolver& operator=(const VariableResolver&) = delete;
    VariableResolver(VariableResolver&&) = delete;
    VariableResolver& operator=(VariableResolver&&) = delete;
    ~VariableResolver() = default;

    /// \brief Replaces the entries of the custom mapping file, by the rules of the constructor, and keeps the owner
    /// keys. An entry whose key or target an owner key holds is ignored with an error.
    void replace_file_mappings(const ocpp::v2::Ocpp16KeyMappings& custom_mappings);

    /// \brief Maps \p key, a custom key or an overridable known key, to the CV of a device model owner.
    /// \returns Why the key or the CV belongs to another key, which leaves the mapping unchanged
    std::optional<std::string> add_owner_key(const std::string& key, const ocpp::v2::Component& component,
                                             const ocpp::v2::Variable& variable);

    /// \brief Removes the owner key \p key; an overridden known key returns to its built-in mapping
    void remove_owner_key(const std::string& key);

    /// \brief Forward lookup: known keys first (built-in mapping or its override; keys with no v2 mapping and
    /// max-limit keys yield nullopt), then custom keys.
    std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> key_to_cv(const std::string& key) const;

    /// \brief Forward lookup of a known key: its override if present, else the built-in mapping. Unlike key_to_cv,
    /// max-limit keys yield the CV that carries their VariableCharacteristics.maxLimit.
    keys::DeviceModel_CV known_key_to_cv(keys::valid_keys key) const;

    /// \brief Forward lookup of a custom key
    std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> custom_key_to_cv(const std::string& key) const;

    /// \brief Reverse lookup: standard reverse table first, then the custom-mapping reverse index.
    std::optional<std::string> cv_to_key(const ocpp::v2::Component& component,
                                         const ocpp::v2::Variable& variable) const;

    /// \brief Reverse lookup of a known key: an override target maps to its key; a built-in target of an
    /// overridden key maps to no key.
    std::optional<std::string>
    cv_to_known_key(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable,
                    ocpp::v2::AttributeEnum attribute = ocpp::v2::AttributeEnum::Actual) const;

    /// \brief Classify a CV for 1.6 key handling
    CVClass classify(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const;

    bool is_overridden(keys::valid_keys key) const;

    /// \brief The overrides of the custom mapping file and of the owners
    std::map<keys::valid_keys, std::pair<ocpp::v2::Component, ocpp::v2::Variable>> overrides() const;

    /// \brief The custom keys of the custom mapping file and of the owners
    ocpp::v2::Ocpp16KeyMappings custom_keys() const;

private:
    struct Layer {
        std::map<keys::valid_keys, std::pair<ocpp::v2::Component, ocpp::v2::Variable>> overrides_by_key;
        // ocpp/v2/comparators.hpp ordering keeps instance/evse-qualified CVs distinct
        std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, keys::valid_keys> overrides_by_cv;
        ocpp::v2::Ocpp16KeyMappings custom;
        std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, std::string> custom_reverse;

        bool holds_key(const std::string& key) const;
        std::optional<std::string> key_of(const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) const;
        void add(const std::string& key, const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv);
    };

    /// \brief The key that holds \p cv in the built-in table, other than \p key, or in a layer
    std::optional<std::string> holder_of(const std::string& key,
                                         const std::pair<ocpp::v2::Component, ocpp::v2::Variable>& cv) const;
    Layer make_file_layer(const ocpp::v2::Ocpp16KeyMappings& custom_mappings) const;
    keys::DeviceModel_CV known_key_to_cv_unlocked(keys::valid_keys key) const;
    std::optional<std::string> cv_to_known_key_unlocked(const ocpp::v2::Component& component,
                                                        const ocpp::v2::Variable& variable,
                                                        ocpp::v2::AttributeEnum attribute) const;
    std::optional<std::string> cv_to_key_unlocked(const ocpp::v2::Component& component,
                                                  const ocpp::v2::Variable& variable) const;
    bool is_overridden_unlocked(keys::valid_keys key) const;

    mutable std::shared_mutex mutex;
    Layer file;
    Layer owner;
};

} // namespace ocpp::v16

#endif // OCPP_V16_VARIABLE_RESOLVER_HPP
