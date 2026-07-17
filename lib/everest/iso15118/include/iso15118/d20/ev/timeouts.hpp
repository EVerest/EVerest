// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

// EVCC timeouts, see flow spec §5.
namespace iso15118::d20::ev {

// Per-message response timeout (armed when a request is sent, stopped on response).
constexpr uint32_t MESSAGE_TIMEOUT_MS = 2000;
constexpr uint32_t MESSAGE_TIMEOUT_SERVICE_DETAIL_MS = 5000;
constexpr uint32_t MESSAGE_TIMEOUT_CHARGE_LOOP_MS = 500;

// Guard timeouts for the "Ongoing" polling loops.
constexpr uint32_t TIMEOUT_AUTHORIZATION_ONGOING_MS = 60000;
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 40000;
constexpr uint32_t TIMEOUT_PRE_CHARGE_MS = 10000;
constexpr uint32_t TIMEOUT_WELDING_DETECTION_MS = 60000;

// V2G_EVCC_CommunicationSetup_Timeout.
constexpr uint32_t COMMUNICATION_SETUP_TIMEOUT_MS = 20000;

// SDP request resend interval.
constexpr uint32_t SDP_RESEND_INTERVAL_MS = 250;

// Minimum interval between two consecutive request messages (paces the Ongoing resend loops).
constexpr uint32_t MIN_REQUEST_INTERVAL_MS = 100;

} // namespace iso15118::d20::ev
