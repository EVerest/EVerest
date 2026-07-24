// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::API::V1_0::types::system {

enum class UpdateFirmwareResponse {
    Accepted,
    Rejected,
    AcceptedCanceled,
    InvalidCertificate,
    RevokedCertificate,
};

enum class UploadLogsStatus {
    Accepted,
    Rejected,
    AcceptedCanceled,
};

enum class LogStatusEnum {
    BadMessage,
    Idle,
    NotSupportedOperation,
    PermissionDenied,
    Uploaded,
    UploadFailure,
    Uploading,
    AcceptedCanceled,
};

enum class FirmwareUpdateStatusEnum {
    Downloaded,
    DownloadFailed,
    Downloading,
    DownloadScheduled,
    DownloadPaused,
    Idle,
    InstallationFailed,
    Installing,
    Installed,
    InstallRebooting,
    InstallScheduled,
    InstallVerificationFailed,
    InvalidSignature,
    SignatureVerified,
};

enum class ResetType {
    Soft,
    Hard,
    NotSpecified,
};

enum class BootReason {
    ApplicationReset,
    FirmwareUpdate,
    LocalReset,
    PowerUp,
    RemoteReset,
    ScheduledReset,
    Triggered,
    Unknown,
    Watchdog,
};

struct FirmwareUpdateRequest {
    int32_t request_id;
    std::string location;
    std::optional<int32_t> retries;
    std::optional<int32_t> retry_interval_s;
    std::optional<std::string> retrieve_timestamp;
    std::optional<std::string> install_timestamp;
    std::optional<std::string> signing_certificate;
    std::optional<std::string> signature;
};

struct UploadLogsRequest {
    std::string location;
    std::optional<int32_t> retries;
    std::optional<int32_t> retry_interval_s;
    std::optional<std::string> oldest_timestamp;
    std::optional<std::string> latest_timestamp;
    std::optional<std::string> type;
    std::optional<int32_t> request_id;
};

struct UploadLogsResponse {
    UploadLogsStatus upload_logs_status;
    std::optional<std::string> file_name;
};

struct LogStatus {
    LogStatusEnum log_status;
    int32_t request_id;
};

struct FirmwareUpdateMetadata {
    std::optional<bool> disable_connectors_during_install;
};

struct FirmwareUpdateStatus {
    FirmwareUpdateStatusEnum firmware_update_status;
    int32_t request_id;
    std::optional<FirmwareUpdateMetadata> firmware_update_metadata;
};

struct ResetRequest {
    ResetType type;
    bool scheduled;
};

// Network configuration (mirrors types/network.yaml)

enum class InterfaceClass {
    Wired0,
    Wired1,
    Wired2,
    Wired3,
    Wireless0,
    Wireless1,
    Wireless2,
    Wireless3,
    Any,
};

enum class APNAuthenticationEnum {
    CHAP,
    NONE,
    PAP,
    AUTO,
};

enum class VPNTypeEnum {
    IKEv2,
    IPSec,
    L2TP,
    PPTP,
    Other,
};

enum class ConfigureNetworkStatusEnum {
    Ready,
    Processing,
    Failed,
    Rejected,
    NotSupported,
};

enum class ConfigureNetworkFinalStatusEnum {
    Ready,
    Failed,
};

struct APN {
    std::string apn;
    std::optional<std::string> apn_user_name;
    std::optional<std::string> apn_password;
    std::optional<std::string> sim_pin;
    std::optional<std::string> preferred_network;
    std::optional<bool> use_only_preferred_network;
    std::optional<APNAuthenticationEnum> apn_authentication;
};

struct VPN {
    std::string server;
    std::optional<std::string> user;
    std::optional<std::string> group;
    std::optional<std::string> password;
    std::optional<std::string> key;
    VPNTypeEnum type;
};

struct ConfigureNetworkRequest {
    int32_t request_id;
    InterfaceClass interface;
    std::optional<std::string> interface_name;
    std::optional<APN> apn;
    std::optional<VPN> vpn;
};

struct ConfigureNetworkResponse {
    ConfigureNetworkStatusEnum status;
    std::optional<std::string> interface_address;
};

struct ConfigureNetworkStatus {
    int32_t request_id;
    ConfigureNetworkFinalStatusEnum status;
    std::optional<std::string> interface_address;
};

} // namespace everest::lib::API::V1_0::types::system
