// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/payment_service_selection.hpp>

#include <algorithm>

#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/payment_details.hpp>
#include <iso15118/d2/state/session_stop.hpp>

#include <iso15118/detail/d2/state/payment_service_selection.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

message_2::PaymentServiceSelectionResponse handle_request(const message_2::PaymentServiceSelectionRequest& req,
                                                          const dt::SessionId& session_id, uint16_t charge_service_id,
                                                          bool pnc_enabled) {
    message_2::PaymentServiceSelectionResponse res;
    res.header.session_id = session_id;

    // Accept ExternalPayment (EIM) always, and Contract (PnC) only when Plug-and-Charge is enabled;
    // otherwise reject [V2G2-465].
    const bool contract_allowed = pnc_enabled and req.selected_payment_option == dt::PaymentOption::Contract;
    if (req.selected_payment_option != dt::PaymentOption::ExternalPayment and not contract_allowed) {
        res.response_code = dt::ResponseCode::FAILED_PaymentSelectionInvalid;
        return res;
    }

    // The charging service must be among the selected services [V2G2-804].
    const auto& list = req.selected_service_list;
    const bool charge_service_selected =
        std::any_of(list.begin(), list.end(), [&](const dt::SelectedService& s) { return s.service_id == charge_service_id; });
    if (not charge_service_selected) {
        res.response_code = dt::ResponseCode::FAILED_NoChargeServiceSelected;
        return res;
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

void PaymentServiceSelection::enter() {
    m_ctx.log.enter_state("PaymentServiceSelection");
}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting mid-handshake sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::PaymentServiceSelectionRequest>();
    if (req == nullptr) {
        m_ctx.log("expected PaymentServiceSelectionReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with the
    // received-type response carrying FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    const auto res = handle_request(*req, m_ctx.get_session_id(), m_ctx.session_config.charge_service_id,
                                    m_ctx.session_config.pnc_enabled);
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    // Contract (PnC) selected: run PaymentDetails (contract chain validation, GenChallenge) before
    // Authorization. ExternalPayment (EIM) goes straight to Authorization.
    if (req->selected_payment_option == dt::PaymentOption::Contract) {
        m_ctx.contract_selected = true;
        return m_ctx.create_state<PaymentDetails>();
    }

    return m_ctx.create_state<Authorization>();
}

} // namespace iso15118::d2::state
