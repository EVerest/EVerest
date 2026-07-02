// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_charge_loop.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

using ResponseCode = dt::ResponseCode;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
        return true;
    case ResponseCode::FAILED:
    case ResponseCode::FAILED_SequenceError:
    case ResponseCode::FAILED_UnknownSession:
        return false;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

message_20::DC_ChargeLoopRequest make_request(const SessionId& session, const DcChargeParams& params) {
    message_20::DC_ChargeLoopRequest req;
    setup_header(req.header, session);
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;
    req.present_voltage = dt::from_float(params.present_voltage);

    dt::Dynamic_DC_CLReqControlMode mode;
    mode.departure_time = std::nullopt;
    mode.target_energy_request = dt::from_float(params.energy_capacity);
    mode.max_energy_request = dt::from_float(params.energy_capacity);
    mode.min_energy_request = {0, 0};
    mode.max_charge_power = dt::from_float(params.max_charge_power);
    mode.min_charge_power = {0, 0};
    mode.max_charge_current = dt::from_float(params.max_charge_current);
    mode.max_voltage = dt::from_float(params.max_voltage);
    mode.min_voltage = dt::from_float(params.min_voltage);
    req.control_mode = mode;

    return req;
}

} // namespace

void DC_ChargeLoop::enter() {
    m_ctx.log.enter_state("DC_ChargeLoop");
    m_ctx.respond(make_request(m_ctx.get_session(), m_ctx.get_dc_params()));
}

Result DC_ChargeLoop::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* stop = m_ctx.get_control_event<StopCharging>(); stop != nullptr and *stop) {
            m_ctx.set_stop_charging_requested(true);
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto res = variant->get_if<message_20::DC_ChargeLoopResponse>();
    if (res == nullptr) {
        logf_error("Expected DC_ChargeLoopResponse, got code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("DC_ChargeLoopResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("DC_ChargeLoopResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->status.has_value() and res->status->notification == dt::EvseNotification::Terminate) {
        m_ctx.feedback.stop_from_charger();
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    if (m_ctx.is_stop_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    m_ctx.respond(make_request(m_ctx.get_session(), m_ctx.get_dc_params()));
    return {};
}

} // namespace iso15118::ev::d20::state
