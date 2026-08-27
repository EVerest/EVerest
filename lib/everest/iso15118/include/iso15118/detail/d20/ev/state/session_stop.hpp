// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/session_stop.hpp>

namespace iso15118::d20::ev::state {

namespace dt = message_20::datatypes;

namespace session_stop {

message_20::SessionStopRequest create_request(dt::ChargingSession charging_session);

struct Result {
    bool valid{false};
};

Result handle_response(const message_20::SessionStopResponse& res);

} // namespace session_stop

} // namespace iso15118::d20::ev::state
