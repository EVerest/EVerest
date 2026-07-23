// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/din/state/sequence_error.hpp>

#include <iso15118/message_din/cable_check.hpp>
#include <iso15118/message_din/charge_parameter_discovery.hpp>
#include <iso15118/message_din/contract_authentication.hpp>
#include <iso15118/message_din/current_demand.hpp>
#include <iso15118/message_din/power_delivery.hpp>
#include <iso15118/message_din/pre_charge.hpp>
#include <iso15118/message_din/service_discovery.hpp>
#include <iso15118/message_din/service_payment_selection.hpp>
#include <iso15118/message_din/session_setup.hpp>
#include <iso15118/message_din/session_stop.hpp>
#include <iso15118/message_din/welding_detection.hpp>

namespace iso15118::din::state {

namespace {

// A minimally-populated DcEvseStatus that satisfies the DC_EVSEStatusType schema.
dt::DcEvseStatus minimal_dc_evse_status() {
    dt::DcEvseStatus status;
    status.evse_notification = dt::EvseNotification::None;
    status.evse_isolation_status = dt::IsolationLevel::Valid;
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    return status;
}

// A schema-valid ChargeService for the ServiceDiscoveryRes.
dt::ChargeService minimal_charge_service(const Context& ctx) {
    dt::ChargeService charge_service;
    charge_service.service_tag.service_id = ctx.session_config.charge_service_id;
    charge_service.service_tag.service_category = dt::ServiceCategory::EVCharging;
    charge_service.free_service = ctx.session_config.free_service;
    charge_service.energy_transfer_type = ctx.session_config.energy_transfer_mode;
    return charge_service;
}

} // namespace

void respond_with_code(Context& ctx, const message_din::Variant& received, dt::ResponseCode code) {
    const auto& session_id = ctx.get_session_id();

    switch (received.get_type()) {
    case message_din::Type::SessionSetupReq: {
        message_din::SessionSetupResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_id = ctx.session_config.evse_id;
        ctx.respond(res);
        return;
    }
    case message_din::Type::ServiceDiscoveryReq: {
        message_din::ServiceDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.payment_options = {dt::PaymentOption::ExternalPayment};
        res.charge_service = minimal_charge_service(ctx);
        ctx.respond(res);
        return;
    }
    case message_din::Type::ServicePaymentSelectionReq: {
        message_din::ServicePaymentSelectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        ctx.respond(res);
        return;
    }
    case message_din::Type::ContractAuthenticationReq: {
        message_din::ContractAuthenticationResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_processing = dt::EvseProcessing::Finished;
        ctx.respond(res);
        return;
    }
    case message_din::Type::ChargeParameterDiscoveryReq: {
        message_din::ChargeParameterDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_processing = dt::EvseProcessing::Finished;
        // DC_EVSEChargeParameter is schema-mandatory in ChargeParameterDiscoveryRes.
        auto& dc = res.dc_evse_charge_parameter.emplace();
        dc.dc_evse_status = minimal_dc_evse_status();
        ctx.respond(res);
        return;
    }
    case message_din::Type::CableCheckReq: {
        message_din::CableCheckResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_processing = dt::EvseProcessing::Finished;
        ctx.respond(res);
        return;
    }
    case message_din::Type::PreChargeReq: {
        message_din::PreChargeResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = ctx.present_voltage;
        ctx.respond(res);
        return;
    }
    case message_din::Type::PowerDeliveryReq: {
        message_din::PowerDeliveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // PowerDeliveryRes requires an EVSEStatus choice; DIN is DC-only.
        res.dc_evse_status = minimal_dc_evse_status();
        ctx.respond(res);
        return;
    }
    case message_din::Type::CurrentDemandReq: {
        message_din::CurrentDemandResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = ctx.present_voltage;
        res.evse_present_current = ctx.present_current;
        ctx.respond(res);
        return;
    }
    case message_din::Type::WeldingDetectionReq: {
        message_din::WeldingDetectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = ctx.present_voltage;
        ctx.respond(res);
        return;
    }
    case message_din::Type::SessionStopReq: {
        message_din::SessionStopResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        ctx.respond(res);
        return;
    }
    default:
        // Not a known DIN request type (e.g. a response type or None); nothing valid to answer with.
        ctx.log("cannot build a DIN response for received type id: %d", received.get_type());
        return;
    }
}

void respond_sequence_error(Context& ctx, const message_din::Variant& received) {
    respond_with_code(ctx, received, dt::ResponseCode::FAILED_SequenceError);
    // Session ends with a FAILED response: oscillator off without delay + SECC-side TCP close
    // ([V2G-DC-942]/[V2G-DC-940]), reported once the response hit the wire.
    ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
}

} // namespace iso15118::din::state
