// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "include/configuration_type_wrapper.hpp"

#include <utils/mqtt_config_service.hpp>

namespace Everest::api::types::configuration {

namespace {

template <class SrcT, class ConvT>
auto srcToTarOpt(std::optional<SrcT> const& src, ConvT const& converter)
    -> std::optional<decltype(converter(src.value()))> {
    if (src) {
        return std::make_optional(converter(src.value()));
    }
    return std::nullopt;
}

template <class SrcT, class ConvT> auto srcToTarVec(std::vector<SrcT> const& src, ConvT const& converter) {
    using TarT = decltype(converter(src[0]));
    std::vector<TarT> result;
    for (SrcT const& elem : src) {
        result.push_back(converter(elem));
    }
    return result;
}

template <class SrcT>
auto optToExternal(std::optional<SrcT> const& src) -> std::optional<decltype(to_external_api(src.value()))> {
    return srcToTarOpt(src, [](SrcT const& val) { return to_external_api(val); });
}

template <class SrcT> auto vecToExternal(std::vector<SrcT> const& src) {
    return srcToTarVec(src, [](SrcT const& val) { return to_external_api(val); });
}

} // namespace

MarkActiveSlotResultEnum_External to_external_api(MarkActiveSlotResultEnum_Internal const& val) {
    using SrcT = MarkActiveSlotResultEnum_Internal;
    using TarT = MarkActiveSlotResultEnum_External;
    switch (val) {
    case SrcT::Success:
        return TarT::Success;
    case SrcT::NoChangeRequired:
        return TarT::NoChangeRequired;
    case SrcT::DoesNotExist:
        return TarT::DoesNotExist;
    case SrcT::Failed:
        return TarT::Failed;
    }
    throw std::out_of_range("Unexpected value for everest::config::SetActiveSlotStatus");
}

DeleteSlotResultEnum_External to_external_api(DeleteSlotResultEnum_Internal const& val) {
    using SrcT = DeleteSlotResultEnum_Internal;
    using TarT = DeleteSlotResultEnum_External;
    switch (val) {
    case SrcT::Success:
        return TarT::Success;
    case SrcT::CannotDeleteActiveSlot:
        return TarT::CannotDeleteActiveSlot;
    case SrcT::DoesNotExist:
        return TarT::DoesNotExist;
    case SrcT::Failed:
        return TarT::Failed;
    }
    throw std::out_of_range("Unexpected value for everest::config::DeleteSlotStatus");
}

ConfigMetadata_External to_external_api(ConfigMetadata_Internal const& val) {
    ConfigMetadata_External result;
    result.slot_id = val.id;
    result.last_updated = val.last_updated;
    result.config_file_path = val.config_file_path;
    result.description = val.description;
    return result;
}

ListSlotIdsResult_External to_external_api(ListSlotIdsResult_Internal const& val) {
    ListSlotIdsResult_External result;
    for (ConfigMetadata_Internal const& slot : val) {
        result.slots.push_back(to_external_api(slot));
    }
    return result;
}

DuplicateSlotResult_External to_external_api(DuplicateSlotResult_Internal const& val) {
    DuplicateSlotResult_External result;
    result.success = val.success;
    result.slot_id = val.slot_id;
    return result;
}

LoadFromYamlResult_External to_external_api(LoadFromYamlResult_Internal const& val) {
    LoadFromYamlResult_External result;
    result.success = val.success;
    result.slot_id = val.slot_id;
    result.error_message = val.error_message;
    return result;
}

ConfigurationParameterUpdateResultEnum_External
to_external_api(ConfigurationParameterUpdateResultEnum_Internal const& val) {
    using SrcT = ConfigurationParameterUpdateResultEnum_Internal;
    using TarT = ConfigurationParameterUpdateResultEnum_External;
    switch (val) {
    case SrcT::Applied:
        return TarT::Applied;
    case SrcT::WillApplyOnRestart:
        return TarT::WillApplyOnRestart;
    case SrcT::DoesNotExist:
        return TarT::DoesNotExist;
    case SrcT::RetryLater:
        return TarT::RetryLater;
    case SrcT::AccessDenied:
        return TarT::AccessDenied;
    case SrcT::Rejected:
        return TarT::Rejected;
    }
    throw std::out_of_range("Unexpected value for Everest::config::SetConfigParameterResultEnum");
}

ActiveSlotStatusEnum_External to_external_api(ActiveSlotStatusEnum_Internal const& val) {
    using SrcT = ActiveSlotStatusEnum_Internal;
    using TarT = ActiveSlotStatusEnum_External;
    switch (val) {
    case SrcT::Running:
        return TarT::Running;
    case SrcT::Stopped:
        return TarT::Stopped;
    case SrcT::Starting:
        return TarT::Starting;
    case SrcT::Stopping:
        return TarT::Stopping;
    case SrcT::FailedToStart:
        return TarT::FailedToStart;
    case SrcT::RestartTriggered:
        return TarT::RestartTriggered;
    }
    throw std::out_of_range("Unexpected value for Everest::config::ActiveSlotStatus");
}

ConfigurationParameterDatatype_External to_external_api(ConfigurationParameterDatatype_Internal const& val) {
    using SrcT = ConfigurationParameterDatatype_Internal;
    using TarT = ConfigurationParameterDatatype_External;
    switch (val) {
    case SrcT::Integer:
        return TarT::Integer;
    case SrcT::Decimal:
        return TarT::Decimal;
    case SrcT::String:
        return TarT::String;
    case SrcT::Boolean:
        return TarT::Boolean;
    case SrcT::Unknown:
        return TarT::Unknown;
    }
    throw std::out_of_range("Unexpected value for everest::config::Datatype");
}

ConfigurationParameterMutability_External to_external_api(ConfigurationParameterMutability_Internal const& val) {
    using SrcT = ConfigurationParameterMutability_Internal;
    using TarT = ConfigurationParameterMutability_External;
    switch (val) {
    case SrcT::ReadOnly:
        return TarT::ReadOnly;
    case SrcT::ReadWrite:
        return TarT::ReadWrite;
    case SrcT::WriteOnly:
        return TarT::WriteOnly;
    }
    throw std::out_of_range("Unexpected value for everest::config::Mutability");
}

GetConfigurationStatusEnum_External to_external_api(GetConfigurationStatusEnum_Internal const& val) {
    using SrcT = GetConfigurationStatusEnum_Internal;
    using TarT = GetConfigurationStatusEnum_External;
    switch (val) {
    case SrcT::Success:
        return TarT::Success;
    case SrcT::SlotDoesNotExist:
        return TarT::SlotDoesNotExist;
    case SrcT::Failed:
        return TarT::Failed;
    }
    throw std::out_of_range("Unexpected value for Everest::config::GetConfigurationStatus");
}

ConfigurationParameterIdentifier_Internal to_internal_api(ConfigurationParameterIdentifier_External const& val) {
    ConfigurationParameterIdentifier_Internal result;
    result.module_id = val.module_id;
    result.configuration_parameter_name = val.parameter_name;
    result.module_implementation_id = val.implementation_id.value_or(::Everest::config::MODULE_IMPLEMENTATION_ID);
    return result;
}

ConfigurationParameterIdentifier_External to_external_api(ConfigurationParameterIdentifier_Internal const& val) {
    ConfigurationParameterIdentifier_External result;
    result.module_id = val.module_id;
    result.parameter_name = val.configuration_parameter_name;
    if (val.module_implementation_id.has_value() and
        val.module_implementation_id.value() != ::Everest::config::MODULE_IMPLEMENTATION_ID) {
        result.implementation_id = val.module_implementation_id;
    }
    return result;
}

ConfigurationParameterUpdate_Internal to_internal_api(ConfigurationParameterUpdate_External const& val) {
    ConfigurationParameterUpdate_Internal result;
    result.identifier = to_internal_api(val.cfg_param_id);
    result.value = val.value;
    return result;
}

ConfigurationParameterUpdate_External to_external_api(ConfigurationParameterUpdate_Internal const& val) {
    ConfigurationParameterUpdate_External result;
    result.cfg_param_id = to_external_api(val.identifier);
    result.value = val.value;
    return result;
}

OriginOfChange_External to_external_api(OriginOfChange_Internal const& val) {
    OriginOfChange_External result;
    result.external = val.external;
    result.identifier = val.module_id;
    return result;
}

ActiveSlotUpdateNotice_External to_external_api(ActiveSlotUpdateNotice_Internal const& val) {
    ActiveSlotUpdateNotice_External result;
    result.tstamp = val.timestamp;
    result.active_slot_id = val.active_slot_id;
    result.next_boot_slot_id = val.next_boot_slot_id;
    result.status = to_external_api(val.status);
    // val.cause is deliberately not mapped: it tells internal consumers which subset of the event
    // stream they care about, and this notice reports every event regardless of cause.
    return result;
}

ConfigurationParameterUpdateResultRecord_External
to_external_api(ConfigurationParameterUpdateResultRecord_Internal const& val) {
    ConfigurationParameterUpdateResultRecord_External result;
    result.update.cfg_param_id = to_external_api(val.identifier);
    result.update.value = val.value;
    result.result = to_external_api(val.result);
    return result;
}

ConfigurationParameterUpdateNotice_External to_external_api(ConfigurationParameterUpdateNotice_Internal const& val) {
    ConfigurationParameterUpdateNotice_External result;
    result.tstamp = val.timestamp;
    result.slot_id = val.slot_id;
    result.update_results = vecToExternal(val.updates);
    result.origin = to_external_api(val.origin);
    return result;
}

Mapping_External to_external_api(Mapping_Internal const& val) {
    Mapping_External result;
    result.evse = val.evse;
    if (val.connector) {
        result.connector = val.connector.value();
    }
    return result;
}

ModuleTierMappings_External to_external_api(ModuleTierMappings_Internal const& val) {
    ModuleTierMappings_External result;
    result.module = optToExternal(val.module);
    for (auto const& [impl_id, mapping] : val.implementations) {
        if (mapping) {
            result.implementations.push_back({impl_id, to_external_api(mapping.value())});
        }
    }
    return result;
}

ReqFulfillment_External to_external_api(ReqFulfillment_Internal const& val) {
    ReqFulfillment_External result;
    result.module_id = val.module_id;
    result.implementation_id = val.implementation_id;
    result.index = val.requirement.index;
    return result;
}

ConfigurationParameterCharacteristics_External
to_external_api(ConfigurationParameterCharacteristics_Internal const& val) {
    ConfigurationParameterCharacteristics_External result;
    result.datatype = to_external_api(val.datatype);
    result.mutability = to_external_api(val.mutability);
    result.unit = val.unit;
    result.min_value = val.min_value;
    result.max_value = val.max_value;
    return result;
}

ConfigurationParameter_External to_external_api(ConfigurationParameter_Internal const& val) {
    ConfigurationParameter_External result;
    result.name = val.name;
    result.characteristics = to_external_api(val.characteristics);
    result.value = std::visit(::everest::config::VisitConfigEntry{}, val.value);
    return result;
}

TelemetryConfig_External to_external_api(TelemetryConfig_Internal const& val) {
    TelemetryConfig_External result;
    result.id = val.id;
    return result;
}

ModuleConfigAccess_External to_external_api(ModuleConfigAccess_Internal const& val, std::string const& module_id) {
    ModuleConfigAccess_External result;
    result.module_id = module_id;
    result.allow_read = val.allow_read;
    result.allow_write = val.allow_write;
    result.allow_set_read_only = val.allow_set_read_only;
    return result;
}

ConfigAccess_External to_external_api(ConfigAccess_Internal const& val) {
    ConfigAccess_External result;
    result.allow_global_read = val.allow_global_read;
    result.allow_global_write = val.allow_global_write;
    result.allow_set_read_only = val.allow_set_read_only;
    for (auto const& [module_id, mod_cfg_access] : val.modules) {
        result.module_config_access.push_back(to_external_api(mod_cfg_access, module_id));
    }
    return result;
}

ConfigAccessControl_External to_external_api(ConfigAccessControl_Internal const& val) {
    ConfigAccessControl_External result;
    result.config = optToExternal(val.config);
    return result;
}

ModuleConfiguration_External to_external_api(ModuleConfiguration_Internal const& val) {
    ModuleConfiguration_External result;
    result.module_id = val.module_id;
    result.module_name = val.module_name;
    for (auto const& [requirement_id, fulfillments] : val.connections) {
        ::everest::lib::API::V1_0::types::configuration::ModuleConnection connection_external;
        connection_external.requirement_id = requirement_id;
        connection_external.fulfillments = vecToExternal(fulfillments);
        result.connections.push_back(connection_external);
    }
    result.mapping = to_external_api(val.mapping);

    for (auto const& [impl_id, config_params] : val.configuration_parameters) {
        if (impl_id == ::Everest::config::MODULE_IMPLEMENTATION_ID) {
            for (auto const& param : config_params) {
                result.module_configuration_parameters.push_back(to_external_api(param));
            }
        } else {
            ::everest::lib::API::V1_0::types::configuration::ImplementationConfigurationParameter
                impl_config_param_external;
            impl_config_param_external.implementation_id = impl_id;
            impl_config_param_external.configuration_parameters = vecToExternal(config_params);
            result.implementation_configuration_parameters.push_back(impl_config_param_external);
        }
    }

    result.standalone = val.standalone;
    result.telemetry_enabled = val.telemetry_enabled;
    result.telemetry_config = optToExternal(val.telemetry_config);
    result.config_access = to_external_api(val.access);

    return result;
}

GetConfigurationResult_External to_external_api(GetConfigurationResult_Internal const& val) {
    GetConfigurationResult_External result;
    result.status = to_external_api(val.status);
    if (val.status == GetConfigurationStatusEnum_Internal::Success) {
        result.module_configurations.emplace();
        for (auto const& [module_id, module_config_internal] : val.module_configurations) {
            result.module_configurations->emplace_back(to_external_api(module_config_internal));
        }
    }
    return result;
}

ConfigurationParameterGetValueResult_External
to_external_api(ConfigurationParameterGetValueResult_Internal const& val) {
    ConfigurationParameterGetValueResult_External result;

    if (val.has_value()) {
        result.status = ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterGetResultEnum::OK;
        result.parameter = {to_external_api(val.value())};
    } else {
        result.status =
            ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterGetResultEnum::DoesNotExist;
        result.parameter = std::nullopt;
    }
    return result;
}

} // namespace Everest::api::types::configuration
