// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

// EVCC timeouts for DIN SPEC 70121 (Josev shared/messages/din_spec/timeouts.py, Table 75/77).
namespace iso15118::din::ev {

// Per-message response timeout (armed when a request is sent, stopped on response). 2000 ms for all
// request messages except CurrentDemandReq.
constexpr uint32_t MESSAGE_TIMEOUT_MS = 2000;
constexpr uint32_t MESSAGE_TIMEOUT_CURRENT_DEMAND_MS = 500;

// Guard timeouts for the "Ongoing" polling loops (V2G_SECC_SequenceTimeout guards the EVSEProcessing
// polls, V2G_EVCC_* guard the isolation / pre-charge ramps).
constexpr uint32_t TIMEOUT_CONTRACT_AUTHENTICATION_ONGOING_MS = 60000;
constexpr uint32_t TIMEOUT_CHARGE_PARAMETER_ONGOING_MS = 60000;
constexpr uint32_t TIMEOUT_CABLE_CHECK_MS = 40000;
constexpr uint32_t TIMEOUT_PRE_CHARGE_MS = 10000; // DIN Table 77 specifies 10 s (V2G_EVCC_PreCharge_Timeout)
constexpr uint32_t TIMEOUT_WELDING_DETECTION_MS = 60000;

} // namespace iso15118::din::ev
