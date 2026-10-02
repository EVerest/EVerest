// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>

// Value datatypes that are byte-identical between ISO 15118-2 (message_2) and DIN SPEC 70121
// (message_din). Defined once here and aliased into each protocol's datatypes namespace (mirrors the
// d20::ev::ControlEvent reuse precedent). ISO 15118-20 (message_20) deliberately keeps its own,
// differently-valued datatypes and is not affected by this header.
namespace iso15118::shared_datatypes {

static constexpr auto SESSION_ID_LENGTH = 8;
using SessionId = std::array<uint8_t, SESSION_ID_LENGTH>;

enum class PaymentOption {
    Contract = 0,
    ExternalPayment = 1,
};

enum class ServiceCategory {
    EVCharging = 0,
    Internet = 1,
    ContractCertificate = 2,
    OtherCustom = 3,
};

enum class EnergyTransferMode {
    AC_single_phase_core = 0,
    AC_three_phase_core = 1,
    DC_core = 2,
    DC_extended = 3,
    DC_combo_core = 4,
    DC_unique = 5,
};

// ISO 15118-2 spells this DC_EVErrorCode; DIN SPEC 70121 spells it DcEvErrorCode. Same enumerators.
enum class DcEvErrorCode {
    NO_ERROR = 0,
    FAILED_RESSTemperatureInhibit = 1,
    FAILED_EVShiftPosition = 2,
    FAILED_ChargerConnectorLockFault = 3,
    FAILED_EVRESSMalfunction = 4,
    FAILED_ChargingCurrentdifferential = 5,
    FAILED_ChargingVoltageOutOfRange = 6,
    Reserved_A = 7,
    Reserved_B = 8,
    Reserved_C = 9,
    FAILED_ChargingSystemIncompatibility = 10,
    NoData = 11,
};

// ISO 15118-2 spells this EVSENotification; DIN SPEC 70121 spells it EvseNotification. Same enumerators.
enum class EvseNotification {
    None = 0,
    StopCharging = 1,
    ReNegotiation = 2,
};

} // namespace iso15118::shared_datatypes
