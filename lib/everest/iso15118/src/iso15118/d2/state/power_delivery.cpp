// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/power_delivery.hpp>

#include <algorithm>

#include <iso15118/d2/state/ac_charge_loop.hpp>
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/dc_charge_loop.hpp>
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/d2/state/welding_detection.hpp>

#include <iso15118/detail/d2/state/power_delivery.hpp>
#include <iso15118/detail/d2/state/sequence_error.hpp>
#include <iso15118/detail/d2/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::state {

namespace {
// V2G_SECC_Msg_Performance_Time(PowerDeliveryRes) = 4,5 s (Table 109), started on
// PowerDeliveryReq(Start) per [V2G2-858]. Not [V2G2-860]'s 3 s, which is the deadline for the
// charger to *close* the contactor -- a different quantity, and the value this used to carry.
constexpr uint32_t CONTACTOR_PERFORMANCE_TIME_MS = 4500;
} // namespace

bool charging_profile_within_limits(const dt::ChargingProfile& profile, const dt::SAScheduleList& sa_schedule_list,
                                    uint8_t advertised_sa_schedule_tuple_id) {
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

    // [V2G2-366]: a module-reported fault wins over the EVSE_Ready this state would otherwise claim,
    // which per Table 98 means the charging procedure is running (EvseV2G parity).
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
        if (not is_dc and not req.charging_profile.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_ChargingProfileInvalid;
            return res;
        }
        if (req.charging_profile.has_value() and
            not charging_profile_within_limits(req.charging_profile.value(), sa_schedule_list,
                                               advertised_sa_schedule_tuple_id)) {
            res.response_code = dt::ResponseCode::FAILED_ChargingProfileInvalid;
            return res;
        }
        // [V2G2-480]: FAILED_PowerDeliveryNotApplied when the EVSE cannot deliver energy. A latched module
        // error is exactly that, and only the DC status code can say so -- hence DC only, as in EvseV2G.
        // EvseV2G phrases the rule as "Start and EVSEStatusCode != EVSE_Ready", which also catches the
        // EVSE_Shutdown of a charger-initiated stop. We deliberately do not: a stop REQUEST is not an
        // inability to deliver energy, and the STOP_CHARGING guard enforces it once the [V2G2-679] window closes.
        if (is_dc and error_status_code.has_value()) {
            res.response_code = dt::ResponseCode::FAILED_PowerDeliveryNotApplied;
            return res;
        }
    }

    res.response_code = dt::ResponseCode::OK;
    return res;
}

namespace {

void report_power_delivery_parameter(Context& ctx, const message_2::PowerDeliveryRequest& req) {
    if (not req.dc_ev_power_delivery_parameter.has_value()) {
        return;
    }
    const auto& param = req.dc_ev_power_delivery_parameter.value();
    ctx.report_ev_status(param.dc_ev_status);

    // No remaining times here, so they stay absent and the module does not overwrite the charge loop's
    // values with zeroes.
    session::feedback::DcEvChargeProgress progress{};
    progress.charging_complete = param.charging_complete;
    progress.bulk_charging_complete = param.bulk_charging_complete;
    ctx.report_charge_progress(progress);
}

// Runs on receipt rather than on response, so it happens whether the answer goes out immediately or
// waits for the AC contactor.
void mark_power_delivery_started(Context& ctx) {
    ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
    // Record that charging was started so a later Renegotiate is accepted [V2G2-812].
    ctx.set_power_delivery_started();
}

bool respond(Context& ctx, const message_2::PowerDeliveryRequest& req, bool is_dc) {
    const auto res = handle_request(req, ctx.get_session_id(), is_dc, ctx.session().sa_schedule_tuple_id,
                                    ctx.isolation_level(), ctx.evse().charger_stop_requested,
                                    ctx.session().sa_schedule_list, ctx.error_status_code(), ctx.rcd_error());
    ctx.respond(res);
    if (res.response_code >= dt::ResponseCode::FAILED) {
        ctx.session_stopped = true;
        return false;
    }
    return true;
}

Result renegotiate(Context& ctx, const message_2::PowerDeliveryRequest& req, bool is_dc) {
    // [V2G2-812]: a Renegotiate before any PowerDelivery(Start) is illegal.
    if (not ctx.session().power_delivery_started) {
        logf_warning("PowerDelivery(Renegotiate) received before any Start; answering FAILED [V2G2-812]");
        auto res = handle_request(req, ctx.get_session_id(), is_dc, ctx.session().sa_schedule_tuple_id,
                                  ctx.isolation_level(), ctx.evse().charger_stop_requested,
                                  ctx.session().sa_schedule_list, ctx.error_status_code(), ctx.rcd_error());
        res.response_code = dt::ResponseCode::FAILED;
        ctx.respond(res);
        ctx.session_stopped = true;
        return {};
    }

    if (not respond(ctx, req, is_dc)) {
        return {};
    }
    return ctx.create_state<ChargeParameterDiscovery>();
}

} // namespace

void AcPowerDelivery::enter() {
    logf_debug("Enter state: AcPowerDelivery");
}

Result AcPowerDelivery::on_event(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control = m_ctx.get_control_event<d20::PresentVoltageCurrent>()) {
            m_ctx.set_present_values(control->voltage, control->current);
        } else if (const auto* closed = m_ctx.get_control_event<d20::ClosedContactor>()) {
            m_ctx.set_contactor_closed(static_cast<bool>(*closed));
            if (m_ctx.evse().ac_contactor_closed and saved_start_req.has_value()) {
                m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);
                const auto req = saved_start_req.value();
                saved_start_req.reset();
                if (not respond(m_ctx, req, false)) {
                    return {};
                }
                return m_ctx.create_state<AcChargeLoopStart>();
            }
        }
        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::CONTACTOR) {
            // [V2G2-862]: the performance time expired with the contactor still open.
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

    return {};
}

Result AcPowerDelivery::on_request(const message_2::Variant& received) {
    const auto type = received.get_type();
    if (type == message_2::Type::PowerDeliveryReq) {
        const auto& req = received.get<message_2::PowerDeliveryRequest>();

        // The contactor gate ([V2G2-858]): hold the response here rather than in the shared action, so that
        // action stays a plain function.
        if (req.charge_progress == dt::ChargeProgress::Start and not m_ctx.evse().ac_contactor_closed) {
            report_power_delivery_parameter(m_ctx, req);
            mark_power_delivery_started(m_ctx);
            saved_start_req = req;
            m_ctx.feedback.signal(session::feedback::Signal::AC_CLOSE_CONTACTOR);
            m_ctx.start_timeout(d20::TimeoutType::CONTACTOR, CONTACTOR_PERFORMANCE_TIME_MS);
            return {};
        }

        return process_ac_power_delivery(m_ctx, req);
    } else {
        logf_warning("Expected PowerDeliveryReq! But got type id: %d", received.get_type());
        respond_sequence_error(m_ctx, received.get_type());
        return {};
    }
}

// An action on a transition, not a wait state. The caller has matched the type against its own
// accepted set, and on AC has already satisfied the contactor gate.
Result process_ac_power_delivery(Context& m_ctx, const message_2::PowerDeliveryRequest& req) {
    report_power_delivery_parameter(m_ctx, req);

    if (req.charge_progress == dt::ChargeProgress::Start) {
        mark_power_delivery_started(m_ctx);
        if (not respond(m_ctx, req, false)) {
            return {};
        }
        return m_ctx.create_state<AcChargeLoopStart>();
    }

    if (req.charge_progress == dt::ChargeProgress::Renegotiate) {
        return renegotiate(m_ctx, req, false);
    }

    // CHARGE_LOOP_FINISHED is DC-only, mirroring the is_dc_charger gate EvseV2G puts on it.
    if (not respond(m_ctx, req, false)) {
        return {};
    }

    // [V2G2-913]: arm the CP State B gate for the following SessionStop ([V2G2-920]..[V2G2-922]).
    m_ctx.set_power_delivery_stopped();
    m_ctx.set_contactor_closed(false);
    m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
    return m_ctx.create_state<SessionStop>();
}

Result process_dc_power_delivery(Context& m_ctx, const message_2::PowerDeliveryRequest& req) {
    report_power_delivery_parameter(m_ctx, req);

    if (req.charge_progress == dt::ChargeProgress::Start) {
        // IEC 61851-23:2023 CC.3.5.3: the EV was told no energy is available but asks to start anyway.
        // AllowEvToIgnorePause deliberately lets it through -- that is what the mode is for. DC only: an AC
        // EV is never told to pause, and is throttled by the 0 A limit in every ChargingStatusRes instead.
        if (m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::BeforeCableCheck or
            m_ctx.session_config.no_energy_pause == d20::NoEnergyPauseMode::AfterCableCheckPreCharge) {
            logf_warning("The EV did not pause the session although the EVSE signalled that no energy is "
                         "available; answering PowerDeliveryRes/FAILED");
            auto res = handle_request(req, m_ctx.get_session_id(), true, m_ctx.session().sa_schedule_tuple_id,
                                      m_ctx.isolation_level(), m_ctx.evse().charger_stop_requested,
                                      m_ctx.session().sa_schedule_list, m_ctx.error_status_code(), m_ctx.rcd_error());
            res.response_code = dt::ResponseCode::FAILED;
            m_ctx.respond(res);
            m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
            m_ctx.session_stopped = true;
            return {};
        }

        mark_power_delivery_started(m_ctx);

        if (not respond(m_ctx, req, true)) {
            return {};
        }
        // Due once from here on, not once per loop-state instance -- the loop state is rebuilt around a
        // metering receipt.
        m_ctx.clear_charge_loop_started();
        return m_ctx.create_state<DcChargeLoop>();
    }

    if (req.charge_progress == dt::ChargeProgress::Renegotiate) {
        return renegotiate(m_ctx, req, true);
    }

    // Tells EvseManager to switch the DC supply off and stop the over-voltage monitor.
    m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);

    if (not respond(m_ctx, req, true)) {
        return {};
    }

    // [V2G2-913]: arm the CP State B gate for the following WeldingDetection ([V2G2-920]..[V2G2-922]).
    m_ctx.set_power_delivery_stopped();

    // With the contactor open the verified isolation no longer holds, so a post-stop restart must re-run
    // the physical test. Renegotiation keeps the contactor closed and so keeps cable_check_done (NOTE 1
    // of 8.7.4.3).
    m_ctx.invalidate_cable_check();
    m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
    return m_ctx.create_state<PostCharge>();
}

} // namespace iso15118::d2::state
