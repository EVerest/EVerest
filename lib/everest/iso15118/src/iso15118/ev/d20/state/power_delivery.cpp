// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <optional>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/ac_phase_split.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_der_iec_charge_loop.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_loop.hpp>
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

// [V2G20-1546]: every Start, Stop and Standby states an EVPowerProfile, in Scheduled mode under the
// ScheduleTupleID ScheduleExchange picked. One entry at `power` spanning the schedule, split across the selected
// connector for the AC family.
std::optional<dt::PowerProfile> make_power_profile(const Context& ctx, float power) {
    dt::PowerProfile profile{};
    if (ctx.selected_control_mode() == dt::ControlMode::Scheduled) {
        const auto tuple_id = ctx.schedule_tuple_id();
        if (not tuple_id.has_value()) {
            logf_warning("Scheduled PowerDelivery has no ScheduleTupleID to reference; omitting the power profile");
            return std::nullopt;
        }
        dt::Scheduled_EVPPTControlMode mode{};
        mode.selected_schedule = *tuple_id;
        profile.control_mode = mode;
    } else {
        profile.control_mode = dt::Dynamic_EVPPTControlMode{};
    }

    profile.time_anchor = ctx.secc_clock().now();
    dt::PowerScheduleEntry entry{dt::SCHEDULED_POWER_DURATION_S, {}, std::nullopt, std::nullopt};
    if (ctx.is_ac_family()) {
        emit_ac_limit(power, ctx.get_ac_params().phase_count, ctx.ac_connector(), entry.power, entry.power_l2,
                      entry.power_l3);
    } else {
        entry.power = dt::from_float(power);
    }
    profile.entries.push_back(entry);
    return profile;
}

// PowerDelivery(Start) enters the service's charge loop.
Result enter_charge_loop(Context& ctx, const der::SaeNegotiation& sae) {
    using ServiceCategory = message_20::datatypes::ServiceCategory;
    switch (ctx.selected_service()) {
    case ServiceCategory::AC:
    case ServiceCategory::AC_BPT:
        ctx.send_request(AC_ChargeLoop::make_request(ctx));
        return ctx.create_state<AC_ChargeLoop>();
    case ServiceCategory::AC_DER_IEC:
        ctx.send_request(AC_DER_IEC_ChargeLoop::make_request(ctx));
        return ctx.create_state<AC_DER_IEC_ChargeLoop>();
    case ServiceCategory::AC_DER_SAE:
        ctx.send_request(AC_DER_SAE_ChargeLoop::make_request(ctx, sae));
        return ctx.create_state<AC_DER_SAE_ChargeLoop>(sae);
    case ServiceCategory::DC:
    case ServiceCategory::DC_BPT:
    case ServiceCategory::MCS:
    case ServiceCategory::MCS_BPT:
        ctx.send_request(DC_ChargeLoop::make_request(ctx));
        return ctx.create_state<DC_ChargeLoop>();
    // Not reached: ServiceSelection stops the session for these.
    case ServiceCategory::WPT:
    case ServiceCategory::DC_ACDP:
    case ServiceCategory::DC_ACDP_BPT:
    case ServiceCategory::Internet:
    case ServiceCategory::ParkingStatus:
        break;
    }

    logf_error("PowerDelivery(Start) has no charge loop for energy transfer service %d; stopping the session",
               static_cast<int>(message_20::to_underlying_value(ctx.selected_service())));
    ctx.stop_session();
    return Result::stopping();
}

dt::Progress wire_progress(const PowerDelivery::Start&) {
    return dt::Progress::Start;
}

dt::Progress wire_progress(const PowerDelivery::Stop&) {
    return dt::Progress::Stop;
}

dt::Progress wire_progress(PowerDelivery::Standby) {
    return dt::Progress::Standby;
}

dt::Progress wire_progress(PowerDelivery::ScheduleRenegotiation) {
    return dt::Progress::ScheduleRenegotiation;
}

Result accepted(Context& ctx, const PowerDelivery::Start& start) {
    return enter_charge_loop(ctx, start.sae);
}

Result accepted(Context& ctx, const PowerDelivery::Stop& stop) {
    if (ctx.is_ac_family()) {
        return to_session_stop(ctx, stop.reason);
    }
    ctx.feedback.power_delivery_stopped();
    ctx.send_request(DC_WeldingDetection::make_request(dt::Processing::Ongoing));
    return ctx.create_state<DC_WeldingDetection>(stop.reason);
}

// The EV drives neither loop; stopping keeps the declared disposition honest.
Result accepted(Context& ctx, PowerDelivery::Standby) {
    logf_warning("PowerDelivery Standby accepted, but the EV drives no standby loop; stopping the session");
    ctx.stop_session();
    return Result::stopping();
}

Result accepted(Context& ctx, PowerDelivery::ScheduleRenegotiation) {
    logf_warning("PowerDelivery ScheduleRenegotiation accepted, but the EV drives no renegotiation; "
                 "stopping the session");
    ctx.stop_session();
    return Result::stopping();
}

// Start predicts the EV's maximum charge power, the same total AC_ChargeParameterDiscoveryReq advertised.
std::optional<dt::PowerProfile> power_profile(const Context& ctx, const PowerDelivery::Start&) {
    return make_power_profile(ctx, ctx.is_ac_family() ? ctx.get_ac_params().max_charge_power
                                                      : ctx.get_dc_params().max_charge_power);
}

// [V2G20-1839], [V2G20-1197]: leaving the loop, a pause included, is a zero power period.
std::optional<dt::PowerProfile> power_profile(const Context& ctx, const PowerDelivery::Stop&) {
    return make_power_profile(ctx, 0.0f);
}

std::optional<dt::PowerProfile> power_profile(const Context& ctx, PowerDelivery::Standby) {
    return make_power_profile(ctx, 0.0f);
}

std::optional<dt::PowerProfile> power_profile(const Context&, PowerDelivery::ScheduleRenegotiation) {
    return std::nullopt;
}

} // namespace

message_20::PowerDeliveryRequest PowerDelivery::make_request(dt::Progress charge_progress,
                                                             std::optional<dt::PowerProfile> profile) {
    message_20::PowerDeliveryRequest req;
    req.processing = dt::Processing::Finished;
    req.charge_progress = charge_progress;
    // channel_selection deliberately left nullopt
    req.power_profile = std::move(profile);
    return req;
}

BasePointerType to_power_delivery(Context& ctx, PowerDelivery::ChargeProgress charge_progress) {
    const auto progress = std::visit([](const auto& p) { return wire_progress(p); }, charge_progress);
    auto profile = std::visit([&ctx](const auto& p) { return power_profile(ctx, p); }, charge_progress);
    ctx.send_request(PowerDelivery::make_request(progress, std::move(profile)));
    return ctx.create_state<PowerDelivery>(charge_progress);
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

    return std::visit([this](const auto& progress) { return accepted(m_ctx, progress); }, m_charge_progress);
}

} // namespace iso15118::ev::d20::state
