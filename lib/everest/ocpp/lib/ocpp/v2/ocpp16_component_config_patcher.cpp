// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/ocpp16_component_config_patcher.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <sstream>
#include <unordered_set>

#include <everest/logging.hpp>
#include <ocpp/v16/known_keys.hpp>
#include <ocpp/v16/utils.hpp>
#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/init_device_model_db.hpp>
#include <ocpp/v2/network_configuration_default_schema.hpp>

namespace ocpp::v2 {

namespace {

// Keys handled as special cases outside the generic mapping loop — suppress the "no mapping" warning for these.
bool is_special_case_key(const std::string& key) {
    static const std::unordered_set<std::string_view> keys = {
        "CentralSystemURI", "SecurityProfile", "AuthorizationKey",    "HostName",
        "ChargePointId",    "MeterPublicKeys", "SupportedMeasurands",
    };
    // DefaultPriceText,<lang> keys are assembled into a single JSON object by patch_default_price_text.
    return keys.count(key) || key.rfind("DefaultPriceText,", 0) == 0;
}

bool is_same_variable(const DeviceModelVariable& v1, const DeviceModelVariable& v2) {
    return ((v1.name == v2.name) && (v1.instance == v2.instance));
}

// Convert a ChargingScheduleAllowedChargingRateUnit CSL from OCPP 1.6 notation ("Current", "Power")
// to OCPP 2.x SmartChargingCtrlr.RateUnit notation ("A", "W"). Unknown tokens pass through unchanged.
std::string charging_rate_unit_csl_v16_to_v2(const std::string& v16_csl) {
    const auto tokens = ocpp::v16::utils::from_csl(v16_csl);
    std::vector<std::string> result;
    result.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (token == "Current") {
            result.push_back("A");
        } else if (token == "Power") {
            result.push_back("W");
        } else {
            result.push_back(token);
        }
    }
    return ocpp::v16::utils::to_csl(result);
}

std::optional<std::reference_wrapper<DeviceModelVariable>>
get_variable_to_patch(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                      const Component& component, const Variable& variable) {

    DeviceModelVariable variable_to_patch;
    variable_to_patch.name = variable.name.get();
    if (variable.instance.has_value()) {
        variable_to_patch.instance = variable.instance->get();
    }

    ComponentKey component_key;
    component_key.name = component.name.get();
    if (component.instance.has_value()) {
        component_key.instance = component.instance.value().get();
    }

    auto component_it = component_configs.find(component_key);
    if (component_it == component_configs.end()) {
        EVLOG_warning << "Component " << component_key.name << " not found in component configs, cannot patch variable "
                      << variable.name.get();
        return std::nullopt;
    }

    auto& variables = component_it->second;
    auto variable_it =
        std::find_if(variables.begin(), variables.end(), [&variable_to_patch](const DeviceModelVariable& var) {
            return is_same_variable(var, variable_to_patch);
        });

    if (variable_it == variables.end()) {
        EVLOG_warning << "Variable " << variable.name.get() << " not found in component " << component_key.name
                      << ", cannot patch variable.";
        return std::nullopt;
    }

    return std::ref(*variable_it);
}

std::optional<std::reference_wrapper<DeviceModelVariable>>
get_variable_to_patch(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                      const ocpp::v16::keys::DeviceModel_CV& device_model_cv) {
    if (!device_model_cv.has_value()) {
        return std::nullopt;
    }
    return get_variable_to_patch(component_configs, device_model_cv->first, device_model_cv->second);
}

ocpp::v16::keys::DeviceModel_CV component_variable_to_dm_cv(const ComponentVariable& cv) {
    if (!cv.variable.has_value()) {
        return std::nullopt;
    }
    return std::make_optional(std::make_pair(cv.component, cv.variable.value()));
}

void patch_variable_value(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                          const ocpp::v16::keys::DeviceModel_CV device_model_cv, const std::string& value,
                          const std::optional<bool>& readonly_override = std::nullopt, bool allow_override = true) {

    auto variable_to_patch = get_variable_to_patch(component_configs, device_model_cv);
    if (!variable_to_patch.has_value()) {
        return;
    }

    auto& dm_variable = variable_to_patch->get();
    auto attribute_it =
        std::find_if(dm_variable.attributes.begin(), dm_variable.attributes.end(), [](const DbVariableAttribute& attr) {
            return attr.variable_attribute.type.has_value() and
                   attr.variable_attribute.type.value() == AttributeEnum::Actual;
        });

    if (attribute_it == dm_variable.attributes.end()) {
        EVLOG_warning << "Attribute Actual not found in variable " << dm_variable.name << ", cannot patch variable.";
        return;
    }

    if (!allow_override && attribute_it->variable_attribute.value.has_value()) {
        EVLOG_debug << "Skipping OCPP 1.6 patch for " << dm_variable.name
                    << ": attribute already has a value in the component config";
        return;
    }

    if (readonly_override.has_value()) {
        attribute_it->variable_attribute.mutability =
            readonly_override.value() ? MutabilityEnum::ReadOnly : MutabilityEnum::ReadWrite;
    }

    attribute_it->value_source = "OCPP16Config";
    attribute_it->variable_attribute.value = value;
}

void patch_variable_value(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                          const ComponentVariable& cv, const std::string& value, bool allow_override = true) {
    patch_variable_value(component_configs, component_variable_to_dm_cv(cv), value, std::nullopt, allow_override);
}

void patch_variable_max_limit(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                              const ocpp::v16::keys::DeviceModel_CV& device_model_cv, const int32_t max_limit) {
    if (auto variable_to_patch = get_variable_to_patch(component_configs, device_model_cv)) {
        variable_to_patch->get().characteristics.maxLimit = static_cast<double>(max_limit);
    }
}

void patch_variable_values_list(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                                const ocpp::v16::keys::DeviceModel_CV& device_model_cv,
                                const std::string& values_list) {

    auto variable_to_patch = get_variable_to_patch(component_configs, device_model_cv);
    if (!variable_to_patch.has_value()) {
        return;
    }

    variable_to_patch->get().characteristics.valuesList = values_list;
}

void patch_optional_max_limit(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                              const std::string& ocpp16_key, const std::optional<int32_t>& max_limit) {
    if (!max_limit.has_value()) {
        return;
    }

    patch_variable_max_limit(component_configs, ocpp::v16::keys::convert_v2(ocpp16_key), max_limit.value());
}

void patch_max_limit(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                     const char* ocpp16_key, const int32_t max_limit) {
    patch_variable_max_limit(component_configs, ocpp::v16::keys::convert_v2(ocpp16_key), max_limit);
}

void patch_max_limit_from_key(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                              ocpp::v16::ChargePointConfiguration& ocpp16_config, const char* ocpp16_key) {
    try {
        const auto key_value = ocpp16_config.get(ocpp16_key);
        if (!key_value.has_value() || !key_value->value.has_value()) {
            return;
        }
        const auto max_limit = std::stoi(key_value->value.value().get());
        patch_max_limit(component_configs, ocpp16_key, max_limit);
    } catch (const std::exception& e) {
        EVLOG_warning << "Could not read or parse max-limit key " << ocpp16_key << ": " << e.what();
    }
}

void patch_meter_public_keys(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                             ocpp::v16::ChargePointConfiguration& ocpp16_config) {

    const auto meter_public_keys = ocpp16_config.getMeterPublicKeys();
    if (!meter_public_keys.has_value()) {
        return;
    }

    const auto meter_public_keys_csl = ocpp16_config.getMeterPublicKeysCsl();
    if (!meter_public_keys_csl.has_value()) {
        return;
    }

    patch_variable_value(component_configs, ocpp::v16::keys::convert_v2("MeterPublicKeys"),
                         meter_public_keys_csl.value());
}

void patch_default_price_text(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                              ocpp::v16::ChargePointConfiguration& ocpp16_config) {
    const auto default_price_text = ocpp16_config.getDefaultPriceText();
    if (!default_price_text.has_value()) {
        return;
    }

    patch_variable_value(component_configs, ocpp::v16::keys::convert_v2("DefaultPriceText"),
                         default_price_text.value().dump());
}

void patch_supported_measurands(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                                ocpp::v16::ChargePointConfiguration& ocpp16_config) {

    const auto supported_measurands = ocpp16_config.getSupportedMeasurands();

    const std::array<ocpp::v16::keys::DeviceModel_CV, 5> measurands_mappings = {
        std::make_pair(ocpp::v2::Component{"AlignedDataCtrlr"}, ocpp::v2::Variable{"Measurands"}),
        std::make_pair(ocpp::v2::Component{"AlignedDataCtrlr"}, ocpp::v2::Variable{"TxEndedMeasurands"}),
        std::make_pair(ocpp::v2::Component{"SampledDataCtrlr"}, ocpp::v2::Variable{"TxEndedMeasurands"}),
        std::make_pair(ocpp::v2::Component{"SampledDataCtrlr"}, ocpp::v2::Variable{"TxStartedMeasurands"}),
        std::make_pair(ocpp::v2::Component{"SampledDataCtrlr"}, ocpp::v2::Variable{"TxUpdatedMeasurands"}),
    };

    for (const auto& mapping : measurands_mappings) {
        patch_variable_values_list(component_configs, mapping, supported_measurands);
    }
}

} // namespace

void patch_component_config_with_ocpp16(std::map<ComponentKey, std::vector<DeviceModelVariable>>& component_configs,
                                        ocpp::v16::ChargePointConfiguration& ocpp16_config,
                                        const Ocpp16CustomConfigMappings& custom_config_mappings,
                                        int32_t network_config_slot) {

    const auto config_keys = ocpp16_config.get_all_key_value();

    for (const auto& config_key : config_keys) {
        const auto key_enum = ocpp::v16::keys::convert(config_key.key.get());
        if (key_enum && ocpp::v16::keys::is_max_limit_key(*key_enum)) {
            continue;
        }

        auto mapping_opt = ocpp::v16::keys::convert_v2(config_key.key.get());

        if (!mapping_opt.has_value()) {
            const auto custom_mapping_it = custom_config_mappings.find(config_key.key.get());
            if (custom_mapping_it != custom_config_mappings.end()) {
                const auto& [comp, var] = custom_mapping_it->second;
                mapping_opt = std::make_pair(comp, var);
            }
        }

        if (!mapping_opt.has_value()) {
            if (!is_special_case_key(config_key.key.get())) {
                EVLOG_warning << "No OCPP 2.x mapping found for OCPP 1.6 config key " << config_key.key.get()
                              << ", skipping patching variable.";
            }
            continue;
        }

        try {
            std::string value = config_key.value.value().get();
            if (key_enum == ocpp::v16::keys::valid_keys::ChargingScheduleAllowedChargingRateUnit) {
                value = charging_rate_unit_csl_v16_to_v2(value);
            }
            patch_variable_value(component_configs, mapping_opt, value, config_key.readonly);
        } catch (const std::exception& e) {
            EVLOG_error << "Failed to patch OCPP 1.6 config key " << config_key.key.get() << " with value "
                        << config_key.value.value().get() << ": " << e.what();
            throw;
        }
    }

    // Handling for special cases that cannot be covered by the generic mapping above:

    if (network_config_slot > 0) {
        namespace NC = ocpp::v2::NetworkConfigurationComponentVariables;

        ComponentKey nc_component_key;
        nc_component_key.name = "NetworkConfiguration";
        nc_component_key.instance = std::to_string(network_config_slot);
        if (component_configs.find(nc_component_key) == component_configs.end()) {
            EVLOG_info << "NetworkConfiguration[" << network_config_slot
                       << "] not found in component configs, injecting from embedded default schema.";
            try {
                auto [key, vars] = parse_component_config_from_string(get_default_network_configuration_schema());
                key.instance = std::to_string(network_config_slot);
                component_configs[key] = std::move(vars);
            } catch (const std::exception& e) {
                EVLOG_error << "Failed to inject NetworkConfiguration[" << network_config_slot
                            << "] from embedded default schema: " << e.what()
                            << " — skipping network connection migration.";
            }
        }
        if (component_configs.find(nc_component_key) != component_configs.end()) {
            // CentralSystemURI -> NetworkConfiguration[N].OcppCsmsUrl
            {
                const auto uri = ocpp16_config.getCentralSystemURI();
                if (!uri.empty()) {
                    patch_variable_value(component_configs,
                                         NC::get_component_variable(network_config_slot, NC::OcppCsmsUrl), uri);
                }
            }
            // SecurityProfile -> NetworkConfiguration[N].SecurityProfile
            patch_variable_value(component_configs,
                                 NC::get_component_variable(network_config_slot, NC::SecurityProfile),
                                 std::to_string(ocpp16_config.getSecurityProfile()), true);
            // AuthorizationKey -> NetworkConfiguration[N].BasicAuthPassword
            if (const auto ak = ocpp16_config.getAuthorizationKey(); ak.has_value()) {
                patch_variable_value(component_configs,
                                     NC::get_component_variable(network_config_slot, NC::BasicAuthPassword),
                                     ak.value());
            }
            // HostName -> NetworkConfiguration[N].HostName
            if (const auto hn = ocpp16_config.getHostName(); hn.has_value()) {
                patch_variable_value(component_configs,
                                     NC::get_component_variable(network_config_slot, ocpp::v2::Variable{"HostName"}),
                                     hn.value());
            }
            // ChargePointId -> NetworkConfiguration[N].Identity
            {
                const auto id = ocpp16_config.getChargePointId();
                if (!id.empty()) {
                    patch_variable_value(component_configs,
                                         NC::get_component_variable(network_config_slot, NC::Identity), id);
                }
            }
        }
    }

    // MeterPublicKeys -> MeterPublicKey[0], MeterPublicKey[1], ...
    patch_meter_public_keys(component_configs, ocpp16_config);
    // DefaultPriceText,<lang> -> OCPP16LegacyCtrlr:DefaultPriceText as combined JSON object
    patch_default_price_text(component_configs, ocpp16_config);
    // MeterValuesAlignedDataMaxLength -> AlignedDataCtrlr:Measurands.maxLimit
    patch_optional_max_limit(component_configs, "MeterValuesAlignedDataMaxLength",
                             ocpp16_config.getMeterValuesAlignedDataMaxLength());
    // MeterValuesSampledDataMaxLength -> SampledDataCtrlr:TxUpdatedMeasurands.maxLimit
    patch_optional_max_limit(component_configs, "MeterValuesSampledDataMaxLength",
                             ocpp16_config.getMeterValuesSampledDataMaxLength());
    // StopTxnAlignedDataMaxLength -> AlignedDataCtrlr:TxEndedMeasurands.maxLimit
    patch_optional_max_limit(component_configs, "StopTxnAlignedDataMaxLength",
                             ocpp16_config.getStopTxnAlignedDataMaxLength());
    // StopTxnSampledDataMaxLength -> SampledDataCtrlr:TxEndedMeasurands.maxLimit
    patch_optional_max_limit(component_configs, "StopTxnSampledDataMaxLength",
                             ocpp16_config.getStopTxnSampledDataMaxLength());
    // LocalAuthListMaxLength -> LocalAuthListCtrlr:Entries.maxLimit
    patch_max_limit_from_key(component_configs, ocpp16_config, "LocalAuthListMaxLength");
    // MaxChargingProfilesInstalled -> SmartChargingCtrlr.ChargingProfileEntries.maxLimit
    patch_max_limit_from_key(component_configs, ocpp16_config, "MaxChargingProfilesInstalled");
    // CertificateStoreMaxLength -> SecurityCtrlr:CertificateEntries.maxLimit
    patch_optional_max_limit(component_configs, "CertificateStoreMaxLength",
                             ocpp16_config.getCertificateStoreMaxLength());
    // SupportedMeasurands -> *Measurands.characteristics.valuesList (patches for all aligned and sampled data
    // measurands variables)
    patch_supported_measurands(component_configs, ocpp16_config);
}

} // namespace ocpp::v2
