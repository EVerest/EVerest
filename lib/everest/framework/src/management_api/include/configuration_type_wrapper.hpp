// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest_api_types/configuration/API.hpp>

#include <utils/config_service_interface.hpp>

/// \file Conversions between the manager's internal configuration types and the external (on-the-wire)
/// configuration API types.
///
/// The conversion is asymmetric on purpose: the manager only ever answers requests, so it converts
/// internal -> external for every reply and notice, but external -> internal only for the two request
/// payloads it has to read back (a parameter identifier and a parameter update). The remaining
/// external -> internal direction was unused and has been removed; a future C++ client library that
/// needs it should reintroduce it together with its call sites.
namespace Everest::api::types::configuration {

using MarkActiveSlotResultEnum_Internal = ::Everest::config::SetActiveSlotStatus;
using MarkActiveSlotResultEnum_External = ::everest::lib::API::V1_0::types::configuration::MarkActiveSlotResultEnum;

MarkActiveSlotResultEnum_External to_external_api(MarkActiveSlotResultEnum_Internal const& val);

using DeleteSlotResultEnum_Internal = ::Everest::config::DeleteSlotStatus;
using DeleteSlotResultEnum_External = ::everest::lib::API::V1_0::types::configuration::DeleteSlotResultEnum;

DeleteSlotResultEnum_External to_external_api(DeleteSlotResultEnum_Internal const& val);

using ConfigurationParameterUpdateResultEnum_Internal = ::Everest::config::SetConfigParameterResultEnum;
using ConfigurationParameterUpdateResultEnum_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterUpdateResultEnum;

ConfigurationParameterUpdateResultEnum_External
to_external_api(ConfigurationParameterUpdateResultEnum_Internal const& val);

using ActiveSlotStatusEnum_Internal = ::Everest::config::ActiveSlotStatus;
using ActiveSlotStatusEnum_External = ::everest::lib::API::V1_0::types::configuration::ActiveSlotStatusEnum;

ActiveSlotStatusEnum_External to_external_api(ActiveSlotStatusEnum_Internal const& val);

using ConfigurationParameterDatatype_Internal = ::everest::config::Datatype;
using ConfigurationParameterDatatype_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterDatatype;

ConfigurationParameterDatatype_External to_external_api(ConfigurationParameterDatatype_Internal const& val);

using ConfigurationParameterMutability_Internal = ::everest::config::Mutability;
using ConfigurationParameterMutability_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterMutability;

ConfigurationParameterMutability_External to_external_api(ConfigurationParameterMutability_Internal const& val);

using GetConfigurationStatusEnum_Internal = ::Everest::config::GetConfigurationStatus;
using GetConfigurationStatusEnum_External = ::everest::lib::API::V1_0::types::configuration::GetConfigurationStatusEnum;

GetConfigurationStatusEnum_External to_external_api(GetConfigurationStatusEnum_Internal const& val);

using ConfigMetadata_Internal = ::everest::config::SlotInfo;
using ConfigMetadata_External = ::everest::lib::API::V1_0::types::configuration::ConfigMetadata;

ConfigMetadata_External to_external_api(ConfigMetadata_Internal const& val);

using ListSlotIdsResult_Internal = ::std::vector<::everest::config::SlotInfo>;
using ListSlotIdsResult_External = ::everest::lib::API::V1_0::types::configuration::ListSlotIdsResult;

ListSlotIdsResult_External to_external_api(ListSlotIdsResult_Internal const& val);

using DuplicateSlotResult_Internal = ::everest::config::DuplicateSlotResult;
using DuplicateSlotResult_External = ::everest::lib::API::V1_0::types::configuration::DuplicateSlotResult;

DuplicateSlotResult_External to_external_api(DuplicateSlotResult_Internal const& val);

using LoadFromYamlResult_Internal = ::Everest::config::LoadFromYamlResult;
using LoadFromYamlResult_External = ::everest::lib::API::V1_0::types::configuration::LoadFromYamlResult;

LoadFromYamlResult_External to_external_api(LoadFromYamlResult_Internal const& val);

using ConfigurationParameterIdentifier_Internal = ::everest::config::ConfigurationParameterIdentifier;
using ConfigurationParameterIdentifier_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterIdentifier;

ConfigurationParameterIdentifier_Internal to_internal_api(ConfigurationParameterIdentifier_External const& val);
ConfigurationParameterIdentifier_External to_external_api(ConfigurationParameterIdentifier_Internal const& val);

using ConfigurationParameterUpdate_Internal = ::Everest::config::ConfigParameterUpdate;
using ConfigurationParameterUpdate_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterUpdate;

ConfigurationParameterUpdate_Internal to_internal_api(ConfigurationParameterUpdate_External const& val);
ConfigurationParameterUpdate_External to_external_api(ConfigurationParameterUpdate_Internal const& val);

using OriginOfChange_Internal = ::Everest::config::Origin;
using OriginOfChange_External = ::everest::lib::API::V1_0::types::configuration::OriginOfChange;

OriginOfChange_External to_external_api(OriginOfChange_Internal const& val);

using ActiveSlotUpdateNotice_Internal = ::Everest::config::ActiveSlotUpdate;
using ActiveSlotUpdateNotice_External = ::everest::lib::API::V1_0::types::configuration::ActiveSlotUpdateNotice;

ActiveSlotUpdateNotice_External to_external_api(ActiveSlotUpdateNotice_Internal const& val);

using ConfigurationParameterUpdateResultRecord_Internal = ::Everest::config::ConfigParameterUpdateNotice;
using ConfigurationParameterUpdateResultRecord_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterUpdateResultRecord;

ConfigurationParameterUpdateResultRecord_External
to_external_api(ConfigurationParameterUpdateResultRecord_Internal const& val);

using ConfigurationParameterUpdateNotice_Internal = ::Everest::config::ConfigurationUpdate;
using ConfigurationParameterUpdateNotice_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterUpdateNotice;

ConfigurationParameterUpdateNotice_External to_external_api(ConfigurationParameterUpdateNotice_Internal const& val);

using Mapping_Internal = Mapping;
using Mapping_External = ::everest::lib::API::V1_0::types::configuration::Mapping;

Mapping_External to_external_api(Mapping_Internal const& val);

using ModuleTierMappings_Internal = ModuleTierMappings;
using ModuleTierMappings_External = ::everest::lib::API::V1_0::types::configuration::ModuleTierMappings;

ModuleTierMappings_External to_external_api(ModuleTierMappings_Internal const& val);

using ReqFulfillment_Internal = Fulfillment;
using ReqFulfillment_External = ::everest::lib::API::V1_0::types::configuration::ReqFulfillment;

ReqFulfillment_External to_external_api(ReqFulfillment_Internal const& val);

using ConfigurationParameterCharacteristics_Internal = ::everest::config::ConfigurationParameterCharacteristics;
using ConfigurationParameterCharacteristics_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterCharacteristics;

ConfigurationParameterCharacteristics_External
to_external_api(ConfigurationParameterCharacteristics_Internal const& val);

using ConfigurationParameter_Internal = ::everest::config::ConfigurationParameter;
using ConfigurationParameter_External = ::everest::lib::API::V1_0::types::configuration::ConfigurationParameter;

ConfigurationParameter_External to_external_api(ConfigurationParameter_Internal const& val);

using TelemetryConfig_Internal = TelemetryConfig;
using TelemetryConfig_External = ::everest::lib::API::V1_0::types::configuration::TelemetryConfig;

TelemetryConfig_External to_external_api(TelemetryConfig_Internal const& val);

using ModuleConfigAccess_Internal = ::everest::config::ModuleConfigAccess;
using ModuleConfigAccess_External = ::everest::lib::API::V1_0::types::configuration::ModuleConfigAccess;

// The module id is not part of the internal per-module access record (it is the map key there), so it
// has to be passed in to fill the external type's module_id field.
ModuleConfigAccess_External to_external_api(ModuleConfigAccess_Internal const& val, std::string const& module_id);

using ConfigAccess_Internal = ::everest::config::ConfigAccess;
using ConfigAccess_External = ::everest::lib::API::V1_0::types::configuration::ConfigAccess;

ConfigAccess_External to_external_api(ConfigAccess_Internal const& val);

using ConfigAccessControl_Internal = ::everest::config::Access;
using ConfigAccessControl_External = ::everest::lib::API::V1_0::types::configuration::ConfigAccessControl;

ConfigAccessControl_External to_external_api(ConfigAccessControl_Internal const& val);

using ModuleConfiguration_Internal = ::everest::config::ModuleConfig;
using ModuleConfiguration_External = ::everest::lib::API::V1_0::types::configuration::ModuleConfiguration;

ModuleConfiguration_External to_external_api(ModuleConfiguration_Internal const& val);

using GetConfigurationResult_Internal = ::Everest::config::GetConfigurationResult;
using GetConfigurationResult_External = ::everest::lib::API::V1_0::types::configuration::GetConfigurationResult;

GetConfigurationResult_External to_external_api(GetConfigurationResult_Internal const& val);

using ConfigurationParameterGetValueResult_Internal = std::optional<everest::config::ConfigurationParameter>;
using ConfigurationParameterGetValueResult_External =
    ::everest::lib::API::V1_0::types::configuration::ConfigurationParameterGetValueResult;

ConfigurationParameterGetValueResult_External to_external_api(ConfigurationParameterGetValueResult_Internal const& val);

} // namespace Everest::api::types::configuration
