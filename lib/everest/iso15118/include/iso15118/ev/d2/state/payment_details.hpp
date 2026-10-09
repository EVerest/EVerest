// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/d2/context.hpp>
#include <iso15118/ev/d2/states.hpp>

namespace iso15118::ev::d2::state {

// Plug & Charge: presents the contract chain + eMAID and keeps the GenChallenge for the signed
// AuthorizationReq.
struct PaymentDetails : public StateBase {
    PaymentDetails(Context& ctx) : StateBase(ctx, StateID::PaymentDetails) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::d2::state
