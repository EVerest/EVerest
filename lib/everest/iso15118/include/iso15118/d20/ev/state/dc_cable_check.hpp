// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// Ongoing polling loop: resend DC_CableCheckReq while the SECC replies Ongoing, guarded by
// V2G_EVCC_CableCheck_Timeout. Finished -> DC_PreCharge.
struct DC_CableCheck : public StateBase {
    DC_CableCheck(Context& ctx) : StateBase(ctx, StateID::DC_CableCheck) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_request{true};
    bool ongoing_timeout_reached{false};
    // Set while the first DC_CableCheckReq is held back until the module reports CP state C/D (only
    // with session_config.has_cp_state_feedback). Cleared when the request is finally sent.
    bool waiting_for_cp_state{false};

    void send(Event ev);
};

} // namespace iso15118::d20::ev::state
