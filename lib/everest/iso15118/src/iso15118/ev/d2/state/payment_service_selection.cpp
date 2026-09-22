// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/payment_service_selection.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/authorization.hpp>
#include <iso15118/ev/d2/state/certificate_installation.hpp>
#include <iso15118/ev/d2/state/payment_details.hpp>
#include <iso15118/ev/d2/state/service_detail.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/payment_service_selection.hpp>
#include <iso15118/message_2/service_detail.hpp>

namespace iso15118::ev::d2::state {

namespace payment_service_selection {

message_2::PaymentServiceSelectionRequest create_request(uint16_t charge_service_id, dt::PaymentOption payment_option,
                                                         bool add_certificate_service) {
    message_2::PaymentServiceSelectionRequest req;
    req.selected_payment_option = payment_option;
    req.selected_service_list.push_back(dt::SelectedService{charge_service_id, std::nullopt});
    if (add_certificate_service) {
        req.selected_service_list.push_back(dt::SelectedService{dt::CERTIFICATE_SERVICE_ID, 1});
    }
    return req;
}

} // namespace payment_service_selection

namespace {

// enforce_contract selects Contract regardless of the SECC offer ([V2G2-465] robustness test).
// Neither it nor prefer_contract puts a contract on a plaintext link: PaymentDetailsReq would carry
// the contract chain and the eMAID in clear, and AuthorizationReq a signature over a peer-chosen
// challenge. allow_insecure_pnc is how that deviation is accepted deliberately.
bool use_contract(const Context& ctx) {
    const auto& pnc = ctx.params().pnc;
    const bool wants = pnc.enforce_contract or (pnc.prefer_contract and ctx.evse_info.contract_offered);
    if (not wants) {
        return false;
    }
    if (ctx.params().secure_transport or pnc.allow_insecure_pnc) {
        return true;
    }
    logf_warning("Plug & Charge requested on a plaintext session; selecting ExternalPayment "
                 "(set allow_insecure_pnc to deviate from the standard)");
    return false;
}

bool needs_certificate_service(const Context& ctx) {
    return use_contract(ctx) and ctx.params().pnc.needs_cert_install() and ctx.evse_info.certificate_service_offered;
}

bool install_cert(const Context& ctx) {
    return needs_certificate_service(ctx) and ctx.evse_info.certificate_installation_offered;
}

// Why the EV is left with ExternalPayment although the SECC offered only Contract.
const char* no_external_payment_reason(const Context& ctx) {
    const auto& pnc = ctx.params().pnc;
    if (not pnc.enforce_contract and not pnc.prefer_contract) {
        return "Contract is the only offer but Plug and Charge is not enabled";
    }
    if (not ctx.params().secure_transport and not pnc.allow_insecure_pnc) {
        return "Contract is the only offer but the session is plaintext and allow_insecure_pnc is off";
    }
    return "No usable contract certificate and no ExternalPayment offer";
}

} // namespace

BasePointerType to_service_detail_or_payment_selection(Context& ctx) {
    if (needs_certificate_service(ctx)) {
        message_2::ServiceDetailRequest req{};
        req.service_id = dt::CERTIFICATE_SERVICE_ID;
        ctx.send_request(req);
        return ctx.create_state<ServiceDetail>();
    }
    return to_payment_service_selection(ctx);
}

BasePointerType to_payment_service_selection(Context& ctx) {
    bool contract = use_contract(ctx);
    const auto& cfg = ctx.params().pnc;
    // Contract without a certificate to install and without a pre-installed one has nothing to
    // authenticate with; enforce_contract keeps the [V2G2-465] robustness case selectable.
    if (contract and not cfg.enforce_contract and not install_cert(ctx) and not cfg.contract.has_value()) {
        logf_warning("Contract selected without an installable or pre-installed contract certificate; "
                     "falling back to ExternalPayment");
        contract = false;
    }
    // [V2G2-465] the selected option must be one the SECC offered.
    if (not contract and not ctx.evse_info.eim_offered) {
        logf_error("%s; stopping the session", no_external_payment_reason(ctx));
        return to_session_stop(ctx, session::StopReason::EvStop);
    }
    ctx.pnc.contract_selected = contract;
    const auto option = contract ? dt::PaymentOption::Contract : dt::PaymentOption::ExternalPayment;
    ctx.send_request(
        payment_service_selection::create_request(ctx.evse_info.selected_charge_service_id, option, install_cert(ctx)));
    return ctx.create_state<PaymentServiceSelection>();
}

void PaymentServiceSelection::enter() {
    logf_debug("Enter state: PaymentServiceSelection (ISO 15118-2)");
}

Result PaymentServiceSelection::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::PaymentServiceSelectionResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (not m_ctx.pnc.contract_selected) {
        return to_authorization(m_ctx);
    }

    if (install_cert(m_ctx)) {
        return to_certificate_installation(m_ctx);
    }

    // Pre-installed contract: seed the runtime PnC state from the config.
    const auto& cfg = m_ctx.params().pnc;
    if (not cfg.contract.has_value()) {
        // enforce_contract only: the SECC is expected to reject the empty PaymentDetailsReq.
        logf_warning("Contract enforced without a contract certificate; sending empty PaymentDetails material");
    }
    m_ctx.pnc.contract = cfg.contract;
    return to_payment_details(m_ctx);
}

} // namespace iso15118::ev::d2::state
