// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::state {

struct ContractAuthentication : public StateBase {
    ContractAuthentication(Context& ctx) : StateBase(ctx, StateID::ContractAuthentication) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool auth_requested{false};
    // "pending" (no AuthorizationResponse control event yet) vs "rejected" (AuthorizationResponse{false}).
    bool auth_response_received{false};
};

} // namespace iso15118::din::state
