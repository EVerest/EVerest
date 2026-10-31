// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message/session_stop.hpp>

#include "../states.hpp"

namespace iso15118::ev::d20::state {

struct SessionStop : public StateBase {
public:
    SessionStop(Context& ctx, session::StopReason reason) : StateBase(ctx, StateID::SessionStop), m_reason(reason) {
    }

    static message_20::SessionStopRequest make_request(session::StopReason reason);

    Result feed(Event) final;

private:
    session::StopReason m_reason;
};

// Sends the SessionStopReq for `reason`, raised to an EV stop latched since, and creates the
// SessionStop state awaiting it.
BasePointerType to_session_stop(Context& ctx, session::StopReason reason);

} // namespace iso15118::ev::d20::state
