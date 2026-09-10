// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

namespace iso15118::din::state {

// The DIN SPEC 70121 Table 77 timers the SECC arms, in one place so they can be reviewed together.

// V2G_SECC_CPState_Detection_Timeout: how long the SECC waits to detect CP State C/D after a
// CableCheckReq [V2G-DC-967], and CP State B after PowerDelivery(off) [V2G-DC-988]/[V2G-DC-556].
constexpr uint32_t TIMEOUT_CPSTATE_DETECTION_MS = 1500;

// V2G_SECC_PowerDelivery_Timeout: armed on the first PreChargeReq [V2G-DC-969], released once the
// PowerDeliveryReq is answered (process_power_delivery).
constexpr uint32_t TIMEOUT_POWER_DELIVERY_MS = 20000;

// V2G_SECC_WeldingDetection_Timeout: bounds the welding-detection loop [V2G-DC-925]/[V2G-DC-926].
constexpr uint32_t TIMEOUT_WELDING_DETECTION_MS = 20000;

// A full day [V2G-DC-556], except while pausing for lack of energy, where the EV is told to come
// back rather than to plan a day of charging (EvseV2G din_server.cpp PAUSE_DURATION).
constexpr uint32_t DIN_SA_SCHEDULE_DURATION = 86400;
constexpr uint32_t DIN_PAUSE_DURATION = 60 * 30;

// Section 9.7.4.2.4 does not list SessionStopReq among the requests admitted in the charge loop:
// [V2G-DC-462] admits a CurrentDemandReq alone, and [V2G-DC-465] a CurrentDemandReq or a
// PowerDeliveryReq. Nine of the twelve wait nodes do admit it, and the only other one that does not
// is SessionSetup, where no session exists yet to stop -- so we read the omission as an oversight,
// and a strict reading would force an aborting EV to send PowerDelivery(FALSE) first merely to
// unlock a stop it has already requested. Undefine for the strict reading: the charge loop then
// answers FAILED_SequenceError per [V2G-DC-666]. Kept as a switch because a conformance test
// written to the letter of the specification will fail with it defined.
#define DIN_ACCEPT_SESSION_STOP_IN_CHARGE_LOOP 1

} // namespace iso15118::din::state
