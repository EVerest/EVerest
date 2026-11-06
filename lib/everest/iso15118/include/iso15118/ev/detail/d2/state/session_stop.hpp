// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message_2/session_stop.hpp>

namespace iso15118::ev::d2::state {

namespace session_stop {

message_2::SessionStopRequest create_request(session::StopReason reason);

} // namespace session_stop

} // namespace iso15118::ev::d2::state
