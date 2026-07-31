// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/dc_cable_check.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace dc_cable_check {

message_20::DC_CableCheckRequest create_request();

struct Result {
    bool valid{false};
    bool finished{false};
};

Result handle_response(const message_20::DC_CableCheckResponse& res);

} // namespace dc_cable_check

} // namespace iso15118::d20::ev::state
