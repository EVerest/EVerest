// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <iso15118/din/states.hpp>
#include <iso15118/message_din/power_delivery.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

// Returns the transition: CurrentDemand on ReadyToChargeState=TRUE [V2G-DC-462], PowerDeliveryStop on
// FALSE [V2G-DC-459].
//
// This is an action, not a wait state: section 9.7.4.2.4 never has the SECC waiting for a
// PowerDeliveryReq alone, so the pre-charge node and the charge loop both dispatch to it.
Result process_power_delivery(Context& ctx, const message_din::PowerDeliveryRequest& req);

// error_status_code becomes the EVSEStatusCode [V2G-DC-638] and makes a ReadyToChargeState request
// fail with FAILED_PowerDeliveryNotApplied [V2G-DC-401].
message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id, bool charger_stop = false,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code = std::nullopt);

// The response to PowerDelivery(ReadyToChargeState=FALSE) waits for DcOutputDisabled, so that side B is
// disabled and the present values in the WeldingDetectionRes were measured after the switch off
// [IEC 61851-23:2023 Table CC.9 t103 before t104]. Sent anyway after d20::TIMEOUT_DC_OUTPUT_DISABLED_DIN.
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

} // namespace iso15118::din::state
