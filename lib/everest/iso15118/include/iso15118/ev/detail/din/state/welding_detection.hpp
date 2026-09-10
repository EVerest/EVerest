// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/welding_detection.hpp>

namespace iso15118::ev::din::state {

namespace dt = message_din::datatypes;

namespace welding_detection {

// DIN WeldingDetectionReq has no processing field: the loop ends on a safe voltage, with a
// cycle-count backstop (Josev parity).
constexpr int WELDING_DETECTION_CYCLES = 3;
// Below this EVSE present voltage the DC link is considered safe.
constexpr float WELDING_DETECTION_SAFE_VOLTAGE_V = 60.0f;

message_din::WeldingDetectionRequest create_request(const dt::DcEvStatus& dc_ev_status);

} // namespace welding_detection

} // namespace iso15118::ev::din::state
