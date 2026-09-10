// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/ac_charge_loop.hpp>

#include <iso15118/detail/d2/state/power_delivery.hpp>

#include <iso15118/d2/state/metering_receipt.hpp>
#include <iso15118/detail/d2/state/charging_status.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ChargingStatusResponse handle_request([[maybe_unused]] const message_2::ChargingStatusRequest& req,
                                                 const dt::SessionId& session_id, const d2::SessionConfig& config,
                                                 uint8_t sa_schedule_tuple_id, bool charger_stop, bool request_receipt,
                                                 const std::optional<dt::MeterInfo>& meter_info) {
    message_2::ChargingStatusResponse res;
    res.header.session_id = session_id;
    res.response_code = dt::ResponseCode::OK;

    res.evse_id = config.evse_id;
    res.sa_schedule_tuple_id = sa_schedule_tuple_id;
    res.evse_max_current = dt::to_physical_value(config.ac_max_current, dt::Unit::A);

    res.ac_evse_status = make_ac_evse_status();
    res.ac_evse_status.notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;

    // [V2G2-691] field always present. PnC: request a signed MeteringReceipt when configured; EIM false.
    res.receipt_required = request_receipt;

    // MeterInfo is an independent optional element here, not a receipt payload: [Table 104] marks it "O"
    // for the SECC in both AC Message Sets, so an EIM session is entitled to a reading too -- unlike the
    // DC loop. Report every reading the module forwarded (EvseV2G parity).
    res.meter_info = meter_info;

    return res;
}

// Returns a transition only when the response asked for a signed receipt: the receipt is then the
// only request still in sequence [V2G2-577]. Shared by both AC charge-loop nodes.
namespace {
Result answer_charging_status(Context& ctx, const message_2::ChargingStatusRequest& req) {
    // No CHARGE_LOOP_STARTED here: current_demand_started is DC-only, and EvseManager subscribes to it
    // only in DC mode, where it arms the over-voltage monitor.

    // Withheld until a meter reading exists: [V2G2-902] has the EV sign the MeterInfo of this response.
    const bool request_receipt = ctx.session_config.receipt_required and ctx.session().contract_selected and
                                 not ctx.session().receipt_received and ctx.evse().latest_meter_info.has_value();
    auto res = handle_request(req, ctx.get_session_id(), ctx.session_config, ctx.session().sa_schedule_tuple_id,
                              ctx.evse().charger_stop_requested, request_receipt, ctx.evse().latest_meter_info);
    // A module-reported RCD error is surfaced via the AC EVSE status RCD flag (mirrors EvseV2G send_error).
    res.ac_evse_status.rcd = (ctx.evse().active_error == d20::EvseErrorCode::RCD);
    ctx.respond(res);

    if (request_receipt) {
        return ctx.create_state<MeteringReceipt>(false);
    }
    return {};
}
} // namespace

void AcChargeLoopStart::enter() {
    logf_debug("Enter state: AcChargeLoopStart");
}

Result AcChargeLoopStart::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        return {};
    }

    return {};
}

Result AcChargeLoopStart::on_request(const message_2::Variant& received) {
    // A PowerDeliveryReq is not in sequence until the EV has reported status once [V2G2-576].
    const auto type = received.get_type();
    if (type == message_2::Type::ChargingStatusReq) {
        auto result = answer_charging_status(m_ctx, received.get<message_2::ChargingStatusRequest>());
        if (result.new_state != nullptr) {
            return result;
        }
        return m_ctx.create_state<AcChargeLoop>();
    } else {
        logf_warning("Expected ChargingStatusReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

void AcChargeLoop::enter() {
    logf_debug("Enter state: AcChargeLoop");
}

Result AcChargeLoop::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        return {};
    }

    return {};
}

Result AcChargeLoop::on_request(const message_2::Variant& received) {
    const auto type = received.get_type();
    if (type == message_2::Type::ChargingStatusReq) {
        return answer_charging_status(m_ctx, received.get<message_2::ChargingStatusRequest>());
    } else if (type == message_2::Type::PowerDeliveryReq) {
        return process_ac_power_delivery(m_ctx, received.get<message_2::PowerDeliveryRequest>());
    } else {
        logf_warning("Expected ChargingStatusReq or PowerDeliveryReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

} // namespace iso15118::d2::state
