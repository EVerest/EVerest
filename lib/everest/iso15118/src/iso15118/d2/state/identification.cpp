// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 - 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/identification.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include <iso15118/d2/state/payment_details.hpp>
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>

#include <iso15118/detail/base64.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

#include <cbv2g/common/exi_bitstream.h>
#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>
#include <cbv2g/iso_2/iso2_msgDefDecoder.h>

#include <memory>

namespace iso15118::d2::state {

namespace {

// The backend's CertificateInstallationRes / CertificateUpdateRes is relayed verbatim, so its
// ResponseCode is only visible by decoding the raw EXI. [V2G2-539]: after the SECC sent a FAILED_*
// ResponseCode it terminates the session -- that holds for a relayed FAILED_* response as much as for
// one the SECC generated itself. Undecodable EXI is reported as "not failed": the EV, not the SECC,
// judges the payload, and a spurious termination would mask the real problem.
bool relayed_response_failed(const std::vector<uint8_t>& raw) {
    exi_bitstream_t stream;
    exi_bitstream_init(&stream, const_cast<uint8_t*>(raw.data()), raw.size(), 0, nullptr);
    auto doc = std::make_unique<iso2_exiDocument>();
    if (decode_iso2_exiDocument(&stream, doc.get()) != 0) {
        logf_warning("Identification: backend response does not decode as an ISO 15118-2 message");
        return false;
    }
    const auto& body = doc->V2G_Message.Body;
    if (body.CertificateInstallationRes_isUsed) {
        return body.CertificateInstallationRes.ResponseCode >= iso2_responseCodeType_FAILED;
    }
    if (body.CertificateUpdateRes_isUsed) {
        return body.CertificateUpdateRes.ResponseCode >= iso2_responseCodeType_FAILED;
    }
    return false;
}

} // namespace

void Identification::enter() {
    logf_debug("Enter state: Identification");
}

Result Identification::on_event(Event ev) {
    // Splice the raw CertificateInstallationRes EXI onto the wire verbatim.
    if (ev == Event::CONTROL_MESSAGE) {
        const auto* response = m_ctx.get_control_event<d20::CertificateResponse>();
        if (response == nullptr) {
            return {};
        }
        if (not response->status_accepted or response->exi_response_base64.empty()) {
            logf_warning("Identification: backend reported failure; terminating session");
            m_ctx.session_stopped = true;
            return {};
        }
        const auto raw = base64_decode(response->exi_response_base64);
        if (raw.empty()) {
            logf_warning("Identification: failed to base64-decode the backend response");
            m_ctx.session_stopped = true;
            return {};
        }
        m_ctx.respond_raw(raw);
        if (relayed_response_failed(raw)) {
            // [V2G2-539] / [V2G2-555] / [V2G2-558]: the FAILED_* response reaches the EV, then the SECC
            // closes -- same discipline as respond_sequence_error() for locally generated failures.
            logf_info("Identification: relayed a FAILED_* certificate response; terminating the session");
            m_ctx.session_stopped = true;
            m_ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
            return {};
        }
        return m_ctx.create_state<PaymentDetails>();
    }

    return {};
}

Result Identification::on_request(const message_2::Variant& received) {
    // [V2G2-551], the Plug-and-Charge branch; an EIM session never reaches here. While the certificate
    // request is with the backend nothing is in sequence at all.
    const auto type = received.get_type();
    if (request_forwarded) {
        logf_warning("Identification: request (type id: %d) received while waiting for the backend "
                     "response; answering FAILED_SequenceError",
                     received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    } else if (type == message_2::Type::PaymentDetailsReq) {
        return process_payment_details(m_ctx, received.get<message_2::PaymentDetailsRequest>());
    } else if (type == message_2::Type::CertificateInstallationReq or type == message_2::Type::CertificateUpdateReq) {
        // [V2G2-551]: after PaymentServiceSelectionRes(OK) with Contract, CertificateInstallationReq and
        // CertificateUpdateReq are allowed next requests -- unconditionally. Whether the EV also selected the
        // Certificate service (ServiceID 2 / Table 106) in PaymentServiceSelectionReq is not a precondition;
        // [V2G2-432]/[V2G2-433] only govern the validity of the selected (ServiceID, ParameterSetID) pairs,
        // and the ISO 15118-4 ATS (TC_SECC_CMN_VTB_CertificateInstallation_001, f_SECC_CMN_PR_
        // PaymentServiceSelection_001) selects the charge service alone and expects the exchange to succeed.
        // Only a SECC that does not offer certificate handling at all treats the request as out of sequence.
        if (not m_ctx.session_config.cert_install_service) {
            logf_warning("Identification: certificate exchange requested but the certificate service is not "
                         "offered by this SECC");
            respond_sequence_error(m_ctx, received.get_type());
            return {};
        }
        return forward_to_backend(received);
    } else {
        logf_warning("Identification: expected PaymentDetailsReq or a certificate exchange, got type id: %d",
                     received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

Result Identification::forward_to_backend(const message_2::Variant& received) {
    const auto action = (received.get_type() == message_2::Type::CertificateUpdateReq)
                            ? session::feedback::CertificateExchangeAction::Update
                            : session::feedback::CertificateExchangeAction::Install;

    const auto& exi = received.get_exi_payload();
    if (exi.empty()) {
        logf_warning("Identification: empty request EXI payload; terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    request_forwarded = true;
    m_ctx.feedback.certificate_request({base64_encode(exi), action, ProtocolId::ISO15118_2});

    // Park: no response is staged until the module injects the CertificateInstallationRes.
    return {};
}

} // namespace iso15118::d2::state
