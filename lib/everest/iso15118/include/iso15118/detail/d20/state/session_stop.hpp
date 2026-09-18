// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/session_stop.hpp>

namespace iso15118::d20::state {

message_20::SessionStopResponse handle_request(const message_20::SessionStopRequest& req, const d20::Session& session);

// Records how the SessionStopRes just staged ends the session (Terminate / Pause / FAILED), so the
// session layer can release the data link with the matching D-LINK signal once the response hit the
// wire. Every state that answers a SessionStopReq must call this right after ctx.respond(res) -
// not only the SessionStop state: the regular DC end arrives in DC_WeldingDetection, the regular AC
// end in the charge-parameter / schedule states, and so on.
void mark_session_stop_response(d20::Context& ctx, const message_20::SessionStopRequest& req,
                                const message_20::SessionStopResponse& res);

} // namespace iso15118::d20::state
