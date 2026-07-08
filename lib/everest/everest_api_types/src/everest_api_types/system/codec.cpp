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

} // namespace everest::lib::API::V1_0::types::system
