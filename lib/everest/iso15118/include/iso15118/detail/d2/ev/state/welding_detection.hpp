// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/welding_detection.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace welding_detection {

message_2::WeldingDetectionRequest create_request(const dt::DC_EVStatus& dc_ev_status);

struct Result {
    bool valid{false};
    float present_voltage{0.0f};
};

Result handle_response(const message_2::WeldingDetectionResponse& res);

// WeldingDetection exits once the EVSE present voltage is below the safe threshold or the cycle
// backstop is reached.
bool should_finish_welding(int cycles, float present_voltage);

} // namespace welding_detection

} // namespace iso15118::d2::ev::state
