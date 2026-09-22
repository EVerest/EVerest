// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>
#include <iso15118/ev/session/stop_reason.hpp>

namespace iso15118::ev::din::state {

struct SessionStop : public StateBase {
    SessionStop(Context& ctx, session::StopReason reason) : StateBase(ctx, StateID::SessionStop), m_reason(reason) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    session::StopReason m_reason;
};

// Sends the SessionStopReq, with `reason` raised to an EV stop latched since, and creates the SessionStop
// state awaiting it.
BasePointerType to_session_stop(Context& ctx, session::StopReason reason);

} // namespace iso15118::ev::din::state
