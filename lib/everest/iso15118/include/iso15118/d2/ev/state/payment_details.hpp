// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// Plug-and-Charge: presents the contract certificate chain + eMAID to the SECC and consumes the
// GenChallenge returned in PaymentDetailsRes (echoed into the signed AuthorizationReq).
struct PaymentDetails : public StateBase {
    PaymentDetails(Context& ctx) : StateBase(ctx, StateID::PaymentDetails) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::ev::state
