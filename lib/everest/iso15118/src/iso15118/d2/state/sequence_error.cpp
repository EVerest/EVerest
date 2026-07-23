// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d2/state/sequence_error.hpp>

#include <iso15118/message_2/authorization.hpp>
#include <iso15118/message_2/cable_check.hpp>
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

// A minimally-populated DC_EVSEStatus that satisfies the DC_EVSEStatusType schema.
dt::DC_EVSEStatus minimal_dc_evse_status() {
    dt::DC_EVSEStatus status;
    status.notification = dt::EVSENotification::None;
    status.notification_max_delay = 0;
    status.isolation_status = dt::IsolationLevel::Valid;
    status.status_code = dt::DC_EVSEStatusCode::EVSE_Ready;
    return status;
}

// A schema-valid ChargeService for the ServiceDiscoveryRes: needs >=1 SupportedEnergyTransferMode.
dt::ChargeService minimal_charge_service(const Context& ctx) {
    dt::ChargeService charge_service;
    charge_service.service_id = ctx.session_config.charge_service_id;
    charge_service.service_category = dt::ServiceCategory::EVCharging;
    charge_service.free_service = true;
    charge_service.supported_energy_transfer_mode = ctx.session_config.supported_energy_transfer_modes;
    if (charge_service.supported_energy_transfer_mode.empty()) {
        // SupportedEnergyTransferModeType requires at least one entry.
        charge_service.supported_energy_transfer_mode.push_back(dt::EnergyTransferMode::DC_extended);
    }
    return charge_service;
}

} // namespace

void respond_with_code(Context& ctx, const message_2::Variant& received, dt::ResponseCode code) {
    const auto& session_id = ctx.get_session_id();

    switch (received.get_type()) {
    case message_2::Type::SessionSetupReq: {
        message_2::SessionSetupResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.evse_id = ctx.session_config.evse_id;
        ctx.respond(res);
        return;
    }
    case message_2::Type::ServiceDiscoveryReq: {
        message_2::ServiceDiscoveryResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.payment_option_list.push_back(dt::PaymentOption::ExternalPayment);
        res.charge_service = minimal_charge_service(ctx);
        ctx.respond(res);
        return;
    }
    case message_2::Type::ServiceDetailReq: {
        message_2::ServiceDetailResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.service_id = ctx.session_config.charge_service_id;
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
        // GenChallenge and EVSETimeStamp are schema-mandatory (fixed 16-byte challenge).
        res.gen_challenge = ctx.gen_challenge;
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
        // EVSEChargeParameter is schema-mandatory in ChargeParameterDiscoveryRes [V2G2-736]; the
        // minimal AC variant is valid regardless of the energy transfer mode.
        auto& ac = res.ac_evse_charge_parameter.emplace();
        ac.ac_evse_status = make_ac_evse_status();
        ac.evse_nominal_voltage = dt::to_physical_value(0, dt::Unit::V);
        ac.evse_max_current = dt::to_physical_value(0, dt::Unit::A);
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
        res.evse_id = ctx.session_config.evse_id;
        res.sa_schedule_tuple_id = ctx.sa_schedule_tuple_id;
        res.ac_evse_status = make_ac_evse_status();
        ctx.respond(res);
        return;
    }
    case message_2::Type::MeteringReceiptReq: {
        message_2::MeteringReceiptResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        // MeteringReceiptRes carries an optional EVSEStatus; the AC status is schema-valid for both
        // energy-transfer modes. Without this case reject_unknown_session / respond_sequence_error would
        // set session_stopped but stage no response, so the SECC would close without the FAILED_* answer.
        res.ac_evse_status = make_ac_evse_status();
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
        res.evse_present_voltage = dt::to_physical_value(ctx.present_voltage, dt::Unit::V);
        ctx.respond(res);
        return;
    }
    case message_2::Type::CurrentDemandReq: {
        message_2::CurrentDemandResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = dt::to_physical_value(ctx.present_voltage, dt::Unit::V);
        res.evse_present_current = dt::to_physical_value(ctx.present_current, dt::Unit::A);
        res.evse_current_limit_achieved = false;
        res.evse_voltage_limit_achieved = false;
        res.evse_power_limit_achieved = false;
        res.evse_id = ctx.session_config.evse_id;
        res.sa_schedule_tuple_id = ctx.sa_schedule_tuple_id;
        ctx.respond(res);
        return;
    }
    case message_2::Type::WeldingDetectionReq: {
        message_2::WeldingDetectionResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        res.dc_evse_status = minimal_dc_evse_status();
        res.evse_present_voltage = dt::to_physical_value(ctx.present_voltage, dt::Unit::V);
        ctx.respond(res);
        return;
    }
    case message_2::Type::SessionStopReq: {
        message_2::SessionStopResponse res;
        res.header.session_id = session_id;
        res.response_code = code;
        ctx.respond(res);
        return;
    }
    default:
        // Not a known request type (e.g. a response type or None); nothing valid to answer with.
        ctx.log("cannot build a response for received type id: %d", received.get_type());
        return;
    }
}

void respond_sequence_error(Context& ctx, const message_2::Variant& received) {
    respond_with_code(ctx, received, dt::ResponseCode::FAILED_SequenceError);
    // Session ends with a FAILED response: oscillator off without delay + SECC-side TCP close
    // ([V2G-DC-942]/[V2G-DC-940] semantics), reported once the response hit the wire.
    ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
}

bool reject_unknown_session(Context& ctx, const message_2::Variant& received) {
    if (received.get_session_id() == ctx.get_session_id()) {
        return false;
    }
    // The received SessionID does not match the one assigned in SessionSetup: answer with the
    // received-type response carrying FAILED_UnknownSession, then terminate the session.
    respond_with_code(ctx, received, dt::ResponseCode::FAILED_UnknownSession);
    ctx.session_stopped = true;
    ctx.session_stop_res_pending = session::feedback::SessionStopAction::FailedTermination;
    return true;
}

} // namespace iso15118::d2::state
