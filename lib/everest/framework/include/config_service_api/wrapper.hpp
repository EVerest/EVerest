// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>

#include <everest_api_types/config_service/API.hpp>

//TODO(CB): Are these pragmas required?
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "utils/config_service_interface.hpp"
#pragma GCC diagnostic pop

namespace everest::config::api::types {

using MarkActiveSlotResultEnum_Internal = ::Everest::config::SetActiveSlotStatus;
using MarkActiveSlotResultEnum_External = ::everest::lib::API::V1_0::types::config_service::MarkActiveSlotResultEnum;

MarkActiveSlotResultEnum_Internal to_internal_api(MarkActiveSlotResultEnum_External const& val);
MarkActiveSlotResultEnum_External to_external_api(MarkActiveSlotResultEnum_Internal const& val);

// using StopModulesResultEnum_Internal = ::types::config_service::StopModulesResultEnum;
// using StopModulesResultEnum_External = ::everest::lib::API::V1_0::types::config_service::StopModulesResultEnum;

// StopModulesResultEnum_Internal to_internal_api(StopModulesResultEnum_External const& val);
// StopModulesResultEnum_External to_external_api(StopModulesResultEnum_Internal const& val);

// using StartModulesResultEnum_Internal = ::types::config_service::StartModulesResultEnum;
// using StartModulesResultEnum_External = ::everest::lib::API::V1_0::types::config_service::StartModulesResultEnum;

// StartModulesResultEnum_Internal to_internal_api(StartModulesResultEnum_External const& val);
// StartModulesResultEnum_External to_external_api(StartModulesResultEnum_Internal const& val);

using DeleteSlotResultEnum_Internal = ::Everest::config::DeleteSlotStatus;
using DeleteSlotResultEnum_External = ::everest::lib::API::V1_0::types::config_service::DeleteSlotResultEnum;

DeleteSlotResultEnum_Internal to_internal_api(DeleteSlotResultEnum_External const& val);
DeleteSlotResultEnum_External to_external_api(DeleteSlotResultEnum_Internal const& val);

// using ConfigurationParameterUpdateResultEnum_Internal = ::types::config_service::ConfigurationParameterUpdateResultEnum;
// using ConfigurationParameterUpdateResultEnum_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateResultEnum;

// ConfigurationParameterUpdateResultEnum_Internal
// to_internal_api(ConfigurationParameterUpdateResultEnum_External const& val);
// ConfigurationParameterUpdateResultEnum_External
// to_external_api(ConfigurationParameterUpdateResultEnum_Internal const& val);

using ActiveSlotStatusEnum_Internal = ::Everest::config::ActiveSlotStatus;
using ActiveSlotStatusEnum_External = ::everest::lib::API::V1_0::types::config_service::ActiveSlotStatusEnum;

ActiveSlotStatusEnum_Internal to_internal_api(ActiveSlotStatusEnum_External const& val);
ActiveSlotStatusEnum_External to_external_api(ActiveSlotStatusEnum_Internal const& val);

// using ExecutionStatusEnum_Internal = ::types::config_service::ExecutionStatusEnum;
// using ExecutionStatusEnum_External = ::everest::lib::API::V1_0::types::config_service::ExecutionStatusEnum;

// ExecutionStatusEnum_Internal to_internal_api(ExecutionStatusEnum_External const& val);
// ExecutionStatusEnum_External to_external_api(ExecutionStatusEnum_Internal const& val);

using ConfigurationParameterDatatype_Internal = ::everest::config::Datatype;
using ConfigurationParameterDatatype_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterDatatype;

ConfigurationParameterDatatype_Internal to_internal_api(ConfigurationParameterDatatype_External const& val);
ConfigurationParameterDatatype_External to_external_api(ConfigurationParameterDatatype_Internal const& val);

using ConfigurationParameterMutability_Internal = ::everest::config::Mutability;
using ConfigurationParameterMutability_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterMutability;

ConfigurationParameterMutability_Internal to_internal_api(ConfigurationParameterMutability_External const& val);
ConfigurationParameterMutability_External to_external_api(ConfigurationParameterMutability_Internal const& val);

// using ConfigurationActivationPolicy_Internal = ::types::config_service::ConfigurationActivationPolicy;
// using ConfigurationActivationPolicy_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationActivationPolicy;

// ConfigurationActivationPolicy_Internal to_internal_api(ConfigurationActivationPolicy_External const& val);
// ConfigurationActivationPolicy_External to_external_api(ConfigurationActivationPolicy_Internal const& val);

using GetConfigurationStatusEnum_Internal = ::Everest::config::GetConfigurationStatus;
using GetConfigurationStatusEnum_External = ::everest::lib::API::V1_0::types::config_service::GetConfigurationStatusEnum;

GetConfigurationStatusEnum_Internal to_internal_api(GetConfigurationStatusEnum_External const& val);
GetConfigurationStatusEnum_External to_external_api(GetConfigurationStatusEnum_Internal const& val);

// using ApplyMode_Internal = ::types::config_service::ApplyMode;
// using ApplyMode_External = ::everest::lib::API::V1_0::types::config_service::ApplyMode;

// ApplyMode_Internal to_internal_api(ApplyMode_External const& val);
// ApplyMode_External to_external_api(ApplyMode_Internal const& val);

using ConfigMetadata_Internal = ::everest::config::SlotInfo;
using ConfigMetadata_External = ::everest::lib::API::V1_0::types::config_service::ConfigMetadata;

ConfigMetadata_Internal to_internal_api(ConfigMetadata_External const& val);
ConfigMetadata_External to_external_api(ConfigMetadata_Internal const& val);

using ListSlotIdsResult_Internal = ::std::vector<::everest::config::SlotInfo>;
using ListSlotIdsResult_External = ::everest::lib::API::V1_0::types::config_service::ListSlotIdsResult;

ListSlotIdsResult_Internal to_internal_api(ListSlotIdsResult_External const& val);
ListSlotIdsResult_External to_external_api(ListSlotIdsResult_Internal const& val);

// using StopModulesResult_Internal = ::types::config_service::StopModulesResult;
// using StopModulesResult_External = ::everest::lib::API::V1_0::types::config_service::StopModulesResult;

// StopModulesResult_Internal to_internal_api(StopModulesResult_External const& val);
// StopModulesResult_External to_external_api(StopModulesResult_Internal const& val);

// using StartModulesResult_Internal = ::types::config_service::StartModulesResult;
// using StartModulesResult_External = ::everest::lib::API::V1_0::types::config_service::StartModulesResult;

// StartModulesResult_Internal to_internal_api(StartModulesResult_External const& val);
// StartModulesResult_External to_external_api(StartModulesResult_Internal const& val);

using DuplicateSlotResult_Internal = ::everest::config::DuplicateSlotResult;
using DuplicateSlotResult_External = ::everest::lib::API::V1_0::types::config_service::DuplicateSlotResult;

DuplicateSlotResult_Internal to_internal_api(DuplicateSlotResult_External const& val);
DuplicateSlotResult_External to_external_api(DuplicateSlotResult_Internal const& val);

using LoadFromYamlResult_Internal = ::Everest::config::LoadFromYamlResult;
using LoadFromYamlResult_External = ::everest::lib::API::V1_0::types::config_service::LoadFromYamlResult;

LoadFromYamlResult_Internal to_internal_api(LoadFromYamlResult_External const& val);
LoadFromYamlResult_External to_external_api(LoadFromYamlResult_Internal const& val);

using ConfigurationParameterIdentifier_Internal = ::everest::config::ConfigurationParameterIdentifier;
using ConfigurationParameterIdentifier_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterIdentifier;

ConfigurationParameterIdentifier_Internal to_internal_api(ConfigurationParameterIdentifier_External const& val);
ConfigurationParameterIdentifier_External to_external_api(ConfigurationParameterIdentifier_Internal const& val);

// using ConfigurationParameterUpdate_Internal = ::types::config_service::ConfigurationParameterUpdate;
// using ConfigurationParameterUpdate_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdate;

// ConfigurationParameterUpdate_Internal to_internal_api(ConfigurationParameterUpdate_External const& val);
// ConfigurationParameterUpdate_External to_external_api(ConfigurationParameterUpdate_Internal const& val);

// using ConfigurationParameterUpdateRequest_Internal = ::types::config_service::ConfigurationParameterUpdateRequest;
// using ConfigurationParameterUpdateRequest_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateRequest;

// ConfigurationParameterUpdateRequest_Internal to_internal_api(ConfigurationParameterUpdateRequest_External const& val);
// ConfigurationParameterUpdateRequest_External to_external_api(ConfigurationParameterUpdateRequest_Internal const& val);

// using ConfigurationParameterUpdateRequestResult_Internal =
//     ::types::config_service::ConfigurationParameterUpdateRequestResult;
// using ConfigurationParameterUpdateRequestResult_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateRequestResult;

// ConfigurationParameterUpdateRequestResult_Internal
// to_internal_api(ConfigurationParameterUpdateRequestResult_External const& val);
// ConfigurationParameterUpdateRequestResult_External
// to_external_api(ConfigurationParameterUpdateRequestResult_Internal const& val);

// using OriginOfChange_Internal = ::types::config_service::OriginOfChange;
// using OriginOfChange_External = ::everest::lib::API::V1_0::types::config_service::OriginOfChange;

// OriginOfChange_Internal to_internal_api(OriginOfChange_External const& val);
// OriginOfChange_External to_external_api(OriginOfChange_Internal const& val);

using ActiveSlotUpdateNotice_Internal = ::Everest::config::ActiveSlotUpdate;
using ActiveSlotUpdateNotice_External = ::everest::lib::API::V1_0::types::config_service::ActiveSlotUpdateNotice;

ActiveSlotUpdateNotice_Internal to_internal_api(ActiveSlotUpdateNotice_External const& val);
ActiveSlotUpdateNotice_External to_external_api(ActiveSlotUpdateNotice_Internal const& val);

// using ConfigurationParameterUpdateResultRecord_Internal =
//     ::types::config_service::ConfigurationParameterUpdateResultRecord;
// using ConfigurationParameterUpdateResultRecord_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateResultRecord;

// ConfigurationParameterUpdateResultRecord_Internal
// to_internal_api(ConfigurationParameterUpdateResultRecord_External const& val);
// ConfigurationParameterUpdateResultRecord_External
// to_external_api(ConfigurationParameterUpdateResultRecord_Internal const& val);

// using ConfigurationParameterUpdateResult_Internal = ::types::config_service::ConfigurationParameterUpdateResult;
// using ConfigurationParameterUpdateResult_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateResult;

// ConfigurationParameterUpdateResult_Internal to_internal_api(ConfigurationParameterUpdateResult_External const& val);
// ConfigurationParameterUpdateResult_External to_external_api(ConfigurationParameterUpdateResult_Internal const& val);

// using ConfigurationParameterUpdateNotice_Internal = ::types::config_service::ConfigurationParameterUpdateNotice;
// using ConfigurationParameterUpdateNotice_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterUpdateNotice;

// ConfigurationParameterUpdateNotice_Internal to_internal_api(ConfigurationParameterUpdateNotice_External const& val);
// ConfigurationParameterUpdateNotice_External to_external_api(ConfigurationParameterUpdateNotice_Internal const& val);

// using ExecutionStatusUpdateNotice_Internal = ::types::config_service::ExecutionStatusUpdateNotice;
// using ExecutionStatusUpdateNotice_External = ::everest::lib::API::V1_0::types::config_service::ExecutionStatusUpdateNotice;

// ExecutionStatusUpdateNotice_Internal to_internal_api(ExecutionStatusUpdateNotice_External const& val);
// ExecutionStatusUpdateNotice_External to_external_api(ExecutionStatusUpdateNotice_Internal const& val);

using Mapping_Internal = Mapping;
using Mapping_External = ::everest::lib::API::V1_0::types::config_service::Mapping;

Mapping_Internal to_internal_api(Mapping_External const& val);
Mapping_External to_external_api(Mapping_Internal const& val);

// using ImplMapping_Internal = ::types::config_service::ImplMapping;
// using ImplMapping_External = ::everest::lib::API::V1_0::types::config_service::ImplMapping;

// ImplMapping_Internal to_internal_api(ImplMapping_External const& val);
// ImplMapping_External to_external_api(ImplMapping_Internal const& val);

using ModuleTierMappings_Internal = ModuleTierMappings;
using ModuleTierMappings_External = ::everest::lib::API::V1_0::types::config_service::ModuleTierMappings;

ModuleTierMappings_Internal to_internal_api(ModuleTierMappings_External const& val);
ModuleTierMappings_External to_external_api(ModuleTierMappings_Internal const& val);

using ReqFulfillment_Internal = Fulfillment;
using ReqFulfillment_External = ::everest::lib::API::V1_0::types::config_service::ReqFulfillment;

ReqFulfillment_Internal to_internal_api(ReqFulfillment_External const& val, const std::string& requirement_id);
ReqFulfillment_External to_external_api(ReqFulfillment_Internal const& val);

// using ModuleConnection_Internal = ::everest::config:ModuleConnection;
// using ModuleConnection_External = ::everest::lib::API::V1_0::types::config_service::ModuleConnection;

// ModuleConnection_Internal to_internal_api(ModuleConnection_External const& val);
// ModuleConnection_External to_external_api(ModuleConnection_Internal const& val);

using ConfigurationParameterCharacteristics_Internal = ::everest::config::ConfigurationParameterCharacteristics;
using ConfigurationParameterCharacteristics_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameterCharacteristics;

ConfigurationParameterCharacteristics_Internal
to_internal_api(ConfigurationParameterCharacteristics_External const& val);
ConfigurationParameterCharacteristics_External
to_external_api(ConfigurationParameterCharacteristics_Internal const& val);

using ConfigurationParameter_Internal = ::everest::config::ConfigurationParameter;
using ConfigurationParameter_External = ::everest::lib::API::V1_0::types::config_service::ConfigurationParameter;

ConfigurationParameter_Internal to_internal_api(ConfigurationParameter_External const& val);
ConfigurationParameter_External to_external_api(ConfigurationParameter_Internal const& val);

// using ImplementationConfigurationParameter_Internal = ::types::config_service::ImplementationConfigurationParameter;
// using ImplementationConfigurationParameter_External = ::everest::lib::API::V1_0::types::config_service::ImplementationConfigurationParameter;

// ImplementationConfigurationParameter_Internal to_internal_api(ImplementationConfigurationParameter_External const& val);
// ImplementationConfigurationParameter_External to_external_api(ImplementationConfigurationParameter_Internal const& val);

std::vector<Fulfillment> fulfillmentVecToInternal(const std::vector<ReqFulfillment_External>& fulfillments_external, const std::string& requirement_id);

using ModuleConfiguration_Internal = ::everest::config::ModuleConfig;
using ModuleConfiguration_External = ::everest::lib::API::V1_0::types::config_service::ModuleConfiguration;

ModuleConfiguration_Internal to_internal_api(ModuleConfiguration_External const& val);
ModuleConfiguration_External to_external_api(ModuleConfiguration_Internal const& val);

using GetConfigurationResult_Internal = ::Everest::config::GetConfigurationResult;
using GetConfigurationResult_External = ::everest::lib::API::V1_0::types::config_service::GetConfigurationResult;

GetConfigurationResult_Internal to_internal_api(GetConfigurationResult_External const& val);
GetConfigurationResult_External to_external_api(GetConfigurationResult_Internal const& val);

} // namespace everest::config::api::types
