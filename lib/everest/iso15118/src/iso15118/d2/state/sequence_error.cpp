// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d2/state/sequence_error.hpp>

#include <iso15118/detail/helper.hpp>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
#include <iso15118/message_2/certificate_installation.hpp>
#include <iso15118/message_2/charge_parameter_discovery.hpp>
#include <iso15118/message_2/charging_status.hpp>
#include <iso15118/message_2/current_demand.hpp>
#include <iso15118/message_2/metering_receipt.hpp>
#include <iso15118/message_2/payment_details.hpp>
#include <iso15118/message_2/payment_service_selection.hpp>
#include <iso15118/message_2/power_delivery.hpp>
#include <iso15118/message_2/pre_charge.hpp>
#include <iso15118/message_2/service_detail.hpp>
#include <iso15118/message_2/service_discovery.hpp>
#include <iso15118/message_2/session_setup.hpp>
#include <iso15118/message_2/session_stop.hpp>
#include <iso15118/message_2/welding_detection.hpp>

#include <iso15118/detail/d2/state/state_helper.hpp>

namespace iso15118::d2::state {

namespace {

// [V2G2-736]: a FAILED response still has to carry every schema-mandatory parameter, but the values
// are arbitrary as long as they are XSD-conform, and [V2G2-735] has the EVCC ignore them. So nothing
// below reads live session or charger state -- an error response reports only what went wrong.

// Table 27's zero-value for an EVSEID the SECC cannot provide; evseIDType is 7..37 characters.
constexpr auto EVSEID_ZERO = "ZZ00000";

// SAIDType is 1..255, so 1 is the smallest conform value.
constexpr uint8_t SA_SCHEDULE_TUPLE_ID_MIN = 1;

// eMAID is 14..15 characters.
constexpr auto EMAID_PLACEHOLDER = "00000000000000";

dt::PhysicalValue zero(dt::Unit unit) {
    return dt::to_physical_value(0, unit);
}

dt::DC_EVSEStatus minimal_dc_evse_status() {
    dt::DC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.isolation_status = dt::IsolationLevel::Valid;
    status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    return status;
}

// A schema-valid ChargeService for the ServiceDiscoveryRes: needs >=1 SupportedEnergyTransferMode.
dt::ChargeService minimal_charge_service() {
    dt::ChargeService charge_service;
    charge_service.service_id = 0;
    charge_service.service_category = dt::ServiceCategory::EVCharging;
    charge_service.free_service = true;
    charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    return charge_service;
}

} // namespace

void respond_with_code(Context& ctx, message_2::Type received_type, dt::ResponseCode code) {
    const auto session_id = ctx.get_session_id();

    switch (received_type) {
    case message_2::Type::SessionSetupReq: {
        message_2::SessionSetupResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_id = EVSEID_ZERO;
        ctx.respond(res);
        return;
    }
    case message_2::Type::ServiceDiscoveryReq: {
        message_2::ServiceDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
        res.charge_service = minimal_charge_service();
        ctx.respond(res);
        return;
    }
    case message_2::Type::ServiceDetailReq: {
        message_2::ServiceDetailResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.service_id = 0;
        ctx.respond(res);
        return;
    }
    case message_2::Type::PaymentServiceSelectionReq: {
        message_2::PaymentServiceSelectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        ctx.respond(res);
        return;
    }
    case message_2::Type::PaymentDetailsReq: {
        message_2::PaymentDetailsResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // All-zero is conform and minimal; the real challenge belongs to the PaymentDetailsRes alone.
        res.gen_challenge = dt::GenChallenge{};
        ctx.respond(res);
        return;
    }
    case message_2::Type::AuthorizationReq: {
        message_2::AuthorizationResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_processing = dt::EVSEProcessing::Finished;
        ctx.respond(res);
        return;
    }
    case message_2::Type::ChargeParameterDiscoveryReq: {
        message_2::ChargeParameterDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_processing = dt::EVSEProcessing::Finished;
        // The minimal AC variant is schema-valid regardless of the energy transfer mode.
        auto& ac = res.ac_evse_charge_parameter.emplace();
        ac.ac_evse_status = make_ac_evse_status();
        ac.evse_nominal_voltage = zero(dt::Unit::V);
        ac.evse_max_current = zero(dt::Unit::A);
        ctx.respond(res);
        return;
    }
    case message_2::Type::PowerDeliveryReq: {
        message_2::PowerDeliveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // PowerDeliveryRes requires an EVSEStatus choice; the AC status is schema-valid for both.
        res.ac_evse_status = make_ac_evse_status();
        ctx.respond(res);
        return;
    }
    case message_2::Type::ChargingStatusReq: {
        message_2::ChargingStatusResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_id = EVSEID_ZERO;
        res.sa_schedule_tuple_id = SA_SCHEDULE_TUPLE_ID_MIN;
        res.ac_evse_status = make_ac_evse_status();
        ctx.respond(res);
        return;
    }
    case message_2::Type::MeteringReceiptReq: {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // EVSEStatus is optional, so the minimal response is the ResponseCode alone. The case still has to
        // exist: without it no response is staged and the SECC closes without telling the EV why.
        ctx.respond(res);
        return;
    }
    case message_2::Type::CableCheckReq: {
        message_2::CableCheckResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_processing = dt::EVSEProcessing::Finished;
        ctx.respond(res);
        return;
    }
    case message_2::Type::PreChargeReq: {
        message_2::PreChargeResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = zero(dt::Unit::V);
        ctx.respond(res);
        return;
    }
    case message_2::Type::CurrentDemandReq: {
        message_2::CurrentDemandResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = zero(dt::Unit::V);
        res.evse_present_current = zero(dt::Unit::A);
        res.evse_current_limit_achieved = false;
        res.evse_voltage_limit_achieved = false;
        res.evse_power_limit_achieved = false;
        res.evse_id = EVSEID_ZERO;
        res.sa_schedule_tuple_id = SA_SCHEDULE_TUPLE_ID_MIN;
        ctx.respond(res);
        return;
    }
    case message_2::Type::WeldingDetectionReq: {
        message_2::WeldingDetectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = zero(dt::Unit::V);
        ctx.respond(res);
        return;
    }
    case message_2::Type::CertificateInstallationReq: {
        message_2::CertificateInstallationResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // Every element is schema-mandatory, so even a FAILED response carries placeholders the EV discards.
        // A successful installation is relayed from the backend as raw EXI and never takes this path.
        res.sa_provisioning_chain.certificate = {0x00};
        res.contract_chain.id = "contractSignatureCertChain";
        res.contract_chain.certificate = {0x00};
        res.encrypted_private_key = {0x00};
        res.dh_public_key = {0x00};
        res.emaid = EMAID_PLACEHOLDER;
        ctx.respond(res);
        return;
    }
    // NOTE: CertificateUpdateReq has no case here -- CertificateUpdateRes is not modelled at all (the
    // SECC only ever relays it), so an out-of-sequence update still closes without a response.
    case message_2::Type::SessionStopReq: {
        message_2::SessionStopResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        ctx.respond(res);
        return;
    }
    default:
        logf_warning("cannot build a response for received type id: %d", received_type);
        return;
    }
}

namespace {

// Owned here so no state has to remember to stop the session after asking for an error response.
void respond_and_terminate(Context& ctx, message_2::Type received_type, dt::ResponseCode code) {
    respond_with_code(ctx, received_type, code);
    ctx.session_stopped = true;
    // Oscillator off without delay + SECC-side TCP close, reported once the response hit the wire.
    ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
}

} // namespace

void respond_sequence_error(Context& ctx, message_2::Type received_type) {
    respond_and_terminate(ctx, received_type, dt::ResponseCode::FAILED_SequenceError);
}

bool reject_unknown_session(Context& ctx, message_2::Type received_type, const dt::SessionId& received_id) {
    // Before SessionSetup there is no id to compare against, and Table 112 does not list
    // FAILED_UnknownSession for SessionSetupRes -- such a request is out of sequence instead.
    if (not ctx.session_established()) {
        return false;
    }
    // [V2G2-460]'s sole exemption; mid-session a second SessionSetupReq is out of sequence instead.
    if (received_type == message_2::Type::SessionSetupReq) {
        return false;
    }
    if (received_id == ctx.get_session_id()) {
        return false;
    }
    respond_and_terminate(ctx, received_type, dt::ResponseCode::FAILED_UnknownSession);
    return true;
}

} // namespace iso15118::d2::state
