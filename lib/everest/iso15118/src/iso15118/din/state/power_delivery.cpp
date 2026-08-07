// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/state/power_delivery.hpp>

#include <cstdint>

#include <iso15118/din/state/current_demand.hpp>
#include <iso15118/din/state/session_stop.hpp>
#include <iso15118/din/state/welding_detection.hpp>

#include <iso15118/detail/din/state/power_delivery.hpp>
#include <iso15118/detail/din/state/sequence_error.hpp>
#include <iso15118/detail/din/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::din::state {

namespace {
// The single SAScheduleTuple advertised in ChargeParameterDiscoveryRes (see build_sa_schedule_list in
// din/state/charge_parameter_discovery.cpp): id 1, spanning DIN_SA_SCHEDULE_DURATION.
constexpr int16_t DIN_SA_SCHEDULE_TUPLE_ID = 1;
constexpr uint32_t DIN_SA_SCHEDULE_DURATION = 86400; // [V2G-DC-556] must cover 24 hours
} // namespace

message_din::PowerDeliveryResponse handle_request(const message_din::PowerDeliveryRequest& req,
                                                  const dt::SessionId& session_id, bool charger_stop,
                                                  std::optional<dt::DcEvseStatusCode> error_status_code) {
    message_din::PowerDeliveryResponse res;
    setup_header(res.header, session_id);

    // PowerDeliveryRes requires an EVSEStatus (DIN is DC-only); it is mandatory even on a
    // FAILED_UnknownSession response, so populate it before the SessionID check below. An EVSE-initiated
    // stop is signalled with EVSE_Shutdown in every state, not just the charge loop (EvseV2G parity; the
    // EVSENotification stays None for DC [V2G-DC-500]).
    // [V2G-DC-638] EVSE_Ready is used "unless a different value shall be used according to another
    // requirement": a module-reported fault (send_error) wins here, and it is one of the two codes the EV
    // is required to act on rather than disregard ([V2G-DC-893]/[V2G-DC-896]/[V2G-DC-899] single out
    // EVSE_Shutdown and EVSE_EmergencyShutdown). EvseV2G reports the module code in every response too.
    dt::DcEvseStatus status;
    status.evse_status_code = error_status_code.value_or(charger_stop ? dt::DcEvseStatusCode::EVSE_Shutdown
                                                                      : dt::DcEvseStatusCode::EVSE_Ready);
    status.evse_isolation_status = dt::IsolationLevel::Valid;
    res.dc_evse_status = status;

    // [V2G-DC-391] the SessionID must match the one assigned in SessionSetup; a mismatch is answered with
    // FAILED_UnknownSession (carrying the mandatory EVSEStatus filled above) and terminates the session.
    if (session_id != req.header.session_id) {
        return response_with_code(res, dt::ResponseCode::FAILED_UnknownSession);
    }

    // Validate the optional ChargingProfile the EV attaches to a PowerDeliveryReq(Start). The referenced
    // SAScheduleTupleID must be the single tuple offered in ChargeParameterDiscoveryRes; any other value is
    // a wrong tariff selection [V2G-DC-400]. Each ProfileEntry must reference a time inside that schedule's
    // duration; an entry starting past it is a wrong charging profile [V2G-DC-399]. (DIN/-2 only; the
    // mandatory EVSEStatus filled above rides along on the FAILED response.)
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

    // [V2G-DC-401] the response carries FAILED_PowerDeliveryNotApplied "if the EVSE is not able to deliver
    // energy". A latched module error (send_error) is exactly that state, and the status code is where the
    // EVSE says so. Checked after the request-validation legs above so a malformed request still gets its
    // own specific code.
    //
    // EvseV2G phrases the ISO 15118-2 twin [V2G2-480] as "Start and EVSEStatusCode != EVSE_Ready"
    // (iso_server.cpp), which also catches the EVSE_Shutdown of a charger-initiated stop. We deliberately
    // do not: a stop REQUEST is not an inability to deliver energy, and the STOP_CHARGING guard already
    // enforces the stop once the EV's grace window closes (charger_stop_ignored).
    if (req.ready_to_charge_state and error_status_code.has_value()) {
        return response_with_code(res, dt::ResponseCode::FAILED_PowerDeliveryNotApplied);
    }

    return response_with_code(res, dt::ResponseCode::OK);
}

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery");
}

Result PowerDelivery::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control_data->voltage;
            m_ctx.present_current = control_data->current;
        }
        // An EVSE-initiated stop (StopCharging) is latched on the context by the engine, in any state.
        return {};
    }

    if (ev == Event::TIMEOUT) {
        // [V2G-DC-969/989]: the SECC PowerDelivery supervision timer armed at first PreChargeReq spans
        // into PowerDelivery; on expiry terminate the session.
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            logf_warning("PowerDelivery timeout reached, terminating session");
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_din::Type::SessionStopReq) {
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<message_din::PowerDeliveryRequest>()) {
        auto res =
            handle_request(*req, m_ctx.get_session_id(), m_ctx.charger_stop_requested, m_ctx.error_status_code());
        if (res.dc_evse_status.has_value()) {
            apply_isolation_status(m_ctx, res.dc_evse_status.value());
        }
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        if (req->dc_ev_power_delivery_parameter.has_value()) {
            const auto& param = req->dc_ev_power_delivery_parameter.value();
            m_ctx.report_ev_status(param.dc_ev_status);

            // DC_EVPowerDeliveryParameter carries the completion flags but no remaining times; those stay
            // absent so the module does not overwrite the charge loop's values with zeroes.
            session::feedback::DcEvChargeProgress progress{};
            progress.charging_complete = param.charging_complete;
            progress.bulk_charging_complete = param.bulk_charging_complete;
            m_ctx.report_charge_progress(progress);
        }

        if (req->ready_to_charge_state) {
            // Contactor closed / power path established -> the charge loop can start. The PowerDelivery
            // supervision timer [V2G-DC-969] has done its job; stop it before the charge loop.
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            m_ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
            return m_ctx.create_state<CurrentDemand>();
        }

        // End of charging: open the contactor and move on to welding detection. With the contactor
        // open the previously verified isolation status no longer holds, so a post-stop restart must
        // re-run the cable check.
        // The next WeldingDetection/SessionStop request now requires CP State B within
        // V2G_SECC_CPState_Detection_Timeout ([V2G-DC-988]/[V2G-DC-556]).
        m_ctx.power_delivery_stopped = true;
        m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
        m_ctx.cable_check_done = false;
        m_ctx.cable_check_fault = false;
        m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
        return m_ctx.create_state<WeldingDetection>();
    }

    logf_warning("Expected PowerDeliveryReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
