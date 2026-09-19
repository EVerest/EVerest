// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest

#include <cstdint>

#include <iso15118/din/state/current_demand.hpp>
#include <iso15118/din/state/session_stop.hpp>
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/detail/din/state/constants.hpp>
#include <iso15118/detail/din/state/power_delivery.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
// The single tuple advertised in ChargeParameterDiscoveryRes: id 1, spanning DIN_SA_SCHEDULE_DURATION.
constexpr int16_t DIN_SA_SCHEDULE_TUPLE_ID = 1;
} // namespace

message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id, bool charger_stop,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::PowerDeliveryResponse res;
    setup_header(res.header, session_id);

    // Mandatory in PowerDeliveryRes even on a FAILED_UnknownSession response, so populate it first.
    // [V2G-DC-638]: a module-reported fault wins here, and EVSE_Shutdown / EVSE_EmergencyShutdown are
    // the two codes the EV is required to act on rather than disregard ([V2G-DC-893]/[V2G-DC-896]).
    dt::DcEvseStatus status;
    status.evse_status_code = error_status_code.value_or(charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown
                                                                      : dt::DcEvseStatusCode::EVSE_Ready);
    status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status = status;
    // The referenced SAScheduleTupleID must be the single tuple offered in ChargeParameterDiscoveryRes;
    // any other value is a wrong tariff selection [V2G-DC-400]. Each ProfileEntry must reference a time
    // inside that schedule's duration; one starting past it is a wrong charging profile [V2G-DC-399].
    if (req.charging_profile.has_value()) {
        const auto& profile = req.charging_profile.value();
        if (profile.sa_schedule_tuple_id != DIN_SA_SCHEDULE_TUPLE_ID) {
            return response_with_code(res, dt::ResponseCode::FAILED_TariffSelectionInvalid);
        }
        for (const auto& entry : profile.profile_entries) {
            if (entry.charging_profile_entry_start > DIN_SA_SCHEDULE_DURATION) {
                return response_with_code(res, dt::ResponseCode::FAILED_ChargingProfileInvalid);
            }
        }
    }

    // [V2G-DC-401]: FAILED_PowerDeliveryNotApplied when the EVSE cannot deliver energy. Checked after
    // the request-validation legs above, so a malformed request still gets its own specific code.
    //
    // EvseV2G phrases the ISO 15118-2 twin [V2G2-480] as "Start and EVSEStatusCode != EVSE_Ready", which
    // also catches the EVSE_Shutdown of a charger-initiated stop. We deliberately do not: a stop REQUEST
    // is not an inability to deliver energy, and the STOP_CHARGING guard enforces it once the window closes.
    if (req.ready_to_charge_state and error_status_code.has_value()) {
        return response_with_code(res, dt::ResponseCode::FAILED_PowerDeliveryNotApplied);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

Result process_power_delivery(Context& m_ctx, const message_din::PowerDeliveryRequest& req) {
    auto res =
        handle_request(req, m_ctx.get_session_id(), m_ctx.evse().charger_stop_requested, m_ctx.error_status_code());
    if (res.dc_evse_status.has_value()) {
        apply_isolation_status(m_ctx, res.dc_evse_status.value());
    }
    m_ctx.respond(res);

    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    if (req.dc_ev_power_delivery_parameter.has_value()) {
        const auto& param = req.dc_ev_power_delivery_parameter.value();
        m_ctx.report_ev_status(param.dc_ev_status);

        // No remaining times here, so they stay absent and the module keeps the charge loop's values.
        session::feedback::DcEvChargeProgress progress{};
        progress.charging_complete = param.charging_complete;
        progress.bulk_charging_complete = param.bulk_charging_complete;
        m_ctx.report_charge_progress(progress);
    }

    if (req.ready_to_charge_state) {
        // The charge loop can start [V2G-DC-462], so the [V2G-DC-969] supervision timer has done its job.
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        m_ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
        return m_ctx.create_state<CurrentDemandStart>();
    }

    // End of charging [V2G-DC-459]: open the contactor and move on to welding detection. The verified
    // isolation no longer holds, so a post-stop restart must re-run the cable check, and the next
    // request now requires CP State B within the detection timeout ([V2G-DC-988]/[V2G-DC-556]).
    m_ctx.power_delivery_stopped = true;
    m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
    m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
    m_ctx.invalidate_cable_check();
    m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
    return m_ctx.create_state<WeldingDetection>();
}

} // namespace iso15118::din::state
