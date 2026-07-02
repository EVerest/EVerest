// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/state/dc_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/dc_charge_parameter_discovery.hpp>

namespace iso15118::ev::d20::state {

namespace {

using ResponseCode = message_20::datatypes::ResponseCode;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
        return true;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

} // namespace

void DC_ChargeParameterDiscovery::enter() {
    m_ctx.log.enter_state("DC_ChargeParameterDiscovery");

    const auto p = m_ctx.get_dc_params();

    message_20::datatypes::DC_CPDReqEnergyTransferMode mode{};
    mode.max_charge_power = message_20::datatypes::from_float(p.max_charge_power);
    mode.min_charge_power = message_20::datatypes::RationalNumber{0, 0};
    mode.max_charge_current = message_20::datatypes::from_float(p.max_charge_current);
    mode.min_charge_current = message_20::datatypes::RationalNumber{0, 0};
    mode.max_voltage = message_20::datatypes::from_float(p.max_voltage);
    mode.min_voltage = message_20::datatypes::from_float(p.min_voltage);
    mode.target_soc = std::nullopt;

    message_20::DC_ChargeParameterDiscoveryRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.transfer_mode = mode;
    m_ctx.respond(req);
}

Result DC_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto res = variant->get_if<message_20::DC_ChargeParameterDiscoveryResponse>();
    if (res == nullptr) {
        logf_error("Expected DC_ChargeParameterDiscoveryResponse, got code type id: %d",
                   static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("DC_ChargeParameterDiscoveryResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("DC_ChargeParameterDiscoveryResponse rejected with response_code: %d",
                   static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    return m_ctx.create_state<ScheduleExchange>();
}

} // namespace iso15118::ev::d20::state
