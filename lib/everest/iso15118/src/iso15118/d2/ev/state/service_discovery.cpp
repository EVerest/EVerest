// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/service_discovery.hpp>

#include <algorithm>

#include <iso15118/d2/ev/state/payment_service_selection.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/service_discovery.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

namespace service_discovery {

message_2::ServiceDiscoveryRequest create_request() {
    return {}; // service_scope/service_category omitted: request all offered services
}

Result handle_response(const message_2::ServiceDiscoveryResponse& res, dt::EnergyTransferMode requested_mode) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    result.charge_service_id = res.charge_service.service_id;

    const auto& modes = res.charge_service.supported_energy_transfer_mode;
    result.mode_supported = std::find(modes.begin(), modes.end(), requested_mode) != modes.end();

    const auto& options = res.payment_option_list;
    result.eim_offered = std::any_of(options.begin(), options.end(),
                                     [](dt::PaymentOption o) { return o == dt::PaymentOption::ExternalPayment; });
    result.contract_offered = std::any_of(options.begin(), options.end(),
                                          [](dt::PaymentOption o) { return o == dt::PaymentOption::Contract; });

    // The Certificate service (ServiceID 2, category ContractCertificate) is advertised in the optional
    // service_list; the EV selects it in PaymentServiceSelection when it wants to install/update a cert.
    if (res.service_list.has_value()) {
        const auto& services = res.service_list.value();
        result.certificate_service_offered = std::any_of(services.begin(), services.end(), [](const dt::Service& s) {
            return s.service_id == dt::CERTIFICATE_SERVICE_ID;
        });
    }

    return result;
}

} // namespace service_discovery

using namespace service_discovery;

void ServiceDiscovery::enter() {
    m_ctx.log.enter_state("ServiceDiscovery");
}

d2::ev::Result ServiceDiscovery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request();
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ServiceDiscovery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::ServiceDiscoveryResponse>()) {
        const auto requested_mode = m_ctx.session_config.requested_energy_transfer_mode;
        const auto result = handle_response(*res, requested_mode);

        if (not result.valid) {
            m_ctx.log("ServiceDiscovery failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        // A Plug-and-Charge (Contract) session is usable only when the EV prefers it and the SECC offers
        // it; otherwise the EV needs ExternalPayment (EIM).
        const bool want_contract = m_ctx.session_config.pnc.prefer_contract and result.contract_offered;
        if (not result.eim_offered and not want_contract) {
            m_ctx.log("SECC offers no usable payment option (EIM absent, Contract unavailable/unwanted), "
                      "terminating session");
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<SessionStop>();
        }

        if (not result.mode_supported) {
            // Proceed anyway (Josev parity): ChargeParameterDiscovery carries the requested mode and the
            // SECC rejects it with FAILED_WrongEnergyTransferMode ([V2G2-464]), which lets the EVSE side
            // surface the mismatch (ChargingParametersNotAccepted) instead of seeing a silent clean stop.
            m_ctx.log("SECC does not offer the requested energy transfer mode; continuing, expecting the SECC "
                      "to reject ChargeParameterDiscovery");
        }

        m_ctx.evse_info.selected_charge_service_id = result.charge_service_id;
        m_ctx.evse_info.contract_offered = result.contract_offered;
        m_ctx.evse_info.certificate_service_offered = result.certificate_service_offered;
        m_ctx.feedback.selected_protocol(is_ac_mode(requested_mode) ? "ISO15118-2:AC" : "ISO15118-2:DC");

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<PaymentServiceSelection>();
    }

    m_ctx.log("expected ServiceDiscoveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
