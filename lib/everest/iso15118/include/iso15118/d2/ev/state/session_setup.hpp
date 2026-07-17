// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Initial state. Sends SessionSetupReq (EVCCID = EV MAC). Accepts both OK_NewSessionEstablished and
// OK_OldSessionJoined, captures the SECC-assigned session id and reports the EVSE id.
struct SessionSetup : public StateBase {
    SessionSetup(Context& ctx) : StateBase(ctx, StateID::SessionSetup) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::ev::state
