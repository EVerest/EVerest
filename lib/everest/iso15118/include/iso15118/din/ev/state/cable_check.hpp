// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

// Ongoing polling loop for the isolation test: resend CableCheckReq (EVReady=true) until
// EVSEProcessing=Finished, then verify the EVSE is ready with a valid isolation status and advance to
// PreCharge. Guarded by V2G_EVCC_CableCheck_Timeout.
struct CableCheck : public StateBase {
    CableCheck(Context& ctx) : StateBase(ctx, StateID::CableCheck) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};

    void send(Event ev);
};

} // namespace iso15118::din::ev::state
