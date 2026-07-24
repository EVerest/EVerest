// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "system/wrapper.hpp"
#include "system/API.hpp"
#include "system/codec.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::system {

UpdateFirmwareResponse_Internal to_internal_api(UpdateFirmwareResponse_External const& val) {
    using SrcT = UpdateFirmwareResponse_External;
    using TarT = UpdateFirmwareResponse_Internal;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    case SrcT::InvalidCertificate:
        return TarT::InvalidCertificate;
    case SrcT::RevokedCertificate:
        return TarT::RevokedCertificate;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::UpdateFirmwareResponse_External");
}
UpdateFirmwareResponse_External to_external_api(UpdateFirmwareResponse_Internal const& val) {
    using SrcT = UpdateFirmwareResponse_Internal;
    using TarT = UpdateFirmwareResponse_External;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    case SrcT::InvalidCertificate:
        return TarT::InvalidCertificate;
    case SrcT::RevokedCertificate:
        return TarT::RevokedCertificate;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::UpdateFirmwareResponse_Internal");
}

UploadLogsStatus_Internal to_internal_api(UploadLogsStatus_External const& val) {
    using SrcT = UploadLogsStatus_External;
    using TarT = UploadLogsStatus_Internal;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::UploadLogsStatus_External");
}
UploadLogsStatus_External to_external_api(UploadLogsStatus_Internal const& val) {
    using SrcT = UploadLogsStatus_Internal;
    using TarT = UploadLogsStatus_External;
    switch (val) {
    case SrcT::Accepted:
        return TarT::Accepted;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::UploadLogsStatus_Internal");
}

LogStatusEnum_Internal to_internal_api(LogStatusEnum_External const& val) {
    using SrcT = LogStatusEnum_External;
    using TarT = LogStatusEnum_Internal;
    switch (val) {
    case SrcT::BadMessage:
        return TarT::BadMessage;
    case SrcT::Idle:
        return TarT::Idle;
    case SrcT::NotSupportedOperation:
        return TarT::NotSupportedOperation;
    case SrcT::PermissionDenied:
        return TarT::PermissionDenied;
    case SrcT::Uploaded:
        return TarT::Uploaded;
    case SrcT::UploadFailure:
        return TarT::UploadFailure;
    case SrcT::Uploading:
        return TarT::Uploading;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::LogStatusEnum_External");
}
LogStatusEnum_External to_external_api(LogStatusEnum_Internal const& val) {
    using SrcT = LogStatusEnum_Internal;
    using TarT = LogStatusEnum_External;
    switch (val) {
    case SrcT::BadMessage:
        return TarT::BadMessage;
    case SrcT::Idle:
        return TarT::Idle;
    case SrcT::NotSupportedOperation:
        return TarT::NotSupportedOperation;
    case SrcT::PermissionDenied:
        return TarT::PermissionDenied;
    case SrcT::Uploaded:
        return TarT::Uploaded;
    case SrcT::UploadFailure:
        return TarT::UploadFailure;
    case SrcT::Uploading:
        return TarT::Uploading;
    case SrcT::AcceptedCanceled:
        return TarT::AcceptedCanceled;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::LogStatusEnum_Internal");
}

FirmwareUpdateStatusEnum_Internal to_internal_api(FirmwareUpdateStatusEnum_External const& val) {
    using SrcT = FirmwareUpdateStatusEnum_External;
    using TarT = FirmwareUpdateStatusEnum_Internal;
    switch (val) {
    case SrcT::Downloaded:
        return TarT::Downloaded;
    case SrcT::DownloadFailed:
        return TarT::DownloadFailed;
    case SrcT::Downloading:
        return TarT::Downloading;
    case SrcT::DownloadScheduled:
        return TarT::DownloadScheduled;
    case SrcT::DownloadPaused:
        return TarT::DownloadPaused;
    case SrcT::Idle:
        return TarT::Idle;
    case SrcT::InstallationFailed:
        return TarT::InstallationFailed;
    case SrcT::Installing:
        return TarT::Installing;
    case SrcT::Installed:
        return TarT::Installed;
    case SrcT::InstallRebooting:
        return TarT::InstallRebooting;
    case SrcT::InstallScheduled:
        return TarT::InstallScheduled;
    case SrcT::InstallVerificationFailed:
        return TarT::InstallVerificationFailed;
    case SrcT::InvalidSignature:
        return TarT::InvalidSignature;
    case SrcT::SignatureVerified:
        return TarT::SignatureVerified;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::FirmwareUpdateStatusEnum_External");
}
FirmwareUpdateStatusEnum_External to_external_api(FirmwareUpdateStatusEnum_Internal const& val) {
    using SrcT = FirmwareUpdateStatusEnum_Internal;
    using TarT = FirmwareUpdateStatusEnum_External;
    switch (val) {
    case SrcT::Downloaded:
        return TarT::Downloaded;
    case SrcT::DownloadFailed:
        return TarT::DownloadFailed;
    case SrcT::Downloading:
        return TarT::Downloading;
    case SrcT::DownloadScheduled:
        return TarT::DownloadScheduled;
    case SrcT::DownloadPaused:
        return TarT::DownloadPaused;
    case SrcT::Idle:
        return TarT::Idle;
    case SrcT::InstallationFailed:
        return TarT::InstallationFailed;
    case SrcT::Installing:
        return TarT::Installing;
    case SrcT::Installed:
        return TarT::Installed;
    case SrcT::InstallRebooting:
        return TarT::InstallRebooting;
    case SrcT::InstallScheduled:
        return TarT::InstallScheduled;
    case SrcT::InstallVerificationFailed:
        return TarT::InstallVerificationFailed;
    case SrcT::InvalidSignature:
        return TarT::InvalidSignature;
    case SrcT::SignatureVerified:
        return TarT::SignatureVerified;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::FirmwareUpdateStatusEnum_Internal");
}

ResetType_Internal to_internal_api(ResetType_External const& val) {
    using SrcT = ResetType_External;
    using TarT = ResetType_Internal;
    switch (val) {
    case SrcT::Soft:
        return TarT::Soft;
    case SrcT::Hard:
        return TarT::Hard;
    case SrcT::NotSpecified:
        return TarT::NotSpecified;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::ResetType_External");
}
ResetType_External to_external_api(ResetType_Internal const& val) {
    using SrcT = ResetType_Internal;
    using TarT = ResetType_External;
    switch (val) {
    case SrcT::Soft:
        return TarT::Soft;
    case SrcT::Hard:
        return TarT::Hard;
    case SrcT::NotSpecified:
        return TarT::NotSpecified;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::ResetType_Internal");
}

BootReason_Internal to_internal_api(BootReason_External const& val) {
    using SrcT = BootReason_External;
    using TarT = BootReason_Internal;
    switch (val) {
    case SrcT::ApplicationReset:
        return TarT::ApplicationReset;
    case SrcT::FirmwareUpdate:
        return TarT::FirmwareUpdate;
    case SrcT::LocalReset:
        return TarT::LocalReset;
    case SrcT::PowerUp:
        return TarT::PowerUp;
    case SrcT::RemoteReset:
        return TarT::RemoteReset;
    case SrcT::ScheduledReset:
        return TarT::ScheduledReset;
    case SrcT::Triggered:
        return TarT::Triggered;
    case SrcT::Unknown:
        return TarT::Unknown;
    case SrcT::Watchdog:
        return TarT::Watchdog;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::BootReason_External");
}
BootReason_External to_external_api(BootReason_Internal const& val) {
    using SrcT = BootReason_Internal;
    using TarT = BootReason_External;
    switch (val) {
    case SrcT::ApplicationReset:
        return TarT::ApplicationReset;
    case SrcT::FirmwareUpdate:
        return TarT::FirmwareUpdate;
    case SrcT::LocalReset:
        return TarT::LocalReset;
    case SrcT::PowerUp:
        return TarT::PowerUp;
    case SrcT::RemoteReset:
        return TarT::RemoteReset;
    case SrcT::ScheduledReset:
        return TarT::ScheduledReset;
    case SrcT::Triggered:
        return TarT::Triggered;
    case SrcT::Unknown:
        return TarT::Unknown;
    case SrcT::Watchdog:
        return TarT::Watchdog;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::BootReason_Internal");
}

FirmwareUpdateRequest_Internal to_internal_api(FirmwareUpdateRequest_External const& val) {
    FirmwareUpdateRequest_Internal result;
    result.request_id = val.request_id;
    result.location = val.location;
    result.retries = val.retries;
    result.retry_interval_s = val.retry_interval_s;
    result.retrieve_timestamp = val.retrieve_timestamp;
    result.install_timestamp = val.install_timestamp;
    result.signing_certificate = val.signing_certificate;
    result.signature = val.signature;
    return result;
}
FirmwareUpdateRequest_External to_external_api(FirmwareUpdateRequest_Internal const& val) {
    FirmwareUpdateRequest_External result;
    result.request_id = val.request_id;
    result.location = val.location;
    result.retries = val.retries;
    result.retry_interval_s = val.retry_interval_s;
    result.retrieve_timestamp = val.retrieve_timestamp;
    result.install_timestamp = val.install_timestamp;
    result.signing_certificate = val.signing_certificate;
    result.signature = val.signature;
    return result;
}

UploadLogsRequest_Internal to_internal_api(UploadLogsRequest_External const& val) {
    UploadLogsRequest_Internal result;
    result.location = val.location;
    result.retries = val.retries;
    result.retry_interval_s = val.retry_interval_s;
    result.oldest_timestamp = val.oldest_timestamp;
    result.latest_timestamp = val.latest_timestamp;
    result.type = val.type;
    result.request_id = val.request_id;
    return result;
}
UploadLogsRequest_External to_external_api(UploadLogsRequest_Internal const& val) {
    UploadLogsRequest_External result;
    result.location = val.location;
    result.retries = val.retries;
    result.retry_interval_s = val.retry_interval_s;
    result.oldest_timestamp = val.oldest_timestamp;
    result.latest_timestamp = val.latest_timestamp;
    result.type = val.type;
    result.request_id = val.request_id;
    return result;
}

UploadLogsResponse_Internal to_internal_api(UploadLogsResponse_External const& val) {
    UploadLogsResponse_Internal result;
    result.upload_logs_status = to_internal_api(val.upload_logs_status);
    result.file_name = val.file_name;
    return result;
}
UploadLogsResponse_External to_external_api(UploadLogsResponse_Internal const& val) {
    UploadLogsResponse_External result;
    result.upload_logs_status = to_external_api(val.upload_logs_status);
    result.file_name = val.file_name;
    return result;
}

LogStatus_Internal to_internal_api(LogStatus_External const& val) {
    LogStatus_Internal result;
    result.log_status = to_internal_api(val.log_status);
    result.request_id = val.request_id;
    return result;
}
LogStatus_External to_external_api(LogStatus_Internal const& val) {
    LogStatus_External result;
    result.log_status = to_external_api(val.log_status);
    result.request_id = val.request_id;
    return result;
}

FirmwareUpdateMetadata_Internal to_internal_api(FirmwareUpdateMetadata_External const& val) {
    FirmwareUpdateMetadata_Internal result;
    result.disable_connectors_during_install = val.disable_connectors_during_install;
    return result;
}
FirmwareUpdateMetadata_External to_external_api(FirmwareUpdateMetadata_Internal const& val) {
    FirmwareUpdateMetadata_External result;
    result.disable_connectors_during_install = val.disable_connectors_during_install;
    return result;
}

FirmwareUpdateStatus_Internal to_internal_api(FirmwareUpdateStatus_External const& val) {
    FirmwareUpdateStatus_Internal result;
    result.firmware_update_status = to_internal_api(val.firmware_update_status);
    result.request_id = val.request_id;
    if (val.firmware_update_metadata.has_value()) {
        result.firmware_update_metadata.emplace(to_internal_api(val.firmware_update_metadata.value()));
    }
    return result;
}
FirmwareUpdateStatus_External to_external_api(FirmwareUpdateStatus_Internal const& val) {
    FirmwareUpdateStatus_External result;
    result.firmware_update_status = to_external_api(val.firmware_update_status);
    result.request_id = val.request_id;
    if (val.firmware_update_metadata.has_value()) {
        result.firmware_update_metadata.emplace(to_external_api(val.firmware_update_metadata.value()));
    }
    return result;
}

InterfaceClass_Internal to_internal_api(InterfaceClass_External const& val) {
    using SrcT = InterfaceClass_External;
    using TarT = InterfaceClass_Internal;
    switch (val) {
    case SrcT::Wired0:
        return TarT::Wired0;
    case SrcT::Wired1:
        return TarT::Wired1;
    case SrcT::Wired2:
        return TarT::Wired2;
    case SrcT::Wired3:
        return TarT::Wired3;
    case SrcT::Wireless0:
        return TarT::Wireless0;
    case SrcT::Wireless1:
        return TarT::Wireless1;
    case SrcT::Wireless2:
        return TarT::Wireless2;
    case SrcT::Wireless3:
        return TarT::Wireless3;
    case SrcT::Any:
        return TarT::Any;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::InterfaceClass_External");
}
InterfaceClass_External to_external_api(InterfaceClass_Internal const& val) {
    using SrcT = InterfaceClass_Internal;
    using TarT = InterfaceClass_External;
    switch (val) {
    case SrcT::Wired0:
        return TarT::Wired0;
    case SrcT::Wired1:
        return TarT::Wired1;
    case SrcT::Wired2:
        return TarT::Wired2;
    case SrcT::Wired3:
        return TarT::Wired3;
    case SrcT::Wireless0:
        return TarT::Wireless0;
    case SrcT::Wireless1:
        return TarT::Wireless1;
    case SrcT::Wireless2:
        return TarT::Wireless2;
    case SrcT::Wireless3:
        return TarT::Wireless3;
    case SrcT::Any:
        return TarT::Any;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::InterfaceClass_Internal");
}

APNAuthenticationEnum_Internal to_internal_api(APNAuthenticationEnum_External const& val) {
    using SrcT = APNAuthenticationEnum_External;
    using TarT = APNAuthenticationEnum_Internal;
    switch (val) {
    case SrcT::CHAP:
        return TarT::CHAP;
    case SrcT::NONE:
        return TarT::NONE;
    case SrcT::PAP:
        return TarT::PAP;
    case SrcT::AUTO:
        return TarT::AUTO;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::APNAuthenticationEnum_External");
}
APNAuthenticationEnum_External to_external_api(APNAuthenticationEnum_Internal const& val) {
    using SrcT = APNAuthenticationEnum_Internal;
    using TarT = APNAuthenticationEnum_External;
    switch (val) {
    case SrcT::CHAP:
        return TarT::CHAP;
    case SrcT::NONE:
        return TarT::NONE;
    case SrcT::PAP:
        return TarT::PAP;
    case SrcT::AUTO:
        return TarT::AUTO;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::APNAuthenticationEnum_Internal");
}

VPNTypeEnum_Internal to_internal_api(VPNTypeEnum_External const& val) {
    using SrcT = VPNTypeEnum_External;
    using TarT = VPNTypeEnum_Internal;
    switch (val) {
    case SrcT::IKEv2:
        return TarT::IKEv2;
    case SrcT::IPSec:
        return TarT::IPSec;
    case SrcT::L2TP:
        return TarT::L2TP;
    case SrcT::PPTP:
        return TarT::PPTP;
    case SrcT::Other:
        return TarT::Other;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::VPNTypeEnum_External");
}
VPNTypeEnum_External to_external_api(VPNTypeEnum_Internal const& val) {
    using SrcT = VPNTypeEnum_Internal;
    using TarT = VPNTypeEnum_External;
    switch (val) {
    case SrcT::IKEv2:
        return TarT::IKEv2;
    case SrcT::IPSec:
        return TarT::IPSec;
    case SrcT::L2TP:
        return TarT::L2TP;
    case SrcT::PPTP:
        return TarT::PPTP;
    case SrcT::Other:
        return TarT::Other;
    }
    throw std::out_of_range("Unexpected value for everest::lib::API::V1_0::types::system::VPNTypeEnum_Internal");
}

ConfigureNetworkStatusEnum_Internal to_internal_api(ConfigureNetworkStatusEnum_External const& val) {
    using SrcT = ConfigureNetworkStatusEnum_External;
    using TarT = ConfigureNetworkStatusEnum_Internal;
    switch (val) {
    case SrcT::Ready:
        return TarT::Ready;
    case SrcT::Processing:
        return TarT::Processing;
    case SrcT::Failed:
        return TarT::Failed;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::NotSupported:
        return TarT::NotSupported;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::ConfigureNetworkStatusEnum_External");
}
ConfigureNetworkStatusEnum_External to_external_api(ConfigureNetworkStatusEnum_Internal const& val) {
    using SrcT = ConfigureNetworkStatusEnum_Internal;
    using TarT = ConfigureNetworkStatusEnum_External;
    switch (val) {
    case SrcT::Ready:
        return TarT::Ready;
    case SrcT::Processing:
        return TarT::Processing;
    case SrcT::Failed:
        return TarT::Failed;
    case SrcT::Rejected:
        return TarT::Rejected;
    case SrcT::NotSupported:
        return TarT::NotSupported;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::ConfigureNetworkStatusEnum_Internal");
}

ConfigureNetworkFinalStatusEnum_Internal to_internal_api(ConfigureNetworkFinalStatusEnum_External const& val) {
    using SrcT = ConfigureNetworkFinalStatusEnum_External;
    using TarT = ConfigureNetworkFinalStatusEnum_Internal;
    switch (val) {
    case SrcT::Ready:
        return TarT::Ready;
    case SrcT::Failed:
        return TarT::Failed;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::ConfigureNetworkFinalStatusEnum_External");
}
ConfigureNetworkFinalStatusEnum_External to_external_api(ConfigureNetworkFinalStatusEnum_Internal const& val) {
    using SrcT = ConfigureNetworkFinalStatusEnum_Internal;
    using TarT = ConfigureNetworkFinalStatusEnum_External;
    switch (val) {
    case SrcT::Ready:
        return TarT::Ready;
    case SrcT::Failed:
        return TarT::Failed;
    }
    throw std::out_of_range(
        "Unexpected value for everest::lib::API::V1_0::types::system::ConfigureNetworkFinalStatusEnum_Internal");
}

APN_Internal to_internal_api(APN_External const& val) {
    APN_Internal result;
    result.apn = val.apn;
    result.apn_user_name = val.apn_user_name;
    result.apn_password = val.apn_password;
    result.sim_pin = val.sim_pin;
    result.preferred_network = val.preferred_network;
    result.use_only_preferred_network = val.use_only_preferred_network;
    if (val.apn_authentication.has_value()) {
        result.apn_authentication.emplace(to_internal_api(val.apn_authentication.value()));
    }
    return result;
}
APN_External to_external_api(APN_Internal const& val) {
    APN_External result;
    result.apn = val.apn;
    result.apn_user_name = val.apn_user_name;
    result.apn_password = val.apn_password;
    result.sim_pin = val.sim_pin;
    result.preferred_network = val.preferred_network;
    result.use_only_preferred_network = val.use_only_preferred_network;
    if (val.apn_authentication.has_value()) {
        result.apn_authentication.emplace(to_external_api(val.apn_authentication.value()));
    }
    return result;
}

VPN_Internal to_internal_api(VPN_External const& val) {
    VPN_Internal result;
    result.server = val.server;
    result.user = val.user;
    result.group = val.group;
    result.password = val.password;
    result.key = val.key;
    result.type = to_internal_api(val.type);
    return result;
}
VPN_External to_external_api(VPN_Internal const& val) {
    VPN_External result;
    result.server = val.server;
    result.user = val.user;
    result.group = val.group;
    result.password = val.password;
    result.key = val.key;
    result.type = to_external_api(val.type);
    return result;
}

ConfigureNetworkRequest_Internal to_internal_api(ConfigureNetworkRequest_External const& val) {
    ConfigureNetworkRequest_Internal result;
    result.request_id = val.request_id;
    result.interface = to_internal_api(val.interface);
    result.interface_name = val.interface_name;
    if (val.apn.has_value()) {
        result.apn.emplace(to_internal_api(val.apn.value()));
    }
    if (val.vpn.has_value()) {
        result.vpn.emplace(to_internal_api(val.vpn.value()));
    }
    return result;
}
ConfigureNetworkRequest_External to_external_api(ConfigureNetworkRequest_Internal const& val) {
    ConfigureNetworkRequest_External result;
    result.request_id = val.request_id;
    result.interface = to_external_api(val.interface);
    result.interface_name = val.interface_name;
    if (val.apn.has_value()) {
        result.apn.emplace(to_external_api(val.apn.value()));
    }
    if (val.vpn.has_value()) {
        result.vpn.emplace(to_external_api(val.vpn.value()));
    }
    return result;
}

ConfigureNetworkResponse_Internal to_internal_api(ConfigureNetworkResponse_External const& val) {
    ConfigureNetworkResponse_Internal result;
    result.status = to_internal_api(val.status);
    result.interface_address = val.interface_address;
    return result;
}
ConfigureNetworkResponse_External to_external_api(ConfigureNetworkResponse_Internal const& val) {
    ConfigureNetworkResponse_External result;
    result.status = to_external_api(val.status);
    result.interface_address = val.interface_address;
    return result;
}

ConfigureNetworkStatus_Internal to_internal_api(ConfigureNetworkStatus_External const& val) {
    ConfigureNetworkStatus_Internal result;
    result.request_id = val.request_id;
    result.status = to_internal_api(val.status);
    result.interface_address = val.interface_address;
    return result;
}
ConfigureNetworkStatus_External to_external_api(ConfigureNetworkStatus_Internal const& val) {
    ConfigureNetworkStatus_External result;
    result.request_id = val.request_id;
    result.status = to_external_api(val.status);
    result.interface_address = val.interface_address;
    return result;
}

} // namespace everest::lib::API::V1_0::types::system
