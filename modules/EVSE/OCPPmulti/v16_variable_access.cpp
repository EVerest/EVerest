// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_variable_access.hpp"

#include <utility>

#include <fmt/core.h>

#include <everest/logging.hpp>
#include <ocpp/common/constants.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model_helpers.hpp>

namespace {

constexpr auto READ_ONLY_DERIVED_INFO = "Managed by the OCPP1.6 stack; read-only via this API";
constexpr auto AMBIGUOUS_MAPPING_INFO = "Ambiguous custom config mapping; fix DeviceModelConfigMappings";

std::string cv_to_string(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    const auto instance = [](const auto& opt) { return opt.has_value() ? fmt::format("({})", opt->get()) : ""; };
    return fmt::format("{}{}/{}{}", component.name.get(), instance(component.instance), variable.name.get(),
                       instance(variable.instance));
}

ocpp::v2::StatusInfo ambiguous_status_info() {
    ocpp::v2::StatusInfo info;
    info.reasonCode = "AmbiguousMapping";
    info.additionalInfo = ocpp::CiString<1024>(AMBIGUOUS_MAPPING_INFO);
    return info;
}

ocpp::v2::StatusInfo read_only_derived_status_info() {
    ocpp::v2::StatusInfo info;
    info.reasonCode = "ReadOnly";
    info.additionalInfo = ocpp::CiString<1024>(READ_ONLY_DERIVED_INFO);
    return info;
}

ocpp::v2::StatusInfo reason_code_status_info(const std::string& reason_code) {
    ocpp::v2::StatusInfo info;
    info.reasonCode = reason_code;
    return info;
}

void log_ambiguous(const ocpp::v2::Component& component, const ocpp::v2::Variable& variable) {
    EVLOG_error << "Ambiguous custom config mapping for " << cv_to_string(component, variable)
                << "; fix DeviceModelConfigMappings";
}

// mirrors ChargePointV16::set_variables' ConfigurationStatus mapping
ocpp::v2::SetVariableStatusEnum convert(ocpp::v16::ConfigurationStatus status) {
    switch (status) {
    case ocpp::v16::ConfigurationStatus::Accepted:
        return ocpp::v2::SetVariableStatusEnum::Accepted;
    case ocpp::v16::ConfigurationStatus::RebootRequired:
        return ocpp::v2::SetVariableStatusEnum::RebootRequired;
    case ocpp::v16::ConfigurationStatus::NotSupported:
        // NotSupported in OCPP1.6 means that the configuration key is not known / not supported, so it's best
        // to go with UnknownVariable
        return ocpp::v2::SetVariableStatusEnum::UnknownVariable;
    case ocpp::v16::ConfigurationStatus::Rejected:
    default:
        return ocpp::v2::SetVariableStatusEnum::Rejected;
    }
}

bool is_actual(const std::optional<ocpp::v2::AttributeEnum>& attribute_type) {
    return !attribute_type.has_value() || (attribute_type.value() == ocpp::v2::AttributeEnum::Actual);
}

} // namespace

namespace ocpp_multi {

V16VariableAccess::V16VariableAccess(const ocpp::v16::VariableResolver& resolver,
                                     ocpp::v2::DeviceModelInterface& device_model, get_keys_fn get_keys,
                                     set_key_fn set_key, on_connection_config_changed_fn on_connection_config_changed) :
    m_resolver(resolver),
    m_device_model(device_model),
    m_connection_config_validator(device_model),
    m_get_keys(std::move(get_keys)),
    m_set_key(std::move(set_key)),
    m_on_connection_config_changed(std::move(on_connection_config_changed)) {
}

void V16VariableAccess::warn_deprecated_key(const std::string& key) {
    if (!m_warned_keys.insert(key).second) {
        return; // once per key per instance
    }
    std::string canonical = "derived key, no canonical CV";
    if (const auto cv = m_resolver.key_to_cv(key)) {
        canonical = cv_to_string(cv->first, cv->second);
    }
    EVLOG_warning << "Deprecated key-only variable addressing for OCPP 1.6 key '" << key << "' (" << canonical
                  << "); use canonical ComponentVariable addressing";
}

// get() and set() route each request through exactly one of four paths:
//   path 1, legacy:     component.name empty, variable.name is a 1.6 key -> key routing, legacy result shape
//   path 2, key-backed: CV reverse-resolves to one 1.6 key -> key routing, result echoes the canonical CV
//   path 3, direct:     no 1.6 key -> device-model access; writes moderated by CVClass
//   path 4, ambiguous:  CV mapped by several custom keys -> Rejected, never a silent pick
// Checked in order 1, 4, 2, 3: ambiguity must be ruled out before a key counts as usable.
// Connection config (CVClass::ConnectionConfig) is only reachable via path 3 or a standard key whose
// reboot/reconnect semantics live in the 1.6 stack: custom mappings targeting connection-config CVs
// are rejected when the mapping file is loaded, so a custom key on paths 1/2 can never carry one.
std::vector<ocpp::v2::GetVariableResult>
V16VariableAccess::get(const std::vector<ocpp::v2::GetVariableData>& requests) {
    std::vector<ocpp::v2::GetVariableResult> results;
    results.reserve(requests.size());

    // requests routed through the 1.6 key path, resolved in one batch like the legacy implementation
    struct PendingKey {
        std::size_t index;
        std::string key;
        bool legacy;
    };
    std::vector<PendingKey> pending;

    for (const auto& request : requests) {
        const auto index = results.size();
        if (request.component.name.get().empty()) {
            // path 1, legacy: key routing with today's result shape
            const std::string key = request.variable.name.get();
            warn_deprecated_key(key);
            // UnknownVariable is a placeholder; overwritten by the batch backfill below if the key resolves
            results.push_back({ocpp::v2::GetVariableStatusEnum::UnknownVariable, {}, {request.variable.name}});
            pending.push_back({index, key, true});
            continue;
        }

        const auto reverse = m_resolver.cv_to_key(request.component, request.variable);
        if (reverse.ambiguous) {
            // path 4, ambiguous custom mapping
            log_ambiguous(request.component, request.variable);
            ocpp::v2::GetVariableResult result;
            result.attributeStatus = ocpp::v2::GetVariableStatusEnum::Rejected;
            result.component = request.component;
            result.variable = request.variable;
            result.attributeStatusInfo = ambiguous_status_info();
            results.push_back(std::move(result));
            continue;
        }
        if (reverse.usable()) {
            // path 2, key-backed canonical CV: key routing, canonical echo; key path is Actual-only
            ocpp::v2::GetVariableResult result;
            result.component = request.component;
            result.variable = request.variable;
            if (!is_actual(request.attributeType)) {
                result.attributeStatus = ocpp::v2::GetVariableStatusEnum::NotSupportedAttributeType;
                results.push_back(std::move(result));
                continue;
            }
            result.attributeStatus = ocpp::v2::GetVariableStatusEnum::UnknownVariable; // placeholder, see backfill
            results.push_back(std::move(result));
            pending.push_back({index, reverse.key.value(), false});
            continue;
        }

        // path 3, direct device-model read; identical for all CV classes (WriteOnly stays hidden)
        const auto attribute = request.attributeType.value_or(ocpp::v2::AttributeEnum::Actual);
        std::string value;
        ocpp::v2::GetVariableResult result;
        result.component = request.component;
        result.variable = request.variable;
        result.attributeStatus = m_device_model.get_variable(request.component, request.variable, attribute, value);
        if (result.attributeStatus == ocpp::v2::GetVariableStatusEnum::Accepted) {
            result.attributeValue = ocpp::CiString<2500>(value);
            result.attributeType = attribute;
        }
        results.push_back(std::move(result));
    }

    if (!pending.empty()) {
        std::vector<ocpp::CiString<50>> keys;
        keys.reserve(pending.size());
        for (const auto& entry : pending) {
            keys.push_back(entry.key);
        }
        const auto response = m_get_keys(keys);

        // canonical entries fan out to every matching key; legacy entries keep today's
        // first-match-only semantics for duplicate keys to preserve byte-parity
        if (response.configurationKey) {
            for (const auto& item : response.configurationKey.value()) {
                bool legacy_filled = false;
                for (const auto& entry : pending) {
                    if (entry.key != item.key.get()) {
                        continue;
                    }
                    if (entry.legacy) {
                        if (legacy_filled) {
                            continue; // mirror today's first-match for duplicate legacy keys
                        }
                        legacy_filled = true;
                    }
                    auto& result = results[entry.index];
                    if (item.value) {
                        result.attributeValue = item.value.value().get();
                    }
                    result.attributeStatus = ocpp::v2::GetVariableStatusEnum::Accepted;
                    result.attributeType = ocpp::v2::AttributeEnum::Actual;
                }
            }
        }
    }

    return results;
}

std::vector<SetVariableOutcome> V16VariableAccess::set(const std::vector<ocpp::v2::SetVariableData>& requests,
                                                       const std::string& source) {
    std::vector<SetVariableOutcome> outcomes;
    outcomes.reserve(requests.size());

    const auto is_committed = [](ocpp::v2::SetVariableStatusEnum status) {
        return status == ocpp::v2::SetVariableStatusEnum::Accepted ||
               status == ocpp::v2::SetVariableStatusEnum::RebootRequired;
    };
    bool connection_config_changed = false;

    for (const auto& request : requests) {
        SetVariableOutcome outcome;
        outcome.result.component = request.component;
        outcome.result.variable = request.variable;

        if (request.component.name.get().empty()) {
            // path 1, legacy key routing
            warn_deprecated_key(request.variable.name.get());
            // a custom key mapping can target a connection-config CV (no standard key does)
            const auto cv = m_resolver.key_to_cv(request.variable.name.get());
            const bool connection_config =
                cv.has_value() && m_resolver.classify(cv->first, cv->second) == ocpp::v16::CVClass::ConnectionConfig;
            if (connection_config) {
                if (const auto reason = m_connection_config_validator.validate_write(cv->first, cv->second,
                                                                                     request.attributeValue.get())) {
                    outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
                    outcome.result.attributeStatusInfo = reason_code_status_info(reason.value());
                    outcomes.push_back(std::move(outcome));
                    continue;
                }
            }
            const ocpp::CiString<500> value = static_cast<std::string>(request.attributeValue);
            outcome.result.attributeStatus = convert(m_set_key(request.variable.name, value));
            if (connection_config && is_committed(outcome.result.attributeStatus)) {
                connection_config_changed = true;
            }
            outcomes.push_back(std::move(outcome));
            continue;
        }

        const auto reverse = m_resolver.cv_to_key(request.component, request.variable);
        if (reverse.ambiguous) {
            // path 4, ambiguous custom mapping
            log_ambiguous(request.component, request.variable);
            outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
            outcome.result.attributeStatusInfo = ambiguous_status_info();
            outcomes.push_back(std::move(outcome));
            continue;
        }
        if (reverse.usable()) {
            // path 2, key-backed canonical CV: key routing, canonical echo; key path is Actual-only
            if (!is_actual(request.attributeType)) {
                outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::NotSupportedAttributeType;
                outcomes.push_back(std::move(outcome));
                continue;
            }
            const bool connection_config =
                m_resolver.classify(request.component, request.variable) == ocpp::v16::CVClass::ConnectionConfig;
            if (connection_config) {
                if (const auto reason = m_connection_config_validator.validate_write(
                        request.component, request.variable, request.attributeValue.get())) {
                    outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
                    outcome.result.attributeStatusInfo = reason_code_status_info(reason.value());
                    outcomes.push_back(std::move(outcome));
                    continue;
                }
            }
            const ocpp::CiString<500> value = static_cast<std::string>(request.attributeValue);
            outcome.result.attributeStatus = convert(m_set_key(reverse.key.value(), value));
            if (connection_config && is_committed(outcome.result.attributeStatus)) {
                connection_config_changed = true;
            }
            outcomes.push_back(std::move(outcome));
            continue;
        }

        // path 3, direct device-model write, moderated by the CV classification
        const auto cv_class = m_resolver.classify(request.component, request.variable);
        if (cv_class == ocpp::v16::CVClass::ReadOnlyDerived) {
            if (m_device_model.get_variable_meta_data(request.component, request.variable).has_value()) {
                outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
                outcome.result.attributeStatusInfo = read_only_derived_status_info();
            } else {
                // absent from the device model: probe a read to tell unknown component from unknown variable
                std::string ignored;
                const auto probe = m_device_model.get_variable(request.component, request.variable,
                                                               ocpp::v2::AttributeEnum::Actual, ignored, true);
                if (probe == ocpp::v2::GetVariableStatusEnum::UnknownComponent) {
                    outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::UnknownComponent;
                } else if (probe == ocpp::v2::GetVariableStatusEnum::UnknownVariable) {
                    outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::UnknownVariable;
                } else {
                    // metadata absent but the probe reported neither unknown component nor variable
                    EVLOG_warning << "Unexpected device-model probe status for read-only-derived CV "
                                  << cv_to_string(request.component, request.variable)
                                  << " with absent metadata; treating as UnknownVariable";
                    outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::UnknownVariable;
                }
            }
            outcomes.push_back(std::move(outcome));
            continue;
        }

        if (cv_class == ocpp::v16::CVClass::ConnectionConfig) {
            // B09-style protection of the in-use network configuration, mirroring the 2.x SetVariables path
            if (const auto reason = m_connection_config_validator.validate_write(request.component, request.variable,
                                                                                 request.attributeValue.get())) {
                outcome.result.attributeStatus = ocpp::v2::SetVariableStatusEnum::Rejected;
                outcome.result.attributeStatusInfo = reason_code_status_info(reason.value());
                outcomes.push_back(std::move(outcome));
                continue;
            }
        }

        // Free / ConnectionConfig: allow writing ReadOnly variables, mirroring the 2.x consumer path
        // (Provisioning::set_variables); this API is a trusted local channel, and mutability models the
        // CSMS-facing contract. ReadOnlyDerived CVs stay rejected above.
        const auto attribute = request.attributeType.value_or(ocpp::v2::AttributeEnum::Actual);
        const auto status = m_device_model.set_value(request.component, request.variable, attribute,
                                                     request.attributeValue.get(), source, true);
        // every committed write carries a monitor value (write-only values masked);
        // ConnectionConfig then forces RebootRequired semantics
        const bool committed = (status == ocpp::v2::SetVariableStatusEnum::Accepted) ||
                               (status == ocpp::v2::SetVariableStatusEnum::RebootRequired);
        if (committed) {
            const auto mutability = m_device_model.get_mutability(request.component, request.variable, attribute);
            outcome.monitor_value =
                (mutability == ocpp::v2::MutabilityEnum::WriteOnly) ? std::string{} : request.attributeValue.get();
            outcome.result.attributeStatus = (cv_class == ocpp::v16::CVClass::ConnectionConfig)
                                                 ? ocpp::v2::SetVariableStatusEnum::RebootRequired
                                                 : status;
            if (cv_class == ocpp::v16::CVClass::ConnectionConfig) {
                connection_config_changed = true;
            }
            if (is_actual(request.attributeType)) {
                clear_active_slot_override(request.component, request.variable);
            }
        } else {
            outcome.result.attributeStatus = status;
        }
        outcomes.push_back(std::move(outcome));
    }

    // Once per batch, mirroring the 2.x SetVariables handling
    if (connection_config_changed && m_on_connection_config_changed) {
        m_on_connection_config_changed();
    }

    return outcomes;
}

// Mirrors the 2.x SetVariables path (Provisioning::handle_variable_changed, B09.FR.26/27): a committed
// write to the SecurityCtrlr Identity/BasicAuthPassword global clears the per-slot override on the active
// NetworkConfiguration slot, so connection-time reads fall back to the new global per B09.FR.16. Without
// this, a per-slot value (e.g. written by the legacy 1.6 migration) masks the new global indefinitely.
void V16VariableAccess::clear_active_slot_override(const ocpp::v2::Component& component,
                                                   const ocpp::v2::Variable& variable) {
    namespace CC = ocpp::v2::ControllerComponentVariables;
    namespace NC = ocpp::v2::NetworkConfigurationComponentVariables;
    const ocpp::v2::ComponentVariable cv = {component, variable, std::nullopt};
    const ocpp::v2::Variable* slot_variable = nullptr;
    const char* reason_tag = nullptr;
    if (cv == CC::BasicAuthPassword) {
        slot_variable = &NC::BasicAuthPassword;
        reason_tag = "B09.FR.27";
    } else if (cv == CC::SecurityCtrlrIdentity) {
        slot_variable = &NC::Identity;
        reason_tag = "B09.FR.26";
    } else {
        return;
    }
    const auto active_slot = ocpp::v2::get_optional_value<int>(m_device_model, CC::ActiveNetworkProfile);
    if (!active_slot.has_value()) {
        return;
    }
    const auto slot_cv = NC::get_component_variable(active_slot.value(), *slot_variable);
    if (!slot_cv.variable.has_value()) {
        return;
    }
    // clear_value bypasses value validation; per-slot BasicAuthPassword declares minLimit=16, which
    // would reject the empty-string sentinel through set_value.
    const auto status =
        m_device_model.clear_value(slot_cv.component, slot_cv.variable.value(), ocpp::v2::AttributeEnum::Actual,
                                   ocpp::VARIABLE_ATTRIBUTE_VALUE_SOURCE_INTERNAL);
    if (status == ocpp::v2::SetVariableStatusEnum::Accepted) {
        EVLOG_info << "Cleared " << slot_variable->name.get() << " on active NetworkConfiguration slot "
                   << active_slot.value() << " (" << reason_tag << ")";
    } else {
        EVLOG_warning << "Could not clear " << slot_variable->name.get() << " on active NetworkConfiguration slot "
                      << active_slot.value() << " (" << reason_tag << "): set rejected";
    }
}

} // namespace ocpp_multi
