// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/session_stop.hpp>

namespace iso15118::d2::ev::state {

namespace dt = message_2::datatypes;

namespace session_stop {

message_2::SessionStopRequest create_request(dt::ChargingSession charging_session);

struct Result {
    bool valid{false};
};

Result handle_response(const message_2::SessionStopResponse& res);

} // namespace session_stop

} // namespace iso15118::d2::ev::state
