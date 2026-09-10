// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// Isolation test poll: resend CableCheckReq (EVReady=true) until EVSEProcessing=Finished, then
// advance to PreCharge. [V2G-DC-547]: the first request is held until the EV applied CP state C or D.
struct CableCheck : public StateBase {
    CableCheck(Context& ctx) : StateBase(ctx, StateID::CableCheck) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool request_sent{false};
};

} // namespace iso15118::ev::din::state
