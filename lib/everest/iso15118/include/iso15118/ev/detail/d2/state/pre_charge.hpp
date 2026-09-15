// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/pre_charge.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace pre_charge {

// Absolute voltage tolerance applied on top of the +/- 10 % band, so a large target does not admit a
// wide absolute error.
constexpr float ABS_VOLTAGE_TOLERANCE_V = 20.0f;

// Target current is 0 during pre-charge; the request's present voltage is informational.
message_2::PreChargeRequest create_request(const dt::DC_EVStatus& dc_ev_status, float target_voltage);

// True when the EVSE-reported present voltage is inside both the +/- 10 % band and the absolute cap.
// A non-positive target voltage never converges.
bool converged(const message_2::PreChargeResponse& res, float target_voltage);

} // namespace pre_charge

} // namespace iso15118::ev::d2::state
