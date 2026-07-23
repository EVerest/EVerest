// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/service_detail.hpp>

#include <iso15118/d2/state/payment_service_selection.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/service_detail.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::ServiceDetailResponse handle_request(const message_2::ServiceDetailRequest& req,
                                                const dt::SessionId& session_id, uint16_t charge_service_id,
                                                bool cert_service_offered) {
    message_2::ServiceDetailResponse res;
    res.header.session_id = session_id;
    res.service_id = req.service_id;

    if (req.service_id == charge_service_id) {
        res.response_code = dt::ResponseCode::OK;
    } else if (cert_service_offered and req.service_id == dt::CERTIFICATE_SERVICE_ID) {
        // Certificate service details [V2G2-428], Table 106: ParameterSetID 1 = "Installation",
        // ParameterSetID 2 = "Update". The SECC relays both CertificateInstallation and CertificateUpdate
        // (raw pass-through to the CSMS/CPS backend, which distinguishes the action), so both are offered.
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
    } else {
        res.response_code = dt::ResponseCode::FAILED_ServiceIDInvalid;
    }
    return res;
}

void ServiceDetail::enter() {
    m_ctx.log.enter_state("ServiceDetail");
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    // ServiceDetail is optional: loop on ServiceDetailReq, otherwise hand the pending request to the
    // PaymentServiceSelection state (transition without consuming; the engine re-feeds it).
    if (m_ctx.peek_request_type() != message_2::Type::ServiceDetailReq) {
        return m_ctx.create_state<PaymentServiceSelection>();
    }

    const auto variant = m_ctx.pull_request();
    const auto req = variant->get<message_2::ServiceDetailRequest>();

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // ServiceDetailRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    // The Certificate service is only offered (hence its detail only valid) over a PnC-enabled TLS session
    // when the SECC actually provides certificate installation/update.
    const bool cert_service_offered = m_ctx.session_config.pnc_enabled and m_ctx.session_config.tls_active and
                                      m_ctx.session_config.cert_install_service;
    const auto res =
        handle_request(req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id, cert_service_offered);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
    }
    return {};
}

} // namespace iso15118::d2::state
