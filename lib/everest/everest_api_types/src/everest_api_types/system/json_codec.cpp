// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "system/json_codec.hpp"
#include "nlohmann/json.hpp"
#include "system/API.hpp"
#include "system/codec.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::system {

void to_json(json& j, const FirmwareUpdateRequest& k) noexcept {
    j = json{
        {"request_id", k.request_id},
        {"location", k.location},
    };
    if (k.retries) {
        j["retries"] = k.retries.value();
    }
    if (k.retry_interval_s) {
        j["retry_interval_s"] = k.retry_interval_s.value();
    }
    if (k.retrieve_timestamp) {
        j["retrieve_timestamp"] = k.retrieve_timestamp.value();
    }
    if (k.install_timestamp) {
        j["install_timestamp"] = k.install_timestamp.value();
    }
    if (k.signing_certificate) {
        j["signing_certificate"] = k.signing_certificate.value();
    }
    if (k.signature) {
        j["signature"] = k.signature.value();
    }
}
void from_json(const json& j, FirmwareUpdateRequest& k) {
    k.request_id = j.at("request_id");
    k.location = j.at("location");

    if (j.contains("retries")) {
        k.retries.emplace(j.at("retries"));
    }
    if (j.contains("retry_interval_s")) {
        k.retry_interval_s.emplace(j.at("retry_interval_s"));
    }
    if (j.contains("retrieve_timestamp")) {
        k.retrieve_timestamp.emplace(j.at("retrieve_timestamp"));
    }
    if (j.contains("install_timestamp")) {
        k.install_timestamp.emplace(j.at("install_timestamp"));
    }
    if (j.contains("signing_certificate")) {
        k.signing_certificate.emplace(j.at("signing_certificate"));
    }
    if (j.contains("signature")) {
        k.signature.emplace(j.at("signature"));
    }
}

void to_json(json& j, const UploadLogsRequest& k) noexcept {
    j = json{
        {"location", k.location},
    };
    if (k.retries) {
        j["retries"] = k.retries.value();
    }
    if (k.retry_interval_s) {
        j["retry_interval_s"] = k.retry_interval_s.value();
    }
    if (k.oldest_timestamp) {
        j["oldest_timestamp"] = k.oldest_timestamp.value();
    }
    if (k.latest_timestamp) {
        j["latest_timestamp"] = k.latest_timestamp.value();
    }
    if (k.type) {
        j["type"] = k.type.value();
    }
    if (k.request_id) {
        j["request_id"] = k.request_id.value();
    }
}
void from_json(const json& j, UploadLogsRequest& k) {
    k.location = j.at("location");

    if (j.contains("retries")) {
        k.retries.emplace(j.at("retries"));
    }
    if (j.contains("retry_interval_s")) {
        k.retry_interval_s.emplace(j.at("retry_interval_s"));
    }
    if (j.contains("oldest_timestamp")) {
        k.oldest_timestamp.emplace(j.at("oldest_timestamp"));
    }
    if (j.contains("latest_timestamp")) {
        k.latest_timestamp.emplace(j.at("latest_timestamp"));
    }
    if (j.contains("type")) {
        k.type.emplace(j.at("type"));
    }
    if (j.contains("request_id")) {
        k.request_id.emplace(j.at("request_id"));
    }
}

void to_json(json& j, const UploadLogsResponse& k) noexcept {
    j = json{
        {"upload_logs_status", k.upload_logs_status},
    };
    if (k.file_name) {
        j["file_name"] = k.file_name.value();
    }
}
void from_json(const json& j, UploadLogsResponse& k) {
    k.upload_logs_status = j.at("upload_logs_status");

    if (j.contains("file_name")) {
        k.file_name.emplace(j.at("file_name"));
    }
}

void to_json(json& j, const LogStatus& k) noexcept {
    j = json{
        {"log_status", k.log_status},
        {"request_id", k.request_id},
    };
}
void from_json(const json& j, LogStatus& k) {
    k.log_status = j.at("log_status");
    k.request_id = j.at("request_id");
}

void to_json(json& j, const FirmwareUpdateMetadata& k) noexcept {
    j = json::object();
    if (k.disable_connectors_during_install.has_value()) {
        j["disable_connectors_during_install"] = k.disable_connectors_during_install.value();
    }
}

void from_json(const json& j, FirmwareUpdateMetadata& k) {
    if (j.contains("disable_connectors_during_install")) {
        k.disable_connectors_during_install = j.at("disable_connectors_during_install");
    }
}

void to_json(json& j, const FirmwareUpdateStatus& k) noexcept {
    j = json{
        {"firmware_update_status", k.firmware_update_status},
        {"request_id", k.request_id},
    };
    if (k.firmware_update_metadata.has_value()) {
        j["firmware_update_metadata"] = k.firmware_update_metadata.value();
    }
}
void from_json(const json& j, FirmwareUpdateStatus& k) {
    k.firmware_update_status = j.at("firmware_update_status");
    k.request_id = j.at("request_id");
    if (j.contains("firmware_update_metadata")) {
        k.firmware_update_metadata = j.at("firmware_update_metadata");
    }
}

void to_json(json& j, const ResetRequest& k) noexcept {
    j = json{
        {"type", k.type},
        {"scheduled", k.scheduled},
    };
}
void from_json(const json& j, ResetRequest& k) {
    k.type = j.at("type");
    k.scheduled = j.at("scheduled");
}

void to_json(json& j, const UpdateFirmwareResponse& k) noexcept {
    switch (k) {
    case UpdateFirmwareResponse::Accepted:
        j = "Accepted";
        return;
    case UpdateFirmwareResponse::Rejected:
        j = "Rejected";
        return;
    case UpdateFirmwareResponse::AcceptedCanceled:
        j = "AcceptedCanceled";
        return;
    case UpdateFirmwareResponse::InvalidCertificate:
        j = "InvalidCertificate";
        return;
    case UpdateFirmwareResponse::RevokedCertificate:
        j = "RevokedCertificate";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::UpdateFirmwareResponse";
}
void from_json(const json& j, UpdateFirmwareResponse& k) {
    std::string s = j;
    if (s == "Accepted") {
        k = UpdateFirmwareResponse::Accepted;
        return;
    }
    if (s == "Rejected") {
        k = UpdateFirmwareResponse::Rejected;
        return;
    }
    if (s == "AcceptedCanceled") {
        k = UpdateFirmwareResponse::AcceptedCanceled;
        return;
    }
    if (s == "InvalidCertificate") {
        k = UpdateFirmwareResponse::InvalidCertificate;
        return;
    }
    if (s == "RevokedCertificate") {
        k = UpdateFirmwareResponse::RevokedCertificate;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type UpdateFirmwareResponse");
}

void to_json(json& j, const UploadLogsStatus& k) noexcept {
    switch (k) {
    case UploadLogsStatus::Accepted:
        j = "Accepted";
        return;
    case UploadLogsStatus::Rejected:
        j = "Rejected";
        return;
    case UploadLogsStatus::AcceptedCanceled:
        j = "AcceptedCanceled";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::UploadLogsStatus";
}
void from_json(const json& j, UploadLogsStatus& k) {
    std::string s = j;
    if (s == "Accepted") {
        k = UploadLogsStatus::Accepted;
        return;
    }
    if (s == "Rejected") {
        k = UploadLogsStatus::Rejected;
        return;
    }
    if (s == "AcceptedCanceled") {
        k = UploadLogsStatus::AcceptedCanceled;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type UploadLogsStatus");
}

void to_json(json& j, const LogStatusEnum& k) noexcept {
    switch (k) {
    case LogStatusEnum::BadMessage:
        j = "BadMessage";
        return;
    case LogStatusEnum::Idle:
        j = "Idle";
        return;
    case LogStatusEnum::NotSupportedOperation:
        j = "NotSupportedOperation";
        return;
    case LogStatusEnum::PermissionDenied:
        j = "PermissionDenied";
        return;
    case LogStatusEnum::Uploaded:
        j = "Uploaded";
        return;
    case LogStatusEnum::UploadFailure:
        j = "UploadFailure";
        return;
    case LogStatusEnum::Uploading:
        j = "Uploading";
        return;
    case LogStatusEnum::AcceptedCanceled:
        j = "AcceptedCanceled";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::LogStatusEnum";
}
void from_json(const json& j, LogStatusEnum& k) {
    std::string s = j;
    if (s == "BadMessage") {
        k = LogStatusEnum::BadMessage;
        return;
    }
    if (s == "Idle") {
        k = LogStatusEnum::Idle;
        return;
    }
    if (s == "NotSupportedOperation") {
        k = LogStatusEnum::NotSupportedOperation;
        return;
    }
    if (s == "PermissionDenied") {
        k = LogStatusEnum::PermissionDenied;
        return;
    }
    if (s == "Uploaded") {
        k = LogStatusEnum::Uploaded;
        return;
    }
    if (s == "UploadFailure") {
        k = LogStatusEnum::UploadFailure;
        return;
    }
    if (s == "Uploading") {
        k = LogStatusEnum::Uploading;
        return;
    }
    if (s == "AcceptedCanceled") {
        k = LogStatusEnum::AcceptedCanceled;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type LogStatusEnum");
}

void to_json(json& j, const FirmwareUpdateStatusEnum& k) noexcept {
    switch (k) {
    case FirmwareUpdateStatusEnum::Downloaded:
        j = "Downloaded";
        return;
    case FirmwareUpdateStatusEnum::DownloadFailed:
        j = "DownloadFailed";
        return;
    case FirmwareUpdateStatusEnum::Downloading:
        j = "Downloading";
        return;
    case FirmwareUpdateStatusEnum::DownloadScheduled:
        j = "DownloadScheduled";
        return;
    case FirmwareUpdateStatusEnum::DownloadPaused:
        j = "DownloadPaused";
        return;
    case FirmwareUpdateStatusEnum::Idle:
        j = "Idle";
        return;
    case FirmwareUpdateStatusEnum::InstallationFailed:
        j = "InstallationFailed";
        return;
    case FirmwareUpdateStatusEnum::Installing:
        j = "Installing";
        return;
    case FirmwareUpdateStatusEnum::Installed:
        j = "Installed";
        return;
    case FirmwareUpdateStatusEnum::InstallRebooting:
        j = "InstallRebooting";
        return;
    case FirmwareUpdateStatusEnum::InstallScheduled:
        j = "InstallScheduled";
        return;
    case FirmwareUpdateStatusEnum::InstallVerificationFailed:
        j = "InstallVerificationFailed";
        return;
    case FirmwareUpdateStatusEnum::InvalidSignature:
        j = "InvalidSignature";
        return;
    case FirmwareUpdateStatusEnum::SignatureVerified:
        j = "SignatureVerified";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::UpdateFirmwareResponFirmwareUpdateStatusEnum";
}
void from_json(const json& j, FirmwareUpdateStatusEnum& k) {
    std::string s = j;
    if (s == "Downloaded") {
        k = FirmwareUpdateStatusEnum::Downloaded;
        return;
    }
    if (s == "DownloadFailed") {
        k = FirmwareUpdateStatusEnum::DownloadFailed;
        return;
    }
    if (s == "Downloading") {
        k = FirmwareUpdateStatusEnum::Downloading;
        return;
    }
    if (s == "DownloadScheduled") {
        k = FirmwareUpdateStatusEnum::DownloadScheduled;
        return;
    }
    if (s == "DownloadPaused") {
        k = FirmwareUpdateStatusEnum::DownloadPaused;
        return;
    }
    if (s == "Idle") {
        k = FirmwareUpdateStatusEnum::Idle;
        return;
    }
    if (s == "InstallationFailed") {
        k = FirmwareUpdateStatusEnum::InstallationFailed;
        return;
    }
    if (s == "Installing") {
        k = FirmwareUpdateStatusEnum::Installing;
        return;
    }
    if (s == "Installed") {
        k = FirmwareUpdateStatusEnum::Installed;
        return;
    }
    if (s == "InstallRebooting") {
        k = FirmwareUpdateStatusEnum::InstallRebooting;
        return;
    }
    if (s == "InstallScheduled") {
        k = FirmwareUpdateStatusEnum::InstallScheduled;
        return;
    }
    if (s == "InstallVerificationFailed") {
        k = FirmwareUpdateStatusEnum::InstallVerificationFailed;
        return;
    }
    if (s == "InvalidSignature") {
        k = FirmwareUpdateStatusEnum::InvalidSignature;
        return;
    }
    if (s == "SignatureVerified") {
        k = FirmwareUpdateStatusEnum::SignatureVerified;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type FirmwareUpdateStatusEnum");
}

void to_json(json& j, const ResetType& k) noexcept {
    switch (k) {
    case ResetType::Soft:
        j = "Soft";
        return;
    case ResetType::Hard:
        j = "Hard";
        return;
    case ResetType::NotSpecified:
        j = "NotSpecified";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::UpdateFirmwareResponResetType";
}
void from_json(const json& j, ResetType& k) {
    std::string s = j;
    if (s == "Soft") {
        k = ResetType::Soft;
        return;
    }
    if (s == "Hard") {
        k = ResetType::Hard;
        return;
    }
    if (s == "NotSpecified") {
        k = ResetType::NotSpecified;
        return;
    }
    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type ResetType");
}

void to_json(json& j, const BootReason& k) noexcept {
    switch (k) {
    case BootReason::ApplicationReset:
        j = "ApplicationReset";
        return;
    case BootReason::FirmwareUpdate:
        j = "FirmwareUpdate";
        return;
    case BootReason::LocalReset:
        j = "LocalReset";
        return;
    case BootReason::PowerUp:
        j = "PowerUp";
        return;
    case BootReason::RemoteReset:
        j = "RemoteReset";
        return;
    case BootReason::ScheduledReset:
        j = "ScheduledReset";
        return;
    case BootReason::Triggered:
        j = "Triggered";
        return;
    case BootReason::Unknown:
        j = "Unknown";
        return;
    case BootReason::Watchdog:
        j = "Watchdog";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::BootReason";
}
void from_json(const json& j, BootReason& k) {
    std::string s = j;
    if (s == "ApplicationReset") {
        k = BootReason::ApplicationReset;
        return;
    }
    if (s == "FirmwareUpdate") {
        k = BootReason::FirmwareUpdate;
        return;
    }
    if (s == "LocalReset") {
        k = BootReason::LocalReset;
        return;
    }
    if (s == "PowerUp") {
        k = BootReason::PowerUp;
        return;
    }
    if (s == "RemoteReset") {
        k = BootReason::RemoteReset;
        return;
    }
    if (s == "ScheduledReset") {
        k = BootReason::ScheduledReset;
        return;
    }
    if (s == "Triggered") {
        k = BootReason::Triggered;
        return;
    }
    if (s == "Unknown") {
        k = BootReason::Unknown;
        return;
    }
    if (s == "Watchdog") {
        k = BootReason::Watchdog;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type BootReason");
}

void to_json(json& j, const InterfaceClass& k) noexcept {
    switch (k) {
    case InterfaceClass::Wired0:
        j = "Wired0";
        return;
    case InterfaceClass::Wired1:
        j = "Wired1";
        return;
    case InterfaceClass::Wired2:
        j = "Wired2";
        return;
    case InterfaceClass::Wired3:
        j = "Wired3";
        return;
    case InterfaceClass::Wireless0:
        j = "Wireless0";
        return;
    case InterfaceClass::Wireless1:
        j = "Wireless1";
        return;
    case InterfaceClass::Wireless2:
        j = "Wireless2";
        return;
    case InterfaceClass::Wireless3:
        j = "Wireless3";
        return;
    case InterfaceClass::Any:
        j = "Any";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::InterfaceClass";
}
void from_json(const json& j, InterfaceClass& k) {
    std::string s = j;
    if (s == "Wired0") {
        k = InterfaceClass::Wired0;
        return;
    }
    if (s == "Wired1") {
        k = InterfaceClass::Wired1;
        return;
    }
    if (s == "Wired2") {
        k = InterfaceClass::Wired2;
        return;
    }
    if (s == "Wired3") {
        k = InterfaceClass::Wired3;
        return;
    }
    if (s == "Wireless0") {
        k = InterfaceClass::Wireless0;
        return;
    }
    if (s == "Wireless1") {
        k = InterfaceClass::Wireless1;
        return;
    }
    if (s == "Wireless2") {
        k = InterfaceClass::Wireless2;
        return;
    }
    if (s == "Wireless3") {
        k = InterfaceClass::Wireless3;
        return;
    }
    if (s == "Any") {
        k = InterfaceClass::Any;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type InterfaceClass");
}

void to_json(json& j, const APNAuthenticationEnum& k) noexcept {
    switch (k) {
    case APNAuthenticationEnum::CHAP:
        j = "CHAP";
        return;
    case APNAuthenticationEnum::NONE:
        j = "NONE";
        return;
    case APNAuthenticationEnum::PAP:
        j = "PAP";
        return;
    case APNAuthenticationEnum::AUTO:
        j = "AUTO";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::APNAuthenticationEnum";
}
void from_json(const json& j, APNAuthenticationEnum& k) {
    std::string s = j;
    if (s == "CHAP") {
        k = APNAuthenticationEnum::CHAP;
        return;
    }
    if (s == "NONE") {
        k = APNAuthenticationEnum::NONE;
        return;
    }
    if (s == "PAP") {
        k = APNAuthenticationEnum::PAP;
        return;
    }
    if (s == "AUTO") {
        k = APNAuthenticationEnum::AUTO;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type APNAuthenticationEnum");
}

void to_json(json& j, const VPNTypeEnum& k) noexcept {
    switch (k) {
    case VPNTypeEnum::IKEv2:
        j = "IKEv2";
        return;
    case VPNTypeEnum::IPSec:
        j = "IPSec";
        return;
    case VPNTypeEnum::L2TP:
        j = "L2TP";
        return;
    case VPNTypeEnum::PPTP:
        j = "PPTP";
        return;
    case VPNTypeEnum::Other:
        j = "Other";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::VPNTypeEnum";
}
void from_json(const json& j, VPNTypeEnum& k) {
    std::string s = j;
    if (s == "IKEv2") {
        k = VPNTypeEnum::IKEv2;
        return;
    }
    if (s == "IPSec") {
        k = VPNTypeEnum::IPSec;
        return;
    }
    if (s == "L2TP") {
        k = VPNTypeEnum::L2TP;
        return;
    }
    if (s == "PPTP") {
        k = VPNTypeEnum::PPTP;
        return;
    }
    if (s == "Other") {
        k = VPNTypeEnum::Other;
        return;
    }

    throw std::out_of_range("Provided string " + s + " could not be converted to enum of type VPNTypeEnum");
}

void to_json(json& j, const ConfigureNetworkStatusEnum& k) noexcept {
    switch (k) {
    case ConfigureNetworkStatusEnum::Ready:
        j = "Ready";
        return;
    case ConfigureNetworkStatusEnum::Processing:
        j = "Processing";
        return;
    case ConfigureNetworkStatusEnum::Failed:
        j = "Failed";
        return;
    case ConfigureNetworkStatusEnum::Rejected:
        j = "Rejected";
        return;
    case ConfigureNetworkStatusEnum::NotSupported:
        j = "NotSupported";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::ConfigureNetworkStatusEnum";
}
void from_json(const json& j, ConfigureNetworkStatusEnum& k) {
    std::string s = j;
    if (s == "Ready") {
        k = ConfigureNetworkStatusEnum::Ready;
        return;
    }
    if (s == "Processing") {
        k = ConfigureNetworkStatusEnum::Processing;
        return;
    }
    if (s == "Failed") {
        k = ConfigureNetworkStatusEnum::Failed;
        return;
    }
    if (s == "Rejected") {
        k = ConfigureNetworkStatusEnum::Rejected;
        return;
    }
    if (s == "NotSupported") {
        k = ConfigureNetworkStatusEnum::NotSupported;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type ConfigureNetworkStatusEnum");
}

void to_json(json& j, const ConfigureNetworkFinalStatusEnum& k) noexcept {
    switch (k) {
    case ConfigureNetworkFinalStatusEnum::Ready:
        j = "Ready";
        return;
    case ConfigureNetworkFinalStatusEnum::Failed:
        j = "Failed";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::system::ConfigureNetworkFinalStatusEnum";
}
void from_json(const json& j, ConfigureNetworkFinalStatusEnum& k) {
    std::string s = j;
    if (s == "Ready") {
        k = ConfigureNetworkFinalStatusEnum::Ready;
        return;
    }
    if (s == "Failed") {
        k = ConfigureNetworkFinalStatusEnum::Failed;
        return;
    }

    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type ConfigureNetworkFinalStatusEnum");
}

void to_json(json& j, const APN& k) noexcept {
    j = json{
        {"apn", k.apn},
    };
    if (k.apn_user_name) {
        j["apn_user_name"] = k.apn_user_name.value();
    }
    if (k.apn_password) {
        j["apn_password"] = k.apn_password.value();
    }
    if (k.sim_pin) {
        j["sim_pin"] = k.sim_pin.value();
    }
    if (k.preferred_network) {
        j["preferred_network"] = k.preferred_network.value();
    }
    if (k.use_only_preferred_network) {
        j["use_only_preferred_network"] = k.use_only_preferred_network.value();
    }
    if (k.apn_authentication) {
        j["apn_authentication"] = k.apn_authentication.value();
    }
}
void from_json(const json& j, APN& k) {
    k.apn = j.at("apn");

    if (j.contains("apn_user_name")) {
        k.apn_user_name.emplace(j.at("apn_user_name"));
    }
    if (j.contains("apn_password")) {
        k.apn_password.emplace(j.at("apn_password"));
    }
    if (j.contains("sim_pin")) {
        k.sim_pin.emplace(j.at("sim_pin"));
    }
    if (j.contains("preferred_network")) {
        k.preferred_network.emplace(j.at("preferred_network"));
    }
    if (j.contains("use_only_preferred_network")) {
        k.use_only_preferred_network.emplace(j.at("use_only_preferred_network"));
    }
    if (j.contains("apn_authentication")) {
        k.apn_authentication.emplace(j.at("apn_authentication"));
    }
}

void to_json(json& j, const VPN& k) noexcept {
    j = json{
        {"server", k.server},
        {"type", k.type},
    };
    if (k.user) {
        j["user"] = k.user.value();
    }
    if (k.group) {
        j["group"] = k.group.value();
    }
    if (k.password) {
        j["password"] = k.password.value();
    }
    if (k.key) {
        j["key"] = k.key.value();
    }
}
void from_json(const json& j, VPN& k) {
    k.server = j.at("server");
    k.type = j.at("type");

    if (j.contains("user")) {
        k.user.emplace(j.at("user"));
    }
    if (j.contains("group")) {
        k.group.emplace(j.at("group"));
    }
    if (j.contains("password")) {
        k.password.emplace(j.at("password"));
    }
    if (j.contains("key")) {
        k.key.emplace(j.at("key"));
    }
}

void to_json(json& j, const ConfigureNetworkRequest& k) noexcept {
    j = json{
        {"request_id", k.request_id},
        {"interface", k.interface},
    };
    if (k.interface_name) {
        j["interface_name"] = k.interface_name.value();
    }
    if (k.apn) {
        j["apn"] = k.apn.value();
    }
    if (k.vpn) {
        j["vpn"] = k.vpn.value();
    }
}
void from_json(const json& j, ConfigureNetworkRequest& k) {
    k.request_id = j.at("request_id");
    k.interface = j.at("interface");

    if (j.contains("interface_name")) {
        k.interface_name.emplace(j.at("interface_name"));
    }
    if (j.contains("apn")) {
        k.apn.emplace(j.at("apn"));
    }
    if (j.contains("vpn")) {
        k.vpn.emplace(j.at("vpn"));
    }
}

void to_json(json& j, const ConfigureNetworkResponse& k) noexcept {
    j = json{
        {"status", k.status},
    };
    if (k.interface_address) {
        j["interface_address"] = k.interface_address.value();
    }
}
void from_json(const json& j, ConfigureNetworkResponse& k) {
    k.status = j.at("status");

    if (j.contains("interface_address")) {
        k.interface_address.emplace(j.at("interface_address"));
    }
}

void to_json(json& j, const ConfigureNetworkStatus& k) noexcept {
    j = json{
        {"request_id", k.request_id},
        {"status", k.status},
    };
    if (k.interface_address) {
        j["interface_address"] = k.interface_address.value();
    }
}
void from_json(const json& j, ConfigureNetworkStatus& k) {
    k.request_id = j.at("request_id");
    k.status = j.at("status");

    if (j.contains("interface_address")) {
        k.interface_address.emplace(j.at("interface_address"));
    }
}

} // namespace everest::lib::API::V1_0::types::system
