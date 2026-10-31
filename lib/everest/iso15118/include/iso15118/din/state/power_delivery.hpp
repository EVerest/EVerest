// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/message_din/power_delivery.hpp>

#include "../states.hpp"

namespace iso15118::din::state {

// The response to PowerDelivery(ReadyToChargeState false) waits for the contactor to be reported open, so
// that side B is disabled before the EV learns of it [IEC 61851-23:2023 Table CC.9 t103 before t104]. Sent
// anyway after d20::TIMEOUT_CONTACTOR_OPEN, and at once on an emergency shutdown. PowerDelivery itself is
// an action (see detail/din/state/power_delivery.hpp); only its stop branch waits here.
struct PowerDeliveryStop : public StateBase {
    PowerDeliveryStop(Context& ctx, const message_din::PowerDeliveryResponse& res) :
        StateBase(ctx, StateID::PowerDeliveryStop), pending_res(res) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_din::Variant& received) final;

private:
    Result send_pending_res();

    message_din::PowerDeliveryResponse pending_res;
};

// The stop response goes out: welding detection follows.
Result finish_stop(Context& ctx, const message_din::PowerDeliveryResponse& res);

} // namespace iso15118::din::state
