// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/session_stop.hpp>

namespace iso15118::ev::din::state {

namespace session_stop {

// DIN 70121 carries no reason on the wire.
message_din::SessionStopRequest create_request();

} // namespace session_stop

} // namespace iso15118::ev::din::state
