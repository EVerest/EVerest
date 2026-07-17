// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/welding_detection.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace welding_detection {

message_din::WeldingDetectionRequest create_request(const dt::DcEvStatus& dc_ev_status);

struct Result {
    bool valid{false};
    float present_voltage{0.0f};
};

Result handle_response(const message_din::WeldingDetectionResponse& res);

} // namespace welding_detection

} // namespace iso15118::din::ev::state
