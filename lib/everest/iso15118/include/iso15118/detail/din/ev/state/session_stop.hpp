// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/session_stop.hpp>

namespace iso15118::din::ev::state {

namespace dt = message_din::datatypes;

namespace session_stop {

message_din::SessionStopRequest create_request();

struct Result {
    bool valid{false};
};

Result handle_response(const message_din::SessionStopResponse& res);

} // namespace session_stop

} // namespace iso15118::din::ev::state
