// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/welding_detection.hpp>

namespace iso15118::ev::d2::state {

namespace dt = message_2::datatypes;

namespace welding_detection {

// Cycle backstop before the EV moves on.
constexpr int CYCLES = 3;

// Early exit once the EVSE-reported present voltage drops below this.
constexpr float SAFE_VOLTAGE_V = 60.0f;

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status);

bool should_finish(int cycles, float present_voltage);

} // namespace welding_detection

} // namespace iso15118::ev::d2::state
