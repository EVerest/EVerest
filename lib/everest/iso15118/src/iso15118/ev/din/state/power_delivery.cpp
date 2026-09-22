// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/power_delivery.hpp>

#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/power_delivery.hpp>
#include <iso15118/ev/detail/din/state/state_helper.hpp>
#include <iso15118/ev/din/state/current_demand.hpp>
#include <iso15118/ev/din/state/welding_detection.hpp>

namespace iso15118::ev::din::state {

namespace power_delivery {

message_din::PowerDeliveryRequest create_request(bool ready_to_charge, const dt::DcEvStatus& dc_ev_status,
                                                 bool charging_complete) {
    message_din::PowerDeliveryRequest req;
    req.ready_to_charge_state = ready_to_charge;
    dt::DcEvPowerDeliveryParameter param;
    param.dc_ev_status = dc_ev_status;
    param.charging_complete = charging_complete;
    req.dc_ev_power_delivery_parameter = param;
    return req;
}

} // namespace power_delivery

namespace {

Result accepted(Context& ctx, PowerDelivery::Start) {
    return to_current_demand(ctx);
}

Result accepted(Context& ctx, const PowerDelivery::Stop& stop) {
    ctx.feedback.power_delivery_stopped();
    return to_welding_detection(ctx, stop.reason);
}

} // namespace

BasePointerType to_power_delivery(Context& ctx, PowerDelivery::Phase phase) {
    const auto params = ctx.get_dc_params();
    const bool ev_ready = not std::holds_alternative<PowerDelivery::Stop>(phase);
    ctx.send_request(
        power_delivery::create_request(ev_ready, make_dc_ev_status(params, ev_ready), charging_complete(params)));
    return ctx.create_state<PowerDelivery>(phase);
}

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery (DIN 70121)");
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::PowerDeliveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    return std::visit([this](const auto& phase) { return accepted(m_ctx, phase); }, m_phase);
}

} // namespace iso15118::ev::din::state
