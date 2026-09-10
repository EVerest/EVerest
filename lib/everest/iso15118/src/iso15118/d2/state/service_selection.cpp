// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_selection.hpp>

#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/service_detail.hpp>
#include <iso15118/detail/d2/vas.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ServiceDetailResponse handle_request(const message_2::ServiceDetailRequest& req,
                                                const dt::SessionId& session_id, uint16_t charge_service_id,
                                                bool cert_service_offered,
                                                const std::optional<dt::ServiceParameterList>& vas_parameters,
                                                bool vas_offered) {
    message_2::ServiceDetailResponse res;
    res.header.session_id = session_id;
    res.service_id = req.service_id;

    if (req.service_id == charge_service_id) {
        res.response_code = dt::ResponseCode::OK;
    } else if (cert_service_offered and req.service_id == dt::CERTIFICATE_SERVICE_ID) {
        // [V2G2-428], Table 106: ParameterSetID 1 = Installation, 2 = Update. The SECC relays both as raw
        // pass-through to the backend, which distinguishes the action, so both are offered.
        res.response_code = dt::ResponseCode::OK;
        auto& parameter_list = res.service_parameter_list.emplace();

        auto& install_set = parameter_list.emplace_back();
        install_set.parameter_set_id = 1;
        auto& install_param = install_set.parameter.emplace_back();
        install_param.name = "Service";
        install_param.string_value = "Installation";

        auto& update_set = parameter_list.emplace_back();
        update_set.parameter_set_id = 2;
        auto& update_param = update_set.parameter.emplace_back();
        update_param.name = "Service";
        update_param.string_value = "Update";
    } else if (vas_offered) {
        // External VAS [V2G2-549]: the provider's parameter sets, or none when it has nothing to detail.
        res.response_code = dt::ResponseCode::OK;
        res.service_parameter_list = vas_parameters;
    } else {
        res.response_code = dt::ResponseCode::FAILED_ServiceIDInvalid;
    }
    return res;
}

void ServiceSelection::enter() {
    logf_debug("Enter state: ServiceSelection");
}

Result ServiceSelection::on_request(const message_2::Variant& received) {
    // [V2G2-545]/[V2G2-548]: ServiceDetail is optional and repeatable, so the EV either asks again or
    // ends the loop by selecting.
    const auto type = received.get_type();
    if (type == message_2::Type::ServiceDetailReq) {
        return process_service_detail(received.get<message_2::ServiceDetailRequest>());
    } else if (type == message_2::Type::PaymentServiceSelectionReq) {
        return process_payment_selection(received.get<message_2::PaymentServiceSelectionRequest>());
    } else {
        logf_warning("Expected ServiceDetailReq or PaymentServiceSelectionReq! But got type id: %d",
                     received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

Result ServiceSelection::process_service_detail(const message_2::ServiceDetailRequest& req) {

    // Only valid over a PnC-enabled TLS session where the SECC provides certificate installation.
    const bool cert_service_offered = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active and
                                      m_ctx.session_config.cert_install_service;

    const bool vas_offered = is_offered_vas(m_ctx.session_config.offered_vas_services, req.service_id);
    std::optional<dt::ServiceParameterList> vas_parameters;
    if (vas_offered) {
        const auto provider_parameters = m_ctx.feedback.get_vas_parameters(req.service_id);
        if (provider_parameters.has_value()) {
            vas_parameters = to_iso2_parameter_list(provider_parameters.value());
        }
    }

    const auto res = handle_request(req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                                    cert_service_offered, vas_parameters, vas_offered);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
    }
    return {};
}

} // namespace iso15118::d2::state
