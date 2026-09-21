// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/states.hpp>
#include <iso15118/message_din/session_stop.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

message_din::SessionStopResponse handle_request([[maybe_unused]] const message_din::SessionStopRequest& req,
                                                const dt::SessionId& session_id);

// Answers a SessionStopReq and terminates the session in place; never transitions, because section
// 9.7.4.2.4 admits the request in nine of its twelve wait nodes. It releases the ONGOING and CPSTATE
// slots itself: the session ends here, so leave() never runs and a timer left armed would fire into
// the closing session.
Result process_session_stop(Context& ctx, [[maybe_unused]] const message_din::SessionStopRequest& req);

} // namespace iso15118::din::state
