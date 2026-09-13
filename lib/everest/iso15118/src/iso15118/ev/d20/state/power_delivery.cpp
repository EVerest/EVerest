// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
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
#include <iso15118/message/schedule_exchange.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;

// [V2G20-1521]: in Scheduled mode PowerDeliveryReq(Start) names the ScheduleTupleID the EV picked
// in ScheduleExchange. One entry spanning the schedule at the EV's maximum charge power; the
// per-line peers stay unset because the profile states one aggregate.
std::optional<dt::PowerProfile> make_scheduled_power_profile(const Context& ctx, uint64_t time_anchor) {
    const auto tuple_id = ctx.selected_schedule_tuple_id();
    if (not tuple_id.has_value()) {
        logf_warning("Scheduled PowerDelivery has no ScheduleTupleID to reference; omitting the power profile");
        return std::nullopt;
    }

    dt::Scheduled_EVPPTControlMode mode{};
    mode.selected_schedule = *tuple_id;

    dt::PowerProfile profile{};
    profile.time_anchor = time_anchor;
    profile.control_mode = mode;
    profile.entries.push_back(
        dt::PowerScheduleEntry{dt::SCHEDULED_POWER_DURATION_S,
                               dt::from_float(ctx.is_ac_family() ? ctx.get_ac_params().max_charge_power
                                                                 : ctx.get_dc_params().max_charge_power),
                               std::nullopt, std::nullopt});
    return profile;
}

message_20::PowerDeliveryRequest make_request(Context& ctx, dt::Progress charge_progress,
                                              std::optional<dt::Processing> processing = std::nullopt) {
    message_20::PowerDeliveryRequest req;
    setup_header(req.header, ctx.get_session());
    req.processing = processing.value_or(dt::Processing::Finished);
    req.charge_progress = charge_progress;
    // channel_selection deliberately left nullopt
    if (charge_progress == dt::Progress::Start and ctx.selected_control_mode() == dt::ControlMode::Scheduled) {
        req.power_profile = make_scheduled_power_profile(ctx, req.header.timestamp);
    }
    return req;
}

} // namespace

void PowerDelivery::enter() {
    m_ctx.send_request(make_request(m_ctx, m_charge_progress));
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::PowerDeliveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    using Progress = message_20::datatypes::Progress;
    using ServiceCategory = message_20::datatypes::ServiceCategory;
    const bool is_ac = m_ctx.is_ac_family();
    const bool is_ac_der_iec = m_ctx.selected_service() == ServiceCategory::AC_DER_IEC;
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
        if (is_ac) {
            return m_ctx.create_state<SessionStop>();
        }
        return m_ctx.create_state<DC_WeldingDetection>();
    // The EV drives neither loop; stopping keeps the declared disposition honest.
    case Progress::Standby:
        logf_warning("PowerDelivery Standby accepted, but the EV drives no standby loop; stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    case Progress::ScheduleRenegotiation:
        logf_warning("PowerDelivery ScheduleRenegotiation accepted, but the EV drives no renegotiation; "
                     "stopping the session");
        m_ctx.stop_session();
        return Result::stopping();
    }

    logf_error("PowerDelivery reached an unhandled charge progress value; stopping the session");
    m_ctx.stop_session();
    return Result::stopping();
}

} // namespace iso15118::ev::d20::state
