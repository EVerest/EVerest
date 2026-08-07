// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

struct Authorization : public StateBase {
    Authorization(Context& ctx) : StateBase(ctx, StateID::Authorization) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_req_msg{true};
    bool timeout_ongoing_reached{false};
    // The module distinguishes "pending" (no AuthorizationResponse control event yet) from "rejected"
    // (an AuthorizationResponse{false} arrived) - mirrors the d20 SECC AuthStatus tri-state.
    bool auth_response_received{false};
};

} // namespace iso15118::d2::state
