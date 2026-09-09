// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/charging_status.hpp>
#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/d2/state/session_stop.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/message/d2/power_delivery.hpp>
#include <iso15118/session/feedback.hpp>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

void PowerDelivery::enter() {
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::PowerDeliveryRequest>()) {
        msg::PowerDeliveryResponse res;
        setup_header(res.header, m_ctx.session);

        // Build AC EVSE status
        dt::AcEvseStatus ac_status;
        ac_status.rcd = false;
        ac_status.notification_max_delay = 0;
        ac_status.evse_notification = dt::EvseNotification::None;
        res.evse_status = ac_status;

        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);

        const auto progress = req->charge_progress;
        const auto sa_id = req->sa_schedule_tuple_id;

        if (progress == dt::ChargeProgress::Start) {
            // Close contactor and enter AC charge loop
            m_ctx.feedback.signal(session::feedback::Signal::AC_CLOSE_CONTACTOR);
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_STARTED);
            return m_ctx.create_state<ChargingStatus>(sa_id);
        } else if (progress == dt::ChargeProgress::Stop) {
            // Open contactor and terminate session
            m_ctx.feedback.signal(session::feedback::Signal::CHARGE_LOOP_FINISHED);
            m_ctx.feedback.signal(session::feedback::Signal::AC_OPEN_CONTACTOR);
            return m_ctx.create_state<SessionStop>();
        } else {
            // Renegotiate: go back to charge parameter discovery
            return m_ctx.create_state<ChargeParameterDiscovery>();
        }
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
