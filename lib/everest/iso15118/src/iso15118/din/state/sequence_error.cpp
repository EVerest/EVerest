// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/din/state/sequence_error.hpp>

#include <iso15118/detail/helper.hpp>

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

dt::DcEvseStatus minimal_dc_evse_status() {
    dt::DcEvseStatus status;
    status.evse_notification = dt::EvseNotification::None;
    status.evse_isolation_status = dt::IsolationLevel::Valid;
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
    return status;
}

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
    const auto session_id = ctx.get_session_id();

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
        res.evse_present_voltage = ctx.evse().present_voltage;
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
        res.evse_present_voltage = ctx.evse().present_voltage;
        res.evse_present_current = ctx.evse().present_current;
        ctx.respond(res);
        return;
    }
    case message_din::Type::WeldingDetectionReq: {
        message_din::WeldingDetectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = ctx.evse().present_voltage;
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
        logf_warning("cannot build a DIN response for received type id: %d", received.get_type());
        return;
    }
}

void respond_sequence_error(Context& ctx, const message_din::Variant& received) {
    respond_with_code(ctx, received, dt::ResponseCode::FAILED_SequenceError);
    // Oscillator off without delay + SECC-side TCP close, reported once the response hit the wire.
    ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
}

bool reject_unknown_session(Context& ctx, const message_din::Variant& received) {
    // Before SessionSetup there is no assigned id to compare against, so nothing can be "unknown":
    // an unexpected first message is a sequence error [V2G-DC-666], not FAILED_UnknownSession.
    if (not ctx.session_established()) {
        return false;
    }
    // A SessionSetupReq is never judged against the assigned id either: [V2G-DC-391] governs requests
    // inside an established session, and one arriving there is out of sequence rather than unknown.
    if (received.get_type() == message_din::Type::SessionSetupReq) {
        return false;
    }
    if (received.get_session_id() == ctx.get_session_id()) {
        return false;
    }
    // ctx.respond() arms the FailedTermination close path for every FAILED_* code.
    respond_with_code(ctx, received, dt::ResponseCode::FAILED_UnknownSession);
    ctx.session_stopped = true;
    return true;
}

} // namespace iso15118::din::state
