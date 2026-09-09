// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/charging_status.hpp>
#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message/d2/ac_charging_status.hpp>
#include <iso15118/message/d2/power_delivery.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

void ChargingStatus::enter() {
}

Result ChargingStatus::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    // AC charge loop: EV polls with ChargingStatusReq
    if (const auto req = variant->get_if<msg::AC_ChargingStatusRequest>()) {
        msg::AC_ChargingStatusResponse res;
        setup_header(res.header, m_ctx.session);
        res.evse_id = m_ctx.session_config.evse_id;
        res.sa_schedule_tuple_id = selected_sa_id;
        // [V2G2-691] ReceiptRequired shall be false for EIM (ExternalPayment)
        res.receipt_required = false;
        // EVSEMaxCurrent derived from the configured rating (power / voltage),
        // consistent with ChargeParameterDiscovery.
        const int32_t nominal_v = m_ctx.session_config.evse_nominal_voltage_v;
        const int32_t phases = (m_ctx.session_config.evse_phase_count > 0) ? m_ctx.session_config.evse_phase_count : 1;
        // Per-phase max current = total power / (phases * voltage).
        const int16_t max_current_a =
            (nominal_v > 0) ? static_cast<int16_t>(m_ctx.session_config.evse_max_power_w / (nominal_v * phases)) : 0;
        res.evse_max_current = dt::PhysicalValue{max_current_a, 0, dt::UnitSymbol::A};
        res.ac_evse_status.rcd = false;
        // Mid-charge deauthorization (RFID/remote revoked): tell the EV to stop
        // via EVSENotification=StopCharging [V2G2-845] so it ramps down and sends
        // PowerDeliveryReq{Stop} -> clean SessionStop. The contactor is already
        // open (the ChargeSupervisor energy gate), so power is off regardless.
        const bool authorized = m_ctx.feedback.is_authorized();
        res.ac_evse_status.notification_max_delay = authorized ? 0 : 10;
        res.ac_evse_status.evse_notification =
            authorized ? dt::EvseNotification::None : dt::EvseNotification::StopCharging;
        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);
        return {}; // stay in ChargingStatus
    }

    // During the AC charge loop the EV may send a PowerDeliveryReq. Its
    // ChargeProgress decides what happens — mirroring EvseV2G handle_iso_power_delivery:
    //   Stop        -> open contactor, end session (SessionStop)
    //   Renegotiate -> KEEP the contactor closed, go back to ChargeParameterDiscovery
    //                  so the EV's follow-up ChargeParameterDiscoveryReq is handled
    //                  [V2G2-813]. Treating Renegotiate as Stop (the old behavior)
    //                  tore the session to SessionStop, which then rejected the EV's
    //                  ChargeParameterDiscoveryReq -> "Unknown code type id: 13".
    if (const auto req = variant->get_if<msg::PowerDeliveryRequest>()) {
        msg::PowerDeliveryResponse res;
        setup_header(res.header, m_ctx.session);
        dt::AcEvseStatus ac_status;
        ac_status.rcd = false;
        ac_status.notification_max_delay = 0;
        ac_status.evse_notification = dt::EvseNotification::None;
        res.evse_status = ac_status;
        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);

        if (req->charge_progress == dt::ChargeProgress::Renegotiate) {
            // Contactor stays closed; charging continues through renegotiation.
            return m_ctx.create_state<ChargeParameterDiscovery>();
        }

        // Stop (or any non-Renegotiate): end charging.
        m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
        m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
        return m_ctx.create_state<SessionStop>();
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
