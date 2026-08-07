// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/power_delivery.hpp>

#include <algorithm>

#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/charging_status.hpp>
#include <iso15118/d2/state/current_demand.hpp>
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/d2/state/welding_detection.hpp>

#include <iso15118/detail/d2/state/power_delivery.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
constexpr uint32_t CONTACTOR_TIMEOUT_MS = 3000;
} // namespace

bool charging_profile_within_limits(const dt::ChargingProfile& profile, const dt::SAScheduleList& sa_schedule_list,
                                    uint8_t advertised_sa_schedule_tuple_id) {
    // Locate the advertised tuple the EV echoed; fall back to the first tuple if none matches.
    const dt::SAScheduleTuple* tuple = nullptr;
    for (const auto& candidate : sa_schedule_list) {
        if (candidate.sa_schedule_tuple_id == advertised_sa_schedule_tuple_id) {
            tuple = &candidate;
            break;
        }
    }
    if (tuple == nullptr and not sa_schedule_list.empty()) {
        tuple = &sa_schedule_list.front();
    }
    if (tuple == nullptr or tuple->pmax_schedule.empty()) {
        // Without an advertised schedule there is nothing to validate against.
        return true;
    }

    // Conservative [V2G2-224/225] check: no ProfileEntry may exceed the largest advertised PMax.
    double max_pmax = 0.0;
    for (const auto& entry : tuple->pmax_schedule) {
        max_pmax = std::max(max_pmax, dt::from_physical_value(entry.p_max));
    }
    for (const auto& entry : profile.profile_entry) {
        if (dt::from_physical_value(entry.max_power) > max_pmax) {
            return false;
        }
    }
    return true;
}

message_2::PowerDeliveryResponse
handle_request(const message_2::PowerDeliveryRequest& req, const dt::SessionId& session_id, bool is_dc,
               uint8_t advertised_sa_schedule_tuple_id, dt::IsolationLevel isolation_status, bool charger_stop,
               const dt::SAScheduleList& sa_schedule_list, std::optional<dt::DC_EVSEStatusCode> error_status_code,
               bool rcd_error) {
    message_2::PowerDeliveryResponse res;
    res.header.session_id = session_id;

    // [V2G2-366] the SECC reports the EVSEStatusCodes of Table 98 in every DC response, so a module-
    // reported fault (send_error: Malfunction / UtilityInterruptEvent / EmergencyShutdown) wins over the
    // EVSE_Ready this state would otherwise claim -- Table 98 defines EVSE_Ready as "charging procedure is
    // running", which is exactly what is not true then (EvseV2G get_emergency_status_code parity).
    const auto dc_status_code = error_status_code.value_or(charger_stop ? dt::DC_EVSEStatusCode::EVSE_Shutdown
                                                                        : dt::DC_EVSEStatusCode::EVSE_Ready);
    const auto notification = charger_stop ? dt::EVSENotification::StopCharging : dt::EVSENotification::None;
    const auto set_status = [&]() {
        if (is_dc) {
            auto& status = res.dc_evse_status.emplace();
            status.notification = notification;
            status.notification_max_delay = 0;
            status.isolation_status = isolation_status;
            status.status_code = dc_status_code;
        } else {
            res.ac_evse_status = make_ac_evse_status();
            res.ac_evse_status->notification = notification;
            res.ac_evse_status->rcd = rcd_error;
        }
    };
    set_status();

    // Echoed SAScheduleTupleID must match the advertised one [V2G2-479].
    if (req.sa_schedule_tuple_id != advertised_sa_schedule_tuple_id) {
        res.response_code = dt::ResponseCode::FAILED_TariffSelectionInvalid;
        return res;
    }

    if (req.charge_progress == dt::ChargeProgress::Start) {
        // AC PowerDelivery(Start) requires a ChargingProfile.
        if (not is_dc and not req.charging_profile.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_ChargingProfileInvalid;
            return res;
        }
        // A provided ChargingProfile must stay within the advertised PMax [V2G2-224/225] (AC and DC).
        if (req.charging_profile.has_value() and
            not charging_profile_within_limits(req.charging_profile.value(), sa_schedule_list,
                                               advertised_sa_schedule_tuple_id)) {
            res.response_code = dt::ResponseCode::FAILED_ChargingProfileInvalid;
            return res;
        }
        // [V2G2-480] the response carries FAILED_PowerDeliveryNotApplied "if the EVSE is not able to
        // deliver energy". A latched module error (send_error) is exactly that state, and the DC status
        // code is the only place the EVSE can say so -- hence DC only, as in EvseV2G. Checked after the
        // request-validation legs above so a malformed request still gets its own specific code.
        //
        // EvseV2G phrases the same rule as "Start and EVSEStatusCode != EVSE_Ready" (iso_server.cpp),
        // which also catches the EVSE_Shutdown of a charger-initiated stop. We deliberately do not: a
        // stop REQUEST is not an inability to deliver energy. [V2G2-679] only says the EV "should" stop
        // on EVSENotification StopCharging, and NotificationMaxDelay grants it a window to do so -- the
        // STOP_CHARGING guard enforces the stop once that window closes (charger_stop_ignored).
        if (is_dc and error_status_code.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_PowerDeliveryNotApplied;
            return res;
        }
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery");
}

Result PowerDelivery::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.present_voltage = control->voltage;
            m_ctx.present_current = control->current;
        } else if (const auto* closed = m_ctx.get_control_event<d20::ClosedContactor>()) {
            m_ctx.ac_contactor_closed = static_cast<bool>(*closed);
            if (m_ctx.ac_contactor_closed and saved_ac_start_req.has_value()) {
                m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);
                const auto res =
                    handle_request(saved_ac_start_req.value(), m_ctx.get_session_id(), false,
                                   m_ctx.sa_schedule_tuple_id, m_ctx.isolation_level(), m_ctx.charger_stop_requested,
                                   m_ctx.sa_schedule_list, m_ctx.error_status_code(), m_ctx.rcd_error());
                saved_ac_start_req.reset();
                m_ctx.respond(res);
                if (res.response_code >= dt::ResponseCode::FAILED) {
                    m_ctx.session_stopped = true;
                    return {};
                }
                return m_ctx.create_state<ChargingStatus>();
            }
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::CONTACTOR) {
            logf_warning("PowerDelivery contactor timeout reached, terminating session");
            message_2::PowerDeliveryResponse res;
            res.header.session_id = m_ctx.get_session_id();
            res.ac_evse_status = make_ac_evse_status();
            res.response_code = dt::ResponseCode::FAILED_ContactorError;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    // An EV aborting sends SessionStopReq; hand it to SessionStop for a clean SessionStopRes.
    if (m_ctx.peek_request_type() == message_2::Type::SessionStopReq) {
        return m_ctx.create_state<SessionStop>();
    }

    const auto variant = m_ctx.pull_request();

    const auto req = variant->get_if<message_2::PowerDeliveryRequest>();
    if (req == nullptr) {
        logf_warning("Expected PowerDeliveryReq! But code type id: %d", variant->get_type());
        // [V2G2-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
        respond_sequence_error(m_ctx, *variant);
        m_ctx.session_stopped = true;
        return {};
    }

    // The request must echo the assigned SessionID [V2G2-388]; a mismatch is answered with
    // PowerDeliveryRes/FAILED_UnknownSession and terminates the session.
    if (reject_unknown_session(m_ctx, *variant)) {
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

    const bool is_dc = m_ctx.dc_charging;

    if (req->charge_progress == dt::ChargeProgress::Start) {
        // IEC 61851-23:2023 CC.3.5.3: the EV was told at ChargeParameterDiscovery that no energy is
        // available, but asks to start charging anyway. Refuse instead of entering a charge loop the
        // charger cannot serve (EvseV2G iso_server.cpp:1765). AllowEvToIgnorePause deliberately lets it
        // through -- that is what the mode is for. DC only (EvseV2G parity): an AC EV is never told to
        // pause (the CPD notification is DC-only in both stacks), so it may start and is throttled by the
        // 0 A limit in every ChargingStatusRes instead.
        if (is_dc and (m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck or
                       m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::AfterCableCheckPreCharge)) {
            logf_warning("The EV did not pause the session although the EVSE signalled that no energy is "
                         "available; answering PowerDeliveryRes/FAILED");
            auto res = handle_request(*req, m_ctx.get_session_id(), is_dc, m_ctx.sa_schedule_tuple_id,
                                      m_ctx.isolation_level(), m_ctx.charger_stop_requested, m_ctx.sa_schedule_list,
                                      m_ctx.error_status_code(), m_ctx.rcd_error());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
        // Record that charging was started so a later Renegotiate is accepted [V2G2-812].
        m_ctx.power_delivery_started = true;

        if (not is_dc) {
            if (m_ctx.ac_contactor_closed) {
                // Resuming after a renegotiation: the contactor never re-opened, so no fresh
                // ClosedContactor confirmation will arrive. Respond OK immediately.
                const auto res = handle_request(*req, m_ctx.get_session_id(), false, m_ctx.sa_schedule_tuple_id,
                                                m_ctx.isolation_level(), m_ctx.charger_stop_requested,
                                                m_ctx.sa_schedule_list, m_ctx.error_status_code(), m_ctx.rcd_error());
                m_ctx.respond(res);
                if (res.response_code >= dt::ResponseCode::FAILED) {
                    m_ctx.session_stopped = true;
                    return {};
                }
                return m_ctx.create_state<ChargingStatus>();
            }
            // AC: close the contactor first, respond once it is confirmed closed.
            saved_ac_start_req = *req;
            m_ctx.feedback.signal(session::feedback::Signal::AC_CLOSE_CONTACTOR);
            m_ctx.start_timeout(d20::TimeoutType::CONTACTOR, CONTACTOR_TIMEOUT_MS);
            return {};
        }

        const auto res = handle_request(*req, m_ctx.get_session_id(), true, m_ctx.sa_schedule_tuple_id,
                                        m_ctx.isolation_level(), m_ctx.charger_stop_requested, m_ctx.sa_schedule_list,
                                        m_ctx.error_status_code(), m_ctx.rcd_error());
        m_ctx.respond(res);
        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }
        return m_ctx.create_state<CurrentDemand>();
    }

    // Renegotiate: acknowledge and return to ChargeParameterDiscovery (EvseV2G iso_server.cpp:1596).
    if (req->charge_progress == dt::ChargeProgress::Renegotiate) {
        // [V2G2-812]: a Renegotiate received before any PowerDelivery(Start) is illegal. Answer FAILED
        // (with a well-formed EVSE status) and terminate the session.
        if (not m_ctx.power_delivery_started) {
            logf_warning("PowerDelivery(Renegotiate) received before any Start; answering FAILED [V2G2-812]");
            auto res = handle_request(*req, m_ctx.get_session_id(), is_dc, m_ctx.sa_schedule_tuple_id,
                                      m_ctx.isolation_level(), m_ctx.charger_stop_requested, m_ctx.sa_schedule_list,
                                      m_ctx.error_status_code(), m_ctx.rcd_error());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
            return {};
        }
        const auto res = handle_request(*req, m_ctx.get_session_id(), is_dc, m_ctx.sa_schedule_tuple_id,
                                        m_ctx.isolation_level(), m_ctx.charger_stop_requested, m_ctx.sa_schedule_list,
                                        m_ctx.error_status_code(), m_ctx.rcd_error());
        m_ctx.respond(res);
        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }
        return m_ctx.create_state<ChargeParameterDiscovery>();
    }

    // Stop. CHARGE_LOOP_FINISHED is DC-only, mirroring the is_dc_charger gate EvseV2G puts on it
    // (iso_server.cpp:1784): it tells EvseManager to switch the DC power supply off and stop the
    // over-voltage monitor. An AC session just opens the contactor (below).
    if (is_dc) {
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
    }

    const auto res = handle_request(*req, m_ctx.get_session_id(), is_dc, m_ctx.sa_schedule_tuple_id,
                                    m_ctx.isolation_level(), m_ctx.charger_stop_requested, m_ctx.sa_schedule_list,
                                    m_ctx.error_status_code(), m_ctx.rcd_error());
    m_ctx.respond(res);
    if (res.response_code >= dt::ResponseCode::FAILED) {
        m_ctx.session_stopped = true;
        return {};
    }

    // [V2G2-913] After PowerDelivery(Stop) the EV must signal CP State B before the next request;
    // arm the CP State B gate for the following WeldingDetection/SessionStop ([V2G2-920]..[V2G2-922]).
    m_ctx.power_delivery_stopped = true;

    if (is_dc) {
        // With the contactor open the isolation status verified by the previous cable check no longer
        // holds: a post-stop restart (WeldingDetection -> ChargeParameterDiscovery -> CableCheck) must
        // re-run the physical isolation test. Renegotiation (above) keeps the contactor closed, so it
        // keeps cable_check_done and still skips the re-test (ISO 15118-2 §8.7.4.3 NOTE 1).
        m_ctx.cable_check_done = false;
        m_ctx.cable_check_fault = false;
        m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
        return m_ctx.create_state<WeldingDetection>();
    }
    m_ctx.ac_contactor_closed = false;
    m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
    return m_ctx.create_state<SessionStop>();
}

} // namespace iso15118::d2::state
