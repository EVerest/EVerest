// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include "../states.hpp"
#include <iso15118/message/power_delivery.hpp>

namespace iso15118::ev::d20::state {

struct PowerDelivery : public StateBase {
public:
    PowerDelivery(Context& ctx, message_20::datatypes::Progress charge_progress) :
        StateBase(ctx, StateID::PowerDelivery), m_charge_progress(charge_progress) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    message_20::datatypes::Progress m_charge_progress;
};

} // namespace iso15118::ev::d20::state
