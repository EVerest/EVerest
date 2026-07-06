// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <optional>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/dc_welding_detection.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/d20/state/session_stop.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/power_delivery.hpp>

namespace iso15118::ev::d20::state {

namespace {

message_20::PowerDeliveryRequest
make_request(const SessionId& session, message_20::datatypes::Progress charge_progress,
             std::optional<message_20::datatypes::Processing> processing = std::nullopt) {
    message_20::PowerDeliveryRequest req;
    setup_header(req.header, session);
    req.processing = processing.value_or(message_20::datatypes::Processing::Finished);
    req.charge_progress = charge_progress;
    // power_profile + channel_selection deliberately left nullopt
    return req;
}

} // namespace

void PowerDelivery::enter() {
    m_ctx.respond(make_request(m_ctx.get_session(), m_charge_progress));
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::PowerDeliveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    using Progress = message_20::datatypes::Progress;
    using ServiceCategory = message_20::datatypes::ServiceCategory;
    const auto service = m_ctx.selected_service();
    const bool is_ac = service == ServiceCategory::AC;
    const bool is_ac_der_iec = service == ServiceCategory::AC_DER_IEC;
    switch (m_charge_progress) {
    case Progress::Start:
        if (is_ac_der_iec) {
            return m_ctx.create_state<AC_DER_IEC_ChargeLoop>();
        }
        if (is_ac) {
            return m_ctx.create_state<AC_ChargeLoop>();
        }
        return m_ctx.create_state<DC_ChargeLoop>();
    case Progress::Stop:
        if (is_ac or is_ac_der_iec) {
            return m_ctx.create_state<SessionStop>();
        }
        return m_ctx.create_state<DC_WeldingDetection>();
    case Progress::Standby:
        logf_info("PowerDelivery Standby accepted; staying in PowerDelivery");
        return {};
    case Progress::ScheduleRenegotiation:
        logf_info("PowerDelivery ScheduleRenegotiation accepted; staying in PowerDelivery");
        return {};
    }
    return {};
}

} // namespace iso15118::ev::d20::state
