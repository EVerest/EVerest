// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include "nlohmann/json_fwd.hpp"
#include <everest_api_types/system/API.hpp>

namespace everest::lib::API::V1_0::types::system {

using json = nlohmann::json;

void to_json(json& j, const FirmwareUpdateRequest& k) noexcept;
void from_json(const json& j, FirmwareUpdateRequest& k);

void to_json(json& j, const UploadLogsRequest& k) noexcept;
void from_json(const json& j, UploadLogsRequest& k);

void to_json(json& j, const UploadLogsResponse& k) noexcept;
void from_json(const json& j, UploadLogsResponse& k);

void to_json(json& j, const LogStatus& k) noexcept;
void from_json(const json& j, LogStatus& k);

void to_json(json& j, const FirmwareUpdateMetadata& k) noexcept;
void from_json(const json& j, FirmwareUpdateMetadata& k);

void to_json(json& j, const FirmwareUpdateStatus& k) noexcept;
void from_json(const json& j, FirmwareUpdateStatus& k);

void to_json(json& j, const ResetRequest& k) noexcept;
void from_json(const json& j, ResetRequest& k);

void to_json(json& j, const UpdateFirmwareResponse& k) noexcept;
void from_json(const json& j, UpdateFirmwareResponse& k);

void to_json(json& j, const UploadLogsStatus& k) noexcept;
void from_json(const json& j, UploadLogsStatus& k);

void to_json(json& j, const LogStatusEnum& k) noexcept;
void from_json(const json& j, LogStatusEnum& k);

void to_json(json& j, const FirmwareUpdateStatusEnum& k) noexcept;
void from_json(const json& j, FirmwareUpdateStatusEnum& k);

void to_json(json& j, const ResetType& k) noexcept;
void from_json(const json& j, ResetType& k);

void to_json(json& j, const BootReason& k) noexcept;
void from_json(const json& j, BootReason& k);

void to_json(json& j, const InterfaceClass& k) noexcept;
void from_json(const json& j, InterfaceClass& k);

void to_json(json& j, const APNAuthenticationEnum& k) noexcept;
void from_json(const json& j, APNAuthenticationEnum& k);

void to_json(json& j, const VPNTypeEnum& k) noexcept;
void from_json(const json& j, VPNTypeEnum& k);

void to_json(json& j, const ConfigureNetworkStatusEnum& k) noexcept;
void from_json(const json& j, ConfigureNetworkStatusEnum& k);

void to_json(json& j, const ConfigureNetworkFinalStatusEnum& k) noexcept;
void from_json(const json& j, ConfigureNetworkFinalStatusEnum& k);

void to_json(json& j, const APN& k) noexcept;
void from_json(const json& j, APN& k);

void to_json(json& j, const VPN& k) noexcept;
void from_json(const json& j, VPN& k);

void to_json(json& j, const ConfigureNetworkRequest& k) noexcept;
void from_json(const json& j, ConfigureNetworkRequest& k);

void to_json(json& j, const ConfigureNetworkResponse& k) noexcept;
void from_json(const json& j, ConfigureNetworkResponse& k);

void to_json(json& j, const ConfigureNetworkStatus& k) noexcept;
void from_json(const json& j, ConfigureNetworkStatus& k);

} // namespace everest::lib::API::V1_0::types::system
