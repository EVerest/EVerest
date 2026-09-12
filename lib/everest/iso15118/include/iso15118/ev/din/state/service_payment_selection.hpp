// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/states.hpp>

namespace iso15118::ev::din::state {

// EIM: selects ExternalPayment and the offered ChargeService, then advances to ContractAuthentication.
struct ServicePaymentSelection : public StateBase {
    ServicePaymentSelection(Context& ctx) : StateBase(ctx, StateID::ServicePaymentSelection) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::ev::din::state
