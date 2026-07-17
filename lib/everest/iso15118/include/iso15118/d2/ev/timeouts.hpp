// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

// EVCC ISO 15118-2 timeouts (Josev iso15118_2/timeouts.py + ISO 15118-2 Table 109).
namespace iso15118::d2::ev {

// Per-message response timeouts (armed when a request is sent, stopped on response).
constexpr uint32_t MESSAGE_TIMEOUT_MS = 2000;
constexpr uint32_t MESSAGE_TIMEOUT_SERVICE_DETAIL_MS = 5000;
constexpr uint32_t MESSAGE_TIMEOUT_POWER_DELIVERY_MS = 5000;
constexpr uint32_t MESSAGE_TIMEOUT_CURRENT_DEMAND_MS = 250; // ISO 15118-2 Table 109
constexpr uint32_t MESSAGE_TIMEOUT_CHARGING_STATUS_MS = 2000;

// Guard timeouts for the "Ongoing" polling loops.
constexpr uint32_t TIMEOUT_ONGOING_MS = 60000;          // V2G_EVCC_Ongoing_Timeout (Authorization/CPD)
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 40000;      // V2G_EVCC_CableCheck_Timeout
constexpr uint32_t TIMEOUT_PRE_CHARGE_MS = 50000;       // V2G_EVCC_PreCharge_Timeout
constexpr uint32_t TIMEOUT_WELDING_DETECTION_MS = 20000;

// Minimum interval between two consecutive request messages (paces the Ongoing resend loops).
constexpr uint32_t MIN_REQUEST_INTERVAL_MS = 100;

// Absolute voltage tolerance for PreCharge convergence, applied on top of the +/- 10 % band.
constexpr float PRE_CHARGE_ABS_VOLTAGE_TOLERANCE_V = 20.0f;

// Maximum number of WeldingDetection cycles (backstop) before the EV sends the terminating request.
constexpr int WELDING_DETECTION_CYCLES = 3;

// WeldingDetection exits early once the EVSE-reported present voltage drops below this safe threshold.
constexpr float WELDING_DETECTION_SAFE_VOLTAGE_V = 60.0f;

} // namespace iso15118::d2::ev
