// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/message_2/power_delivery.hpp>

#include "../states.hpp"

namespace iso15118::d2::state {

// An AC session has to close the contactor and wait for the confirmation before it may answer; a DC
// session waits for the contactor to open before it answers a stop, and ends into WeldingDetection
// rather than SessionStop.
// On DC the request is accepted inside PreCharge and DcChargeLoop and processed on the transition out,
// so the DC half is process_dc_power_delivery(), and only its stop branch waits, in DcPowerDeliveryStop.

// AC: PowerDelivery(Start) closes the contactor and the response waits for the confirmation.
struct AcPowerDelivery : public StateBase {
    AcPowerDelivery(Context& ctx) : StateBase(ctx, StateID::AcPowerDelivery) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    // PowerDelivery(Start) held back until the ClosedContactor control event arrives.
    std::optional<message_2::PowerDeliveryRequest> saved_start_req{std::nullopt};
};

// DC: the response to PowerDelivery(Stop) waits for the contactor to be reported open, so that side B is
// disabled before the EV learns of it [IEC 61851-23:2023 Table CC.9 t103 before t104]. Sent anyway after
// d20::TIMEOUT_CONTACTOR_OPEN_ISO2, and at once on an emergency shutdown.
struct DcPowerDeliveryStop : public StateBase {
    DcPowerDeliveryStop(Context& ctx, const message_2::PowerDeliveryResponse& res) :
        StateBase(ctx, StateID::DcPowerDeliveryStop), pending_res(res) {
    }

    void enter() final;
    Result on_event(Event) final;
    Result on_request(const message_2::Variant& received) final;

private:
    Result send_pending_res();

    message_2::PowerDeliveryResponse pending_res;
};

// The DC stop response goes out: arms the CP State B gate of the request that follows.
Result finish_dc_stop(Context& ctx, const message_2::PowerDeliveryResponse& res);

} // namespace iso15118::d2::state
