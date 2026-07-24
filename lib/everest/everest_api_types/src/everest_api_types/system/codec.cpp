// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "system/codec.hpp"
#include "nlohmann/json.hpp"
#include "system/API.hpp"
#include "system/json_codec.hpp"
#include "utilities/constants.hpp"
#include "utilities/json_codec_helpers.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace everest::lib::API::V1_0::types::system {

std::string serialize(UpdateFirmwareResponse val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(UploadLogsStatus val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LogStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FirmwareUpdateStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ResetType val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(BootReason val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FirmwareUpdateRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(UploadLogsRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(UploadLogsResponse const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(LogStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FirmwareUpdateMetadata const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(FirmwareUpdateStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ResetRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(InterfaceClass val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(APNAuthenticationEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(VPNTypeEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigureNetworkStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigureNetworkFinalStatusEnum val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(APN const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(VPN const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigureNetworkRequest const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigureNetworkResponse const& val) noexcept {
    return utilities::dump_json(val);
}

std::string serialize(ConfigureNetworkStatus const& val) noexcept {
    return utilities::dump_json(val);
}

std::ostream& operator<<(std::ostream& os, UpdateFirmwareResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, UploadLogsStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LogStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FirmwareUpdateStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ResetType const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, BootReason const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FirmwareUpdateRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, UploadLogsRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, LogStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FirmwareUpdateMetadata const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, FirmwareUpdateStatus const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, UploadLogsResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ResetRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, InterfaceClass const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, APNAuthenticationEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, VPNTypeEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigureNetworkStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigureNetworkFinalStatusEnum const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, APN const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, VPN const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigureNetworkRequest const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigureNetworkResponse const& val) {
    os << serialize(val);
    return os;
}

std::ostream& operator<<(std::ostream& os, ConfigureNetworkStatus const& val) {
    os << serialize(val);
    return os;
}

template <> UpdateFirmwareResponse deserialize(std::string_view val) {
    return utilities::parse_json<UpdateFirmwareResponse>(val);
}

template <> UploadLogsStatus deserialize(std::string_view val) {
    return utilities::parse_json<UploadLogsStatus>(val);
}

template <> LogStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<LogStatusEnum>(val);
}

template <> FirmwareUpdateStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<FirmwareUpdateStatusEnum>(val);
}

template <> ResetType deserialize(std::string_view val) {
    return utilities::parse_json<ResetType>(val);
}

template <> BootReason deserialize(std::string_view val) {
    return utilities::parse_json<BootReason>(val);
}

template <> FirmwareUpdateRequest deserialize(std::string_view val) {
    return utilities::parse_json<FirmwareUpdateRequest>(val);
}

template <> UploadLogsRequest deserialize(std::string_view val) {
    return utilities::parse_json<UploadLogsRequest>(val);
}

template <> UploadLogsResponse deserialize(std::string_view val) {
    return utilities::parse_json<UploadLogsResponse>(val);
}

template <> LogStatus deserialize(std::string_view val) {
    return utilities::parse_json<LogStatus>(val);
}

template <> FirmwareUpdateMetadata deserialize(std::string_view val) {
    return utilities::parse_json<FirmwareUpdateMetadata>(val);
}

template <> FirmwareUpdateStatus deserialize(std::string_view val) {
    return utilities::parse_json<FirmwareUpdateStatus>(val);
}

template <> ResetRequest deserialize(std::string_view val) {
    return utilities::parse_json<ResetRequest>(val);
}

template <> InterfaceClass deserialize(std::string_view val) {
    return utilities::parse_json<InterfaceClass>(val);
}

template <> APNAuthenticationEnum deserialize(std::string_view val) {
    return utilities::parse_json<APNAuthenticationEnum>(val);
}

template <> VPNTypeEnum deserialize(std::string_view val) {
    return utilities::parse_json<VPNTypeEnum>(val);
}

template <> ConfigureNetworkStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<ConfigureNetworkStatusEnum>(val);
}

template <> ConfigureNetworkFinalStatusEnum deserialize(std::string_view val) {
    return utilities::parse_json<ConfigureNetworkFinalStatusEnum>(val);
}

template <> APN deserialize(std::string_view val) {
    return utilities::parse_json<APN>(val);
}

template <> VPN deserialize(std::string_view val) {
    return utilities::parse_json<VPN>(val);
}

template <> ConfigureNetworkRequest deserialize(std::string_view val) {
    return utilities::parse_json<ConfigureNetworkRequest>(val);
}

template <> ConfigureNetworkResponse deserialize(std::string_view val) {
    return utilities::parse_json<ConfigureNetworkResponse>(val);
}

template <> ConfigureNetworkStatus deserialize(std::string_view val) {
    return utilities::parse_json<ConfigureNetworkStatus>(val);
}

} // namespace everest::lib::API::V1_0::types::system
