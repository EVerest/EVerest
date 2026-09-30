// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d2/states.hpp>

#include <iso15118/message_2/common_types.hpp>
#include <iso15118/message_2/session_stop.hpp>

namespace iso15118::d2::state {

// The caller has satisfied the [V2G2-920] CP State B gate, so this never waits.
Result process_session_stop(Context& ctx, const message_2::SessionStopRequest& req);

namespace dt = message_2::datatypes;

message_2::SessionStopResponse handle_request(const message_2::SessionStopRequest& req,
                                              const dt::SessionId& session_id);

} // namespace iso15118::d2::state
