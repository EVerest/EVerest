// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "configuration/codec.hpp"
#include "configuration/API.hpp"
#include "configuration/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::configuration {

std::string serialize(MarkActiveSlotResultEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DeleteSlotResultEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdateResultEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ActiveSlotStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterDatatype val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterMutability val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetConfigurationStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterGetRequestEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigMetadata const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ListSlotIdsResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetActiveSlotIdResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(MarkActiveSlotResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DeleteSlotResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DuplicateSlotResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LoadFromYamlResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SetDescriptionRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(SetDescriptionResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterIdentifier const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdate const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdateRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdateRequestResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(OriginOfChange const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetConfigurationParameterRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterGetValueResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetConfigurationParameterResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ActiveSlotUpdateNotice const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdateResultRecord const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterUpdateNotice const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(Mapping const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ImplMapping const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModuleTierMappings const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ReqFulfillment const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModuleConnection const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameterCharacteristics const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigurationParameter const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ImplementationConfigurationParameter const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(TelemetryConfig const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModuleConfigAccess const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigAccess const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigAccessControl const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ModuleConfiguration const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetConfigurationResult const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(MarkActiveSlotRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DeleteSlotRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(DuplicateSlotRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LoadFromYamlRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(GetConfigurationRequest const& val) noexcept {
    return utilities::dump_json(val);
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

template <> MarkActiveSlotResultEnum deserialize(std::string_view val) {
    return utilities::parse_json<MarkActiveSlotResultEnum>(val);
}

template <> DeleteSlotResultEnum deserialize(std::string_view val) {
    return utilities::parse_json<DeleteSlotResultEnum>(val);
}

template <> ConfigurationParameterUpdateResultEnum deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdateResultEnum>(val);
}

template <> ActiveSlotStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<ActiveSlotStatusEnum>(val);
}

template <> ConfigurationParameterDatatype deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterDatatype>(val);
}

template <> ConfigurationParameterMutability deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterMutability>(val);
}

template <> GetConfigurationStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<GetConfigurationStatusEnum>(val);
}

template <> ConfigurationParameterGetRequestEnum deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterGetRequestEnum>(val);
}

template <> ConfigMetadata deserialize(std::string_view val) {
    return utilities::parse_json<ConfigMetadata>(val);
}

template <> ListSlotIdsResult deserialize(std::string_view val) {
    return utilities::parse_json<ListSlotIdsResult>(val);
}

template <> GetActiveSlotIdResult deserialize(std::string_view val) {
    return utilities::parse_json<GetActiveSlotIdResult>(val);
}

template <> MarkActiveSlotResult deserialize(std::string_view val) {
    return utilities::parse_json<MarkActiveSlotResult>(val);
}

template <> DeleteSlotResult deserialize(std::string_view val) {
    return utilities::parse_json<DeleteSlotResult>(val);
}

template <> DuplicateSlotResult deserialize(std::string_view val) {
    return utilities::parse_json<DuplicateSlotResult>(val);
}

template <> LoadFromYamlResult deserialize(std::string_view val) {
    return utilities::parse_json<LoadFromYamlResult>(val);
}

template <> SetDescriptionRequest deserialize(std::string_view val) {
    return utilities::parse_json<SetDescriptionRequest>(val);
}

template <> SetDescriptionResult deserialize(std::string_view val) {
    return utilities::parse_json<SetDescriptionResult>(val);
}

template <> ConfigurationParameterIdentifier deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterIdentifier>(val);
}

template <> ConfigurationParameterUpdate deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdate>(val);
}

template <> ConfigurationParameterUpdateRequest deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdateRequest>(val);
}

template <> ConfigurationParameterUpdateRequestResult deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdateRequestResult>(val);
}

template <> OriginOfChange deserialize(std::string_view val) {
    return utilities::parse_json<OriginOfChange>(val);
}

template <> GetConfigurationParameterRequest deserialize(std::string_view val) {
    return utilities::parse_json<GetConfigurationParameterRequest>(val);
}

template <> ConfigurationParameterGetValueResult deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterGetValueResult>(val);
}

template <> GetConfigurationParameterResult deserialize(std::string_view val) {
    return utilities::parse_json<GetConfigurationParameterResult>(val);
}

template <> ActiveSlotUpdateNotice deserialize(std::string_view val) {
    return utilities::parse_json<ActiveSlotUpdateNotice>(val);
}

template <> ConfigurationParameterUpdateResultRecord deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdateResultRecord>(val);
}

template <> ConfigurationParameterUpdateNotice deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterUpdateNotice>(val);
}

template <> Mapping deserialize(std::string_view val) {
    return utilities::parse_json<Mapping>(val);
}

template <> ImplMapping deserialize(std::string_view val) {
    return utilities::parse_json<ImplMapping>(val);
}

template <> ModuleTierMappings deserialize(std::string_view val) {
    return utilities::parse_json<ModuleTierMappings>(val);
}

template <> ReqFulfillment deserialize(std::string_view val) {
    return utilities::parse_json<ReqFulfillment>(val);
}

template <> ModuleConnection deserialize(std::string_view val) {
    return utilities::parse_json<ModuleConnection>(val);
}

template <> ConfigurationParameterCharacteristics deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameterCharacteristics>(val);
}

template <> ConfigurationParameter deserialize(std::string_view val) {
    return utilities::parse_json<ConfigurationParameter>(val);
}

template <> ImplementationConfigurationParameter deserialize(std::string_view val) {
    return utilities::parse_json<ImplementationConfigurationParameter>(val);
}

template <> TelemetryConfig deserialize(std::string_view val) {
    return utilities::parse_json<TelemetryConfig>(val);
}

template <> ModuleConfigAccess deserialize(std::string_view val) {
    return utilities::parse_json<ModuleConfigAccess>(val);
}

template <> ConfigAccess deserialize(std::string_view val) {
    return utilities::parse_json<ConfigAccess>(val);
}

template <> ConfigAccessControl deserialize(std::string_view val) {
    return utilities::parse_json<ConfigAccessControl>(val);
}

template <> ModuleConfiguration deserialize(std::string_view val) {
    return utilities::parse_json<ModuleConfiguration>(val);
}

template <> GetConfigurationResult deserialize(std::string_view val) {
    return utilities::parse_json<GetConfigurationResult>(val);
}

template <> MarkActiveSlotRequest deserialize(std::string_view val) {
    return utilities::parse_json<MarkActiveSlotRequest>(val);
}

template <> DeleteSlotRequest deserialize(std::string_view val) {
    return utilities::parse_json<DeleteSlotRequest>(val);
}

template <> DuplicateSlotRequest deserialize(std::string_view val) {
    return utilities::parse_json<DuplicateSlotRequest>(val);
}

template <> LoadFromYamlRequest deserialize(std::string_view val) {
    return utilities::parse_json<LoadFromYamlRequest>(val);
}

template <> GetConfigurationRequest deserialize(std::string_view val) {
    return utilities::parse_json<GetConfigurationRequest>(val);
}

} // namespace everest::lib::API::V1_0::types::configuration
