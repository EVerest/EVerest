// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::state {

struct ContractAuthentication : public StateBase {
    ContractAuthentication(Context& ctx) : StateBase(ctx, StateID::ContractAuthentication) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    bool auth_requested{false};
    // Local to this state: ContractAuthentication is entered once per session and never re-entered.
    bool authorized{false};
    // "pending" (no AuthorizationResponse control event yet) vs "rejected" (AuthorizationResponse{false}).
    bool auth_response_received{false};
    // The Ongoing window elapsed without a result; the next request is answered with FAILED.
    bool timeout_ongoing_reached{false};
};

} // namespace iso15118::din::state
