// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/d20/context.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/message/session_stop.hpp>

namespace iso15118::d20::state {

message_20::SessionStopResponse handle_request(const message_20::SessionStopRequest& req, const d20::Session& session);

// Records how the SessionStopRes just staged ends the session (Terminate / Pause / FAILED), so the
// session layer can release the data link with the matching D-LINK signal once the response hit the
// wire, and reports the EV's termination code and explanation. Every state that answers a
// SessionStopReq must call this right after ctx.respond(res) - not only the SessionStop state: the
// regular DC end arrives in DC_WeldingDetection, the regular AC end in the charge-parameter /
// schedule states, and so on.
void mark_session_stop_response(d20::Context& ctx, const message_20::SessionStopRequest& req,
                                const message_20::SessionStopResponse& res);

// Ends or pauses the session as the SessionStopRes just staged says, mark_session_stop_response included.
// A positive Pause keeps the pause context, with the selected services and the authorization a resumed
// session restarts from; a FAILED Res or a Terminate drops it. For the states after the services are
// selected, where a pause can be resumed; the earlier states end the session for any SessionStopReq.
void apply_session_stop_response(d20::Context& ctx, const message_20::SessionStopRequest& req,
                                 const message_20::SessionStopResponse& res);

} // namespace iso15118::d20::state
