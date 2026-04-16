// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "configuration/codec.hpp"
#include "configuration/API.hpp"
#include "configuration/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include <string>

namespace everest::lib::API::V1_0::types::configuration {

std::string serialize(MarkActiveSlotResultEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DeleteSlotResultEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdateResultEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ActiveSlotStatusEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterDatatype val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterMutability val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetConfigurationStatusEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterGetRequestEnum val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigMetadata const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ListSlotIdsResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetActiveSlotIdResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(MarkActiveSlotResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DeleteSlotResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DuplicateSlotResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(LoadFromYamlResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SetDescriptionRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(SetDescriptionResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterIdentifier const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdate const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdateRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdateRequestResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(OriginOfChange const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetConfigurationParameterRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterGetValueResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetConfigurationParameterResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ActiveSlotUpdateNotice const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdateResultRecord const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterUpdateNotice const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(Mapping const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ImplMapping const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ModuleTierMappings const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ReqFulfillment const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ModuleConnection const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameterCharacteristics const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigurationParameter const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ImplementationConfigurationParameter const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(TelemetryConfig const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ModuleConfigAccess const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigAccess const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ConfigAccessControl const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(ModuleConfiguration const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetConfigurationResult const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(MarkActiveSlotRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DeleteSlotRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(DuplicateSlotRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(LoadFromYamlRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::string serialize(GetConfigurationRequest const& val) noexcept {
    return nlohmann::json(val).dump(json_indent);
}

std::ostream& operator<<(std::ostream& os, MarkActiveSlotResultEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DeleteSlotResultEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdateResultEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ActiveSlotStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterDatatype const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterMutability const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetConfigurationStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterGetRequestEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigMetadata const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ListSlotIdsResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetActiveSlotIdResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, MarkActiveSlotResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DeleteSlotResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DuplicateSlotResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LoadFromYamlResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetDescriptionRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, SetDescriptionResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterIdentifier const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdate const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdateRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdateRequestResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, OriginOfChange const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetConfigurationParameterRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterGetValueResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetConfigurationParameterResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ActiveSlotUpdateNotice const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdateResultRecord const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterUpdateNotice const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, Mapping const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ImplMapping const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleTierMappings const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ReqFulfillment const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleConnection const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameterCharacteristics const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigurationParameter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ImplementationConfigurationParameter const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, TelemetryConfig const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleConfigAccess const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigAccess const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigAccessControl const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ModuleConfiguration const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetConfigurationResult const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, MarkActiveSlotRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DeleteSlotRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, DuplicateSlotRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LoadFromYamlRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, GetConfigurationRequest const& val) {
    os << serialize(val);
    return os;
}

template <> MarkActiveSlotResultEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> DeleteSlotResultEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdateResultEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ActiveSlotStatusEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterDatatype deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterMutability deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetConfigurationStatusEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterGetRequestEnum deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigMetadata deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ListSlotIdsResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetActiveSlotIdResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> MarkActiveSlotResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> DeleteSlotResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> DuplicateSlotResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> LoadFromYamlResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> SetDescriptionRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> SetDescriptionResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterIdentifier deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdate deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdateRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdateRequestResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> OriginOfChange deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetConfigurationParameterRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterGetValueResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetConfigurationParameterResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ActiveSlotUpdateNotice deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdateResultRecord deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterUpdateNotice deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> Mapping deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ImplMapping deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ModuleTierMappings deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ReqFulfillment deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ModuleConnection deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameterCharacteristics deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigurationParameter deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ImplementationConfigurationParameter deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> TelemetryConfig deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ModuleConfigAccess deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigAccess deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ConfigAccessControl deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> ModuleConfiguration deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetConfigurationResult deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> MarkActiveSlotRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> DeleteSlotRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> DuplicateSlotRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> LoadFromYamlRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

template <> GetConfigurationRequest deserialize<>(std::string const& val) {
    return json::parse(val);
}

} // namespace everest::lib::API::V1_0::types::configuration
