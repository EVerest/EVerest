// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

// ISO 15118-2 Plug-and-Charge PaymentDetails: parse the ContractSignatureCertChain, validate it to the
// trusted MO/V2G root, extract the contract public key + eMAID, generate a 16-byte GenChallenge and
// answer PaymentDetailsRes. Only reached when Contract was selected in PaymentServiceSelection.
struct PaymentDetails : public StateBase {
    PaymentDetails(Context& ctx) : StateBase(ctx, StateID::PaymentDetails) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::state
