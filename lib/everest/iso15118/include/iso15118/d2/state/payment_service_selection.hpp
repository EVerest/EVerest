// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::d2::state {

struct PaymentServiceSelection : public StateBase {
    PaymentServiceSelection(Context& ctx) : StateBase(ctx, StateID::PaymentServiceSelection) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::d2::state
