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
                                                  const dt::SessionId& session_id) {
    message_din::PowerDeliveryResponse res;
    setup_header(res.header, session_id);

    // PowerDeliveryRes requires an EVSEStatus (DIN is DC-only); it is mandatory even on a
    // FAILED_UnknownSession response, so populate it before the SessionID check below.
    dt::DcEvseStatus status;
    status.evse_status_code = dt::DcEvseStatusCode::EVSE_Ready;
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

    return response_with_code(res, dt::ResponseCode::OK);
}

void PowerDelivery::enter() {
    m_ctx.log.enter_state("PowerDelivery");
}

Result PowerDelivery::feed(Event ev) {
    if (ev == Event::TIMEOUT) {
        // [V2G-DC-969/989]: the SECC PowerDelivery supervision timer armed at first PreChargeReq spans
        // into PowerDelivery; on expiry terminate the session.
        const auto* timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            m_ctx.log("PowerDelivery timeout reached, terminating session");
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
        const auto res = handle_request(*req, m_ctx.get_session_id());
        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
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

    m_ctx.log("expected PowerDeliveryReq! But code type id: %d", variant->get_type());
    // [V2G-DC-539]: answer with the received-type response carrying FAILED_SequenceError, then close.
    respond_sequence_error(m_ctx, *variant);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::din::state
