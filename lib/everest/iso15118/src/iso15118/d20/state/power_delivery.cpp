// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_charge_loop.hpp>
#include <iso15118/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/d20/state/dc_charge_loop.hpp>
#include <iso15118/d20/state/dc_welding_detection.hpp>
#include <iso15118/d20/state/power_delivery.hpp>
#include <iso15118/d20/state/session_stop.hpp>
#include <iso15118/d20/timeout.hpp>

#include <optional>
#include <variant>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/dc_pre_charge.hpp>
#include <iso15118/detail/d20/state/power_delivery.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::state {

// Secc performance timer for PowerDelivery is 1.5s.
// 100ms is resevered for polling timeout and sending the response.
constexpr uint32_t AC_CLOSE_CONTACTOR_TIMEOUT = 1400;

namespace dt = message_20::datatypes;

namespace {

const dt::Scheduled_EVPPTControlMode* scheduled_profile(const message_20::PowerDeliveryRequest& req) {
    if (req.charge_progress != dt::Progress::Start or not req.power_profile.has_value()) {
        return nullptr;
    }
    return std::get_if<dt::Scheduled_EVPPTControlMode>(&req.power_profile->control_mode);
}

// Checks the scheduled-mode EVPowerProfile of a PowerDeliveryReq(Start) against the ScheduleExchangeRes.
// [V2G20-1070]: anything else is accepted, so a dynamic-mode profile is never rejected here.
std::optional<dt::ResponseCode> power_profile_error(const message_20::PowerDeliveryRequest& req,
                                                    const d20::Session& session) {
    const auto* scheduled = scheduled_profile(req);
    if (scheduled == nullptr) {
        return std::nullopt;
    }

    const auto offered = session.offered_schedules.find(scheduled->selected_schedule);
    if (offered == session.offered_schedules.end()) {
        // [V2G20-479] names FAILED_TariffSelectionInvalid, which the -20 schema does not define.
        return dt::ResponseCode::FAILED_ScheduleSelectionInvalid;
    }

    // [V2G20-478], [V2G20-1559]: each entry against the offered power of every slot it overlaps. The EV's
    // TimeAnchor is in microseconds (Table 103). Only charge power is checked: no discharge schedule is offered.
    const auto profile =
        PowerTimeline::from(req.power_profile->time_anchor / MICROSECONDS_PER_SECOND, req.power_profile->entries);
    uint64_t entry_start_s = profile.time_anchor_s;
    for (const auto& entry : profile.entries) {
        const uint64_t entry_end_s = entry_start_s + entry.duration_s;
        if (offered->second.exceeded_by(entry_start_s, entry_end_s, entry.power_w)) {
            return dt::ResponseCode::FAILED_EVPowerProfileInvalid;
        }
        entry_start_s = entry_end_s;
    }

    return std::nullopt;
}

// [V2G20-1944] for a confirmed tolerance. A declined one is FAILED only if the SECC intends to stop
// ([V2G20-1945]) and WARNING if it intends to renegotiate ([V2G20-1946]); the offer carries no PowerTolerance to
// enforce, so the SECC does neither and the warning lets the EV charge on.
dt::ResponseCode power_tolerance_response_code(const message_20::PowerDeliveryRequest& req) {
    const auto* scheduled = scheduled_profile(req);
    if (scheduled == nullptr or not scheduled->power_tolerance_acceptance.has_value()) {
        return dt::ResponseCode::OK;
    }
    if (*scheduled->power_tolerance_acceptance == dt::PowerToleranceAcceptance::NotConfirmed) {
        return dt::ResponseCode::WARNING_PowerToleranceNotConfirmed;
    }
    return dt::ResponseCode::OK_PowerToleranceConfirmed;
}

} // namespace

message_20::PowerDeliveryResponse handle_request(const message_20::PowerDeliveryRequest& req,
                                                 const d20::Session& session, bool contactor_error,
                                                 bool shutdown_requested) {

    message_20::PowerDeliveryResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        set_response_code(res, dt::ResponseCode::FAILED_UnknownSession);
        return res;
    }

    if (contactor_error) {
        set_response_code(res, dt::ResponseCode::FAILED_ContactorError);
        return res;
    }

    if (const auto error = power_profile_error(req, session)) {
        set_response_code(res, *error);
        return res;
    }

    if (shutdown_requested) {
        auto& notification = res.status.emplace();
        notification.notification = dt::EvseNotification::Terminate;
        notification.notification_max_delay = 0;
    }

    // TODO(sl): Check Req ChannelSelection

    // Todo(sl): Add standby feature and define as everest module config
    if (req.charge_progress == dt::Progress::Standby) {
        set_response_code(res, dt::ResponseCode::WARNING_StandbyNotAllowed);
        return res;
    }

    set_response_code(res, power_tolerance_response_code(req));
    return res;
}

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery");
}

Result PowerDelivery::feed(Event ev) {

    const auto selected_energy_service = m_ctx.session.get_selected_services().selected_energy_service;
    if (ev == Event::CONTROL_MESSAGE) {

        if (const auto* control_data = m_ctx.get_control_event<PresentVoltageCurrent>()) {
            present_voltage = control_data->voltage;
        } else if (const auto* control_data = m_ctx.get_control_event<ClosedContactor>()) {
            ac_connector_closed = *control_data;

            if (not ac_connector_closed) {
                if (not m_ctx.shutdown_requested()) {
                    logf_warning("Got ClosedContactor event, but contactor is not closed.  Waiting until the "
                                 "contactor is closed");
                } else if (previous_req.has_value()) {
                    // The contactor will never close now, and the timeout is what answers the saved
                    // PowerDeliveryReq. Cancel it and answer here, terminating rather than failing.
                    m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);
                    m_ctx.respond(handle_request(previous_req.value(), m_ctx.session, /*contactor_error=*/false,
                                                 /*shutdown_requested=*/true));
                    m_ctx.session_stopped = true;
                }
                return {};
            }

            m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);

            if (not previous_req.has_value()) {
                logf_warning("There is no power_delivery_req messages saved!");
                return {};
            }

            const auto& res = handle_request(previous_req.value(), m_ctx.session, false, false);
            m_ctx.respond(res);

            if (res.response_code >= dt::ResponseCode::FAILED) {
                m_ctx.session_stopped = true;
                return {};
            }

            if (m_ctx.session.is_ac_charger()) {
                return m_ctx.create_state<AC_ChargeLoop>();
            }
            if (m_ctx.session.is_ac_der_iec_charger()) {
                return m_ctx.create_state<AC_DER_IEC_ChargeLoop>();
            }
            if (m_ctx.session.is_ac_der_sae_charger()) {
                return m_ctx.create_state<AC_DER_SAE_ChargeLoop>();
            }
        }

        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto* const timeout = m_ctx.get_active_timeout();
        if (timeout != nullptr and *timeout == d20::TimeoutType::CONTACTOR) {
            logf_error("AC contactor is not closed within %ums, sending failure response code and stop the session",
                       AC_CLOSE_CONTACTOR_TIMEOUT);
            // TODO(SL): Check if value_or is the correct way
            const auto& res =
                handle_request(previous_req.value_or(message_20::PowerDeliveryRequest{}), m_ctx.session, true, false);
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (previous_req.has_value()) {
        // The saved PowerDeliveryReq(Start) is unanswered until the contactor closes, so no request may follow it.
        m_ctx.stop_timeout(d20::TimeoutType::CONTACTOR);
        m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
        send_sequence_error(variant->get_type(), m_ctx);
        m_ctx.session_stopped = true;
        return {};
    }

    if (const auto* const req = variant->get_if<message_20::PowerDeliveryRequest>()) {
        const auto shutdown_requested = m_ctx.shutdown_requested();

        // Checked before the AC contactor closes for a Start: a rejected request must not energize the outlet.
        const auto res = handle_request(*req, m_ctx.session, false, shutdown_requested);
        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.respond(res);
            m_ctx.session_stopped = true;
            return {};
        }

        if (req->power_profile.has_value()) {
            m_ctx.session.ev_power_profile = PowerTimeline::from(
                req->power_profile->time_anchor / MICROSECONDS_PER_SECOND, req->power_profile->entries);
        }

        if (not shutdown_requested) {

            if (req->charge_progress == dt::Progress::Start) {
                m_ctx.feedback.signal(session::feedback::Signal::SETUP_FINISHED);
            }

            if ((m_ctx.session.is_ac_charger() or m_ctx.session.is_ac_der_iec_charger() or
                 m_ctx.session.is_ac_der_sae_charger()) and
                not ac_connector_closed and req->charge_progress == dt::Progress::Start) {
                // Save req
                previous_req = *req;
                // Close the AC contactor so that charging can start
                m_ctx.feedback.signal(session::feedback::Signal::AC_CLOSE_CONTACTOR);
                m_ctx.start_timeout(d20::TimeoutType::CONTACTOR, AC_CLOSE_CONTACTOR_TIMEOUT);
                logf_info("Waiting for contactor is closed");
                return {};
            }
        }

        m_ctx.respond(res);

        if (shutdown_requested) {
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            if (m_ctx.session.is_ac_charger() or m_ctx.session.is_ac_der_iec_charger() or
                m_ctx.session.is_ac_der_sae_charger()) {
                m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
                return m_ctx.create_state<SessionStop>();
            }
            if (m_ctx.session.is_dc_charger()) {
                m_ctx.feedback.signal(session::feedback::Signal::DC_OPEN_CONTACTOR);
                return m_ctx.create_state<DC_WeldingDetection>();
            }
        }

        if (m_ctx.session.is_ac_charger()) {
            return m_ctx.create_state<AC_ChargeLoop>();
        }
        if (m_ctx.session.is_ac_der_iec_charger()) {
            return m_ctx.create_state<AC_DER_IEC_ChargeLoop>();
        }
        if (m_ctx.session.is_ac_der_sae_charger()) {
            return m_ctx.create_state<AC_DER_SAE_ChargeLoop>();
        }
        if (m_ctx.session.is_dc_charger()) {
            return m_ctx.create_state<DC_ChargeLoop>();
        }
        logf_warning("Expected selected_energy_service AC, AC_BPT, DC, DC_BPT! But code type id: %d",
                     static_cast<int>(selected_energy_service));

        m_ctx.session_stopped = true;
        return {};

    } else if (const auto* const req = variant->get_if<message_20::SessionStopRequest>()) {
        // [V2G20-2645]: a SessionStopReq before PowerDeliveryReq(Start) is answered per [V2G20-1632].
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        apply_session_stop_response(m_ctx, *req, res);

        return {};
    } else {
        logf_warning("Expected PowerDeliveryReq! But code type id: %d", variant->get_type());

        // Sequence Error
        const message_20::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

} // namespace iso15118::d20::state
