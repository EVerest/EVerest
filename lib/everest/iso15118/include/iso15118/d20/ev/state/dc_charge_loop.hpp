// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message/common_types.hpp>

#include "../states.hpp"

namespace iso15118::d20::ev::state {

// DC charging loop. Sends Dynamic/Scheduled (+ BPT) requests, paced by the session driver. On an
// EvseNotification Terminate/Pause (or a stop/pause control event) it diverts to PowerDelivery(Stop).
struct DC_ChargeLoop : public StateBase {
    DC_ChargeLoop(Context& ctx) : StateBase(ctx, StateID::DC_ChargeLoop) {
    }

    void enter() final;
    Result feed(Event) final;

private:
    bool first_response{true};
    bool stop_requested{false};
    message_20::datatypes::ChargingSession stop_reason{message_20::datatypes::ChargingSession::Terminate};

    void send(Event ev);
};

} // namespace iso15118::d20::ev::state
