// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include "generic_chargepoint_interface.hpp"

// Conversions between OCPP 1.6 and OCPP 2.x types. OCPPmulti uses the v2 types as the common
// vocabulary across GenericChargePointInterface, so the v16 adapter translates in both directions.
namespace ocpp_multi::v16_conversions {

ocpp::v2::AuthorizationStatusEnum convert(ocpp::v16::AuthorizationStatus value);
ocpp::v2::ChangeAvailabilityStatusEnum convert(ocpp::v16::AvailabilityStatus value);
ocpp::v2::BootNotificationResponse convert(const ocpp::v16::BootNotificationResponse& value);
ocpp::v16::BootReasonEnum convert(ocpp::v2::BootReasonEnum value);
ocpp::v16::ChargingRateUnit convert(ocpp::v2::ChargingRateUnitEnum value);
ocpp::v2::ChargingRateUnitEnum convert(ocpp::v16::ChargingRateUnit value);
ocpp::v2::DataTransferRequest convert(const ocpp::v16::DataTransferRequest& value);
ocpp::v16::DataTransferResponse convert(const ocpp::v2::DataTransferResponse& value);
ocpp::v2::DataTransferStatusEnum convert(ocpp::v16::DataTransferStatus value);
ocpp::v2::EnhancedChargingSchedulePeriod convert(const ocpp::v16::EnhancedChargingSchedulePeriod& value);
ocpp::v2::EnhancedCompositeSchedule convert(std::int32_t evse_id, const ocpp::v16::EnhancedChargingSchedule& value);
ocpp::FirmwareStatusNotification convert(ocpp::v2::FirmwareStatusEnum value);
ocpp::v16::GetLogResponse convert(const ocpp::v2::GetLogResponse& value);
ocpp::v2::LogEnum convert(ocpp::v16::LogEnumType value);
ocpp::v16::AvailabilityType convert(ocpp::v2::OperationalStatusEnum value);
ocpp::v16::ReservationStatus convert(ocpp::v2::ReserveNowStatusEnum value);
GenericChargePointCallbacks::ResetType convert(ocpp::v16::ResetType value);
ocpp::v16::DataTransferResponse convert(const ocpp::v2::SetDisplayMessageResponse& value);
ocpp::v16::UnlockStatus convert(const ocpp::v2::UnlockConnectorResponse& value);
ocpp::v2::UpdateFirmwareRequest convert(const ocpp::v16::UpdateFirmwareRequest& value);
ocpp::v2::UpdateFirmwareRequest convert(const ocpp::v16::SignedUpdateFirmwareRequest& value);
ocpp::v16::UpdateFirmwareStatusEnumType convert(const ocpp::v2::UpdateFirmwareResponse& value);

} // namespace ocpp_multi::v16_conversions
