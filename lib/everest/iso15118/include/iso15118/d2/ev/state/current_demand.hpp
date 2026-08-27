// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// DC CurrentDemand loop (paced by the session driver, 500 ms). On an EVSENotification StopCharging
// (or a stop/pause control event) it diverts to PowerDelivery(Stop).
struct CurrentDemand : public StateBase {
    CurrentDemand(Context& ctx) : StateBase(ctx, StateID::CurrentDemand) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_response{true};
    bool stop_requested{false};
    message_2::datatypes::ChargingSession stop_reason{message_2::datatypes::ChargingSession::Terminate};

    void send(Event ev);
};

} // namespace iso15118::d2::ev::state
