// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#ifndef OCPP_V16_VARIABLE_RESOLVER_HPP
#define OCPP_V16_VARIABLE_RESOLVER_HPP

#include <map>
#include <optional>
#include <string>
#include <utility>

#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ocpp16_custom_config_mappings.hpp>
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
class VariableResolver {
public:
    explicit VariableResolver(ocpp::v2::Ocpp16CustomConfigMappings custom_mappings);

    /// \brief Forward lookup: standard key table first (keys with no v2 mapping yield nullopt), then custom mappings.
    std::optional<std::pair<ocpp::v2::Component, ocpp::v2::Variable>> key_to_cv(const std::string& key) const;

    /// \brief Reverse lookup: standard reverse table first, then the custom-mapping reverse index.
    ReverseResult cv_to_key(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const;

    /// \brief Classify a CV for 1.6 key handling
    CVClass classify(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) const;

private:
    ocpp::v2::Ocpp16CustomConfigMappings custom_mappings;
    // ocpp/v2/comparators.hpp ordering keeps instance/evse-qualified CVs distinct
    std::map<std::pair<ocpp::v2::Component, ocpp::v2::Variable>, ReverseResult> custom_reverse;
};

} // namespace ocpp::v16

#endif // OCPP_V16_VARIABLE_RESOLVER_HPP
