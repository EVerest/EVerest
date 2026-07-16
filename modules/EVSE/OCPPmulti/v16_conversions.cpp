// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "v16_conversions.hpp"

namespace ocpp_multi::v16_conversions {

ocpp::v2::AuthorizationStatusEnum convert(ocpp::v16::AuthorizationStatus value) {
    switch (value) {
    case ocpp::v16::AuthorizationStatus::Accepted:
        return ocpp::v2::AuthorizationStatusEnum::Accepted;
    case ocpp::v16::AuthorizationStatus::Blocked:
        return ocpp::v2::AuthorizationStatusEnum::Blocked;
    case ocpp::v16::AuthorizationStatus::Expired:
        return ocpp::v2::AuthorizationStatusEnum::Expired;
    case ocpp::v16::AuthorizationStatus::Invalid:
        return ocpp::v2::AuthorizationStatusEnum::Invalid;
    case ocpp::v16::AuthorizationStatus::ConcurrentTx:
        return ocpp::v2::AuthorizationStatusEnum::ConcurrentTx;
    default:
        return ocpp::v2::AuthorizationStatusEnum::Unknown;
    }
}

ocpp::v2::ChangeAvailabilityStatusEnum convert(ocpp::v16::AvailabilityStatus value) {
    switch (value) {
    case ocpp::v16::AvailabilityStatus::Accepted:
        return ocpp::v2::ChangeAvailabilityStatusEnum::Accepted;
    case ocpp::v16::AvailabilityStatus::Scheduled:
        return ocpp::v2::ChangeAvailabilityStatusEnum::Scheduled;
    case ocpp::v16::AvailabilityStatus::Rejected:
    default:
        return ocpp::v2::ChangeAvailabilityStatusEnum::Rejected;
    }
}

ocpp::v2::BootNotificationResponse convert(const ocpp::v16::BootNotificationResponse& value) {
    ocpp::v2::BootNotificationResponse result{};
    result.currentTime = value.currentTime;
    result.interval = value.interval;
    switch (value.status) {
    case ocpp::v16::RegistrationStatus::Accepted:
        result.status = ocpp::v2::RegistrationStatusEnum::Accepted;
        break;
    case ocpp::v16::RegistrationStatus::Pending:
        result.status = ocpp::v2::RegistrationStatusEnum::Pending;
        break;
    case ocpp::v16::RegistrationStatus::Rejected:
    default:
        result.status = ocpp::v2::RegistrationStatusEnum::Rejected;
        break;
    }
    return result;
}

ocpp::v16::BootReasonEnum convert(ocpp::v2::BootReasonEnum value) {
    switch (value) {
    case ocpp::v2::BootReasonEnum::ApplicationReset:
        return ocpp::v16::BootReasonEnum::ApplicationReset;
    case ocpp::v2::BootReasonEnum::FirmwareUpdate:
        return ocpp::v16::BootReasonEnum::FirmwareUpdate;
    case ocpp::v2::BootReasonEnum::LocalReset:
        return ocpp::v16::BootReasonEnum::LocalReset;
    case ocpp::v2::BootReasonEnum::PowerUp:
        return ocpp::v16::BootReasonEnum::PowerUp;
    case ocpp::v2::BootReasonEnum::RemoteReset:
        return ocpp::v16::BootReasonEnum::RemoteReset;
    case ocpp::v2::BootReasonEnum::ScheduledReset:
        return ocpp::v16::BootReasonEnum::ScheduledReset;
    case ocpp::v2::BootReasonEnum::Triggered:
        return ocpp::v16::BootReasonEnum::Triggered;
    case ocpp::v2::BootReasonEnum::Watchdog:
        return ocpp::v16::BootReasonEnum::Watchdog;
    case ocpp::v2::BootReasonEnum::Unknown:
    default:
        return ocpp::v16::BootReasonEnum::Unknown;
    }
}

ocpp::v16::ChargingRateUnit convert(ocpp::v2::ChargingRateUnitEnum value) {
    switch (value) {
    case ocpp::v2::ChargingRateUnitEnum::W:
        return ocpp::v16::ChargingRateUnit::W;
    case ocpp::v2::ChargingRateUnitEnum::A:
    default:
        return ocpp::v16::ChargingRateUnit::A;
    }
}

ocpp::v2::ChargingRateUnitEnum convert(ocpp::v16::ChargingRateUnit value) {
    switch (value) {
    case ocpp::v16::ChargingRateUnit::W:
        return ocpp::v2::ChargingRateUnitEnum::W;
    case ocpp::v16::ChargingRateUnit::A:
    default:
        return ocpp::v2::ChargingRateUnitEnum::A;
    }
}

ocpp::v2::DataTransferRequest convert(const ocpp::v16::DataTransferRequest& value) {
    ocpp::v2::DataTransferRequest result{};
    result.vendorId = value.vendorId;
    result.messageId = value.messageId;
    result.data = value.data;
    return result;
}

ocpp::v16::DataTransferResponse convert(const ocpp::v2::DataTransferResponse& value) {
    ocpp::v16::DataTransferResponse result{};
    switch (value.status) {
    case ocpp::v2::DataTransferStatusEnum::Accepted:
        result.status = ocpp::v16::DataTransferStatus::Accepted;
        break;
    case ocpp::v2::DataTransferStatusEnum::Rejected:
        result.status = ocpp::v16::DataTransferStatus::Rejected;
        break;
    case ocpp::v2::DataTransferStatusEnum::UnknownMessageId:
        result.status = ocpp::v16::DataTransferStatus::UnknownMessageId;
        break;
    case ocpp::v2::DataTransferStatusEnum::UnknownVendorId:
        result.status = ocpp::v16::DataTransferStatus::UnknownVendorId;
        break;
    }
    result.data = value.data;
    return result;
}

ocpp::v2::DataTransferStatusEnum convert(ocpp::v16::DataTransferStatus value) {
    switch (value) {
    case ocpp::v16::DataTransferStatus::Accepted:
        return ocpp::v2::DataTransferStatusEnum::Accepted;
    case ocpp::v16::DataTransferStatus::UnknownMessageId:
        return ocpp::v2::DataTransferStatusEnum::UnknownMessageId;
    case ocpp::v16::DataTransferStatus::UnknownVendorId:
        return ocpp::v2::DataTransferStatusEnum::UnknownVendorId;
    case ocpp::v16::DataTransferStatus::Rejected:
    default:
        return ocpp::v2::DataTransferStatusEnum::Rejected;
    }
}

ocpp::v2::EnhancedChargingSchedulePeriod convert(const ocpp::v16::EnhancedChargingSchedulePeriod& value) {
    ocpp::v2::EnhancedChargingSchedulePeriod result;
    result.startPeriod = value.startPeriod;
    result.limit = value.limit;
    result.stackLevel = value.stackLevel;
    result.numberPhases = value.numberPhases;
    // TODO(james-ctc): periodTransformed need handling
    return result;
}

ocpp::v2::EnhancedCompositeSchedule convert(std::int32_t evse_id, const ocpp::v16::EnhancedChargingSchedule& value) {
    ocpp::v2::EnhancedCompositeSchedule result;
    result.evseId = evse_id;
    result.chargingRateUnit = convert(value.chargingRateUnit);
    for (const auto& item : value.chargingSchedulePeriod) {
        result.chargingSchedulePeriod.push_back(convert(item));
    }
    result.duration = value.duration.value_or(0);
    result.scheduleStart = value.startSchedule.value_or(ocpp::DateTime{});
    // TODO(james-ctc): minChargingRate need handling
    return result;
}

ocpp::FirmwareStatusNotification convert(ocpp::v2::FirmwareStatusEnum value) {
    switch (value) {
    case ocpp::v2::FirmwareStatusEnum::Downloaded:
        return ocpp::FirmwareStatusNotification::Downloaded;
    case ocpp::v2::FirmwareStatusEnum::DownloadFailed:
        return ocpp::FirmwareStatusNotification::DownloadFailed;
    case ocpp::v2::FirmwareStatusEnum::Downloading:
        return ocpp::FirmwareStatusNotification::Downloading;
    case ocpp::v2::FirmwareStatusEnum::DownloadScheduled:
        return ocpp::FirmwareStatusNotification::DownloadScheduled;
    case ocpp::v2::FirmwareStatusEnum::DownloadPaused:
        return ocpp::FirmwareStatusNotification::DownloadPaused;
    case ocpp::v2::FirmwareStatusEnum::Idle:
        return ocpp::FirmwareStatusNotification::Idle;
    case ocpp::v2::FirmwareStatusEnum::Installing:
        return ocpp::FirmwareStatusNotification::Installing;
    case ocpp::v2::FirmwareStatusEnum::Installed:
        return ocpp::FirmwareStatusNotification::Installed;
    case ocpp::v2::FirmwareStatusEnum::InstallRebooting:
        return ocpp::FirmwareStatusNotification::InstallRebooting;
    case ocpp::v2::FirmwareStatusEnum::InstallScheduled:
        return ocpp::FirmwareStatusNotification::InstallScheduled;
    case ocpp::v2::FirmwareStatusEnum::InstallVerificationFailed:
        return ocpp::FirmwareStatusNotification::InstallVerificationFailed;
    case ocpp::v2::FirmwareStatusEnum::InvalidSignature:
        return ocpp::FirmwareStatusNotification::InvalidSignature;
    case ocpp::v2::FirmwareStatusEnum::SignatureVerified:
        return ocpp::FirmwareStatusNotification::SignatureVerified;
    case ocpp::v2::FirmwareStatusEnum::InstallationFailed:
    default:
        return ocpp::FirmwareStatusNotification::InstallationFailed;
    }
}

ocpp::v16::GetLogResponse convert(const ocpp::v2::GetLogResponse& value) {
    ocpp::v16::GetLogResponse result{};
    switch (value.status) {
    case ocpp::v2::LogStatusEnum::Accepted:
        result.status = ocpp::v16::LogStatusEnumType::Accepted;
        break;
    case ocpp::v2::LogStatusEnum::AcceptedCanceled:
        result.status = ocpp::v16::LogStatusEnumType::AcceptedCanceled;
        break;
    case ocpp::v2::LogStatusEnum::Rejected:
    default:
        result.status = ocpp::v16::LogStatusEnumType::Rejected;
        break;
    }
    result.filename = value.filename;
    return result;
}

ocpp::v2::LogEnum convert(ocpp::v16::LogEnumType value) {
    switch (value) {
    case ocpp::v16::LogEnumType::SecurityLog:
        return ocpp::v2::LogEnum::SecurityLog;
    case ocpp::v16::LogEnumType::DiagnosticsLog:
    default:
        return ocpp::v2::LogEnum::DiagnosticsLog;
    }
}

ocpp::v16::AvailabilityType convert(ocpp::v2::OperationalStatusEnum value) {
    switch (value) {
    case ocpp::v2::OperationalStatusEnum::Operative:
        return ocpp::v16::AvailabilityType::Operative;
    case ocpp::v2::OperationalStatusEnum::Inoperative:
    default:
        return ocpp::v16::AvailabilityType::Inoperative;
    }
}

ocpp::v16::ReservationStatus convert(ocpp::v2::ReserveNowStatusEnum value) {
    switch (value) {
    case ocpp::v2::ReserveNowStatusEnum::Accepted:
        return ocpp::v16::ReservationStatus::Accepted;
    case ocpp::v2::ReserveNowStatusEnum::Faulted:
        return ocpp::v16::ReservationStatus::Faulted;
    case ocpp::v2::ReserveNowStatusEnum::Occupied:
        return ocpp::v16::ReservationStatus::Occupied;
    case ocpp::v2::ReserveNowStatusEnum::Unavailable:
        return ocpp::v16::ReservationStatus::Unavailable;
    case ocpp::v2::ReserveNowStatusEnum::Rejected:
    default:
        return ocpp::v16::ReservationStatus::Rejected;
    }
}

GenericChargePointCallbacks::ResetType convert(ocpp::v16::ResetType value) {
    using ResetType = GenericChargePointCallbacks::ResetType;
    switch (value) {
    case ocpp::v16::ResetType::Hard:
        return ResetType::Hard;
    case ocpp::v16::ResetType::Soft:
    default:
        return ResetType::Soft;
    }
}

ocpp::v16::DataTransferResponse convert(const ocpp::v2::SetDisplayMessageResponse& value) {
    ocpp::v16::DataTransferResponse result{};
    switch (value.status) {
    case ocpp::v2::DisplayMessageStatusEnum::Accepted:
        result.status = ocpp::v16::DataTransferStatus::Accepted;
        break;
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedMessageFormat:
    case ocpp::v2::DisplayMessageStatusEnum::LanguageNotSupported:
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedPriority:
    case ocpp::v2::DisplayMessageStatusEnum::NotSupportedState:
    case ocpp::v2::DisplayMessageStatusEnum::UnknownTransaction:
    case ocpp::v2::DisplayMessageStatusEnum::Rejected:
    default:
        result.status = ocpp::v16::DataTransferStatus::Rejected;
        break;
    }
    if (value.statusInfo) {
        result.data = value.statusInfo->reasonCode;
    }
    return result;
}

ocpp::v16::UnlockStatus convert(const ocpp::v2::UnlockConnectorResponse& value) {
    // OCPP module only returns Unlocked or NotSupported
    switch (value.status) {
    case ocpp::v2::UnlockStatusEnum::Unlocked:
        return ocpp::v16::UnlockStatus::Unlocked;
    case ocpp::v2::UnlockStatusEnum::UnlockFailed:
    case ocpp::v2::UnlockStatusEnum::OngoingAuthorizedTransaction:
    case ocpp::v2::UnlockStatusEnum::UnknownConnector:
    default:
        return ocpp::v16::UnlockStatus::NotSupported;
    }
}

ocpp::v2::UpdateFirmwareRequest convert(const ocpp::v16::UpdateFirmwareRequest& value) {
    ocpp::v2::UpdateFirmwareRequest result{};
    ocpp::v2::Firmware firmware{};
    firmware.location = value.location;
    firmware.retrieveDateTime = value.retrieveDate;
    result.requestId = -1;
    result.firmware = std::move(firmware);
    result.retries = value.retries;
    result.retryInterval = value.retryInterval;
    return result;
}

ocpp::v2::UpdateFirmwareRequest convert(const ocpp::v16::SignedUpdateFirmwareRequest& value) {
    ocpp::v2::UpdateFirmwareRequest request{};
    request.requestId = value.requestId;
    request.firmware.location = static_cast<std::string>(value.firmware.location);
    request.firmware.retrieveDateTime = value.firmware.retrieveDateTime;
    request.firmware.signingCertificate = value.firmware.signingCertificate;
    request.firmware.signature = value.firmware.signature;
    request.firmware.installDateTime = value.firmware.installDateTime;
    request.retries = value.retries;
    request.retryInterval = value.retryInterval;
    return request;
}

ocpp::v16::UpdateFirmwareStatusEnumType convert(const ocpp::v2::UpdateFirmwareResponse& value) {
    switch (value.status) {
    case ocpp::v2::UpdateFirmwareStatusEnum::Accepted:
        return ocpp::v16::UpdateFirmwareStatusEnumType::Accepted;
    case ocpp::v2::UpdateFirmwareStatusEnum::AcceptedCanceled:
        return ocpp::v16::UpdateFirmwareStatusEnumType::AcceptedCanceled;
    case ocpp::v2::UpdateFirmwareStatusEnum::InvalidCertificate:
        return ocpp::v16::UpdateFirmwareStatusEnumType::InvalidCertificate;
    case ocpp::v2::UpdateFirmwareStatusEnum::RevokedCertificate:
        return ocpp::v16::UpdateFirmwareStatusEnumType::RevokedCertificate;
    case ocpp::v2::UpdateFirmwareStatusEnum::Rejected:
    default:
        return ocpp::v16::UpdateFirmwareStatusEnumType::Rejected;
    }
}

} // namespace ocpp_multi::v16_conversions
