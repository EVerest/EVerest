// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"

namespace iso15118::din::ev::state {

struct ServicePaymentSelection : public StateBase {
    ServicePaymentSelection(Context& ctx) : StateBase(ctx, StateID::ServicePaymentSelection) {
    }

    void enter() final;
    Result feed(Event) final;
};

} // namespace iso15118::din::ev::state
