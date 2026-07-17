// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/dc_welding_detection.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace dc_welding_detection {

message_20::DC_WeldingDetectionRequest create_request(dt::Processing processing);

struct Result {
    bool valid{false};
};

Result handle_response(const message_20::DC_WeldingDetectionResponse& res);

} // namespace dc_welding_detection

} // namespace iso15118::d20::ev::state
