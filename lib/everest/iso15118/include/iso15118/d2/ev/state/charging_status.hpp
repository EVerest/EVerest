// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_2/common_types.hpp>

#include "../states.hpp"

namespace iso15118::d2::ev::state {

// AC ChargingStatus loop (paced by the session driver, 2000 ms). Publishes ac_evse_target_power on
// every response. On an AC_EVSEStatus EVSENotification StopCharging (or a stop/pause control event) it
// diverts to PowerDelivery(Stop). ReceiptRequired is ignored (EIM).
struct ChargingStatus : public StateBase {
    ChargingStatus(Context& ctx) : StateBase(ctx, StateID::ChargingStatus) {
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
