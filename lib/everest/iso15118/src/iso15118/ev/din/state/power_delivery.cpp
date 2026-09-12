// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/power_delivery.hpp>

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

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery (DIN 70121)");
    const auto params = m_ctx.get_dc_params();
    m_ctx.send_request(power_delivery::create_request(m_phase == Phase::Start, make_dc_ev_status(params, true),
                                                      charging_complete(params)));
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

    if (m_phase == Phase::Start) {
        return m_ctx.create_state<CurrentDemand>();
    }
    return m_ctx.create_state<WeldingDetection>();
}

} // namespace iso15118::ev::din::state
