// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/power_delivery.hpp>

#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/d2/state/welding_detection.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/charging_status.hpp>
#include <iso15118/ev/detail/d2/state/current_demand.hpp>
#include <iso15118/ev/detail/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>
#include <iso15118/ev/detail/d2/state/welding_detection.hpp>

namespace iso15118::ev::d2::state {

namespace power_delivery {

dt::ChargingProfile build_charging_profile(const everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12>& schedule,
                                           float fallback_max_power_w) {
    dt::ChargingProfile profile;
    if (schedule.empty()) {
        dt::ProfileEntry entry;
        entry.start = 0;
        entry.max_power = dt::to_physical_value(fallback_max_power_w, dt::Unit::W);
        profile.profile_entry.push_back(entry);
        return profile;
    }

    // At most 12 PMaxSchedule entries against 24 ChargingProfile slots, so every entry fits.
    for (const auto& pmax_entry : schedule) {
        dt::ProfileEntry entry;
        entry.start = pmax_entry.start;
        entry.max_power = pmax_entry.p_max;
        profile.profile_entry.push_back(entry);
    }
    return profile;
}

message_2::PowerDeliveryRequest create_dc_request(dt::ChargeProgress charge_progress, uint8_t sa_schedule_tuple_id,
                                                  const dt::DC_EVStatus& dc_ev_status, bool charging_complete,
                                                  std::optional<dt::ChargingProfile> charging_profile) {
    message_2::PowerDeliveryRequest req;
    req.charge_progress = charge_progress;
    req.sa_schedule_tuple_id = sa_schedule_tuple_id;
    req.charging_profile = std::move(charging_profile);

    auto& param = req.dc_ev_power_delivery_parameter.emplace();
    param.dc_ev_status = dc_ev_status;
    param.charging_complete = charging_complete;
    return req;
}

message_2::PowerDeliveryRequest create_ac_request(dt::ChargeProgress charge_progress, uint8_t sa_schedule_tuple_id,
                                                  std::optional<dt::ChargingProfile> charging_profile) {
    message_2::PowerDeliveryRequest req;
    req.charge_progress = charge_progress;
    req.sa_schedule_tuple_id = sa_schedule_tuple_id;
    req.charging_profile = std::move(charging_profile);
    return req;
}

} // namespace power_delivery

namespace {

dt::ChargeProgress wire_progress(PowerDelivery::Start) {
    return dt::ChargeProgress::Start;
}

dt::ChargeProgress wire_progress(const PowerDelivery::Stop&) {
    return dt::ChargeProgress::Stop;
}

dt::ChargeProgress wire_progress(PowerDelivery::Renegotiate) {
    return dt::ChargeProgress::Renegotiate;
}

Result accepted(Context& ctx, bool, PowerDelivery::Start) {
    return to_charge_loop(ctx);
}

// The SECC answers a renegotiation by returning to ChargeParameterDiscovery for a new
// SAScheduleList; for DC that re-runs CableCheck -> PreCharge before charging resumes.
Result accepted(Context& ctx, bool, PowerDelivery::Renegotiate) {
    return to_charge_parameter_discovery(ctx);
}

Result accepted(Context& ctx, bool is_dc, const PowerDelivery::Stop& stop) {
    ctx.feedback.power_delivery_stopped();
    if (is_dc) {
        // Charging has ended; the EV is no longer ready to take power (DIN mirror).
        ctx.send_request(welding_detection::create_request(make_dc_ev_status(ctx.get_dc_params(), false)));
        return ctx.create_state<WeldingDetection>(stop.reason);
    }
    return to_session_stop(ctx, stop.reason);
}

} // namespace

BasePointerType to_charge_loop(Context& ctx) {
    if (is_dc_mode(ctx.params().energy_transfer_mode)) {
        ctx.send_request(current_demand::create_request(ctx.get_dc_params()));
        return ctx.create_state<CurrentDemand>();
    }
    ctx.send_request(charging_status::create_request());
    return ctx.create_state<ChargingStatus>();
}

BasePointerType to_power_delivery(Context& ctx, PowerDelivery::Phase phase) {
    const auto progress = std::visit([](const auto& p) { return wire_progress(p); }, phase);
    const bool is_dc = is_dc_mode(ctx.params().energy_transfer_mode);
    const auto dc_params = ctx.get_dc_params();

    // [V2G2-673]: the ChargingProfile accompanies Start in both modes, stating the power the EV intends
    // to draw within the selected SAScheduleTuple. Stop/Renegotiate announce nothing. The fallback power
    // comes from the EV's own voltage/current limits, not from an energy amount.
    std::optional<dt::ChargingProfile> profile{std::nullopt};
    if (std::holds_alternative<PowerDelivery::Start>(phase)) {
        const float fallback_power_w =
            is_dc ? (dc_params.max_charge_power > 0.0f ? dc_params.max_charge_power
                                                       : dc_params.max_voltage * dc_params.max_charge_current)
                  : ctx.params().ac.ev_max_voltage * ctx.params().ac.ev_max_current;
        profile = power_delivery::build_charging_profile(ctx.evse_info.selected_pmax_schedule, fallback_power_w);
    }

    const auto tuple_id = ctx.sa_schedule_tuple_id();
    if (is_dc) {
        // Table 103 [V2G2-375]: ChargingComplete means fully charged, not merely that the session ends.
        const bool charging_complete = (dc_params.present_soc >= 100.0);
        // EVReady drops with the Stop request; Start and Renegotiate keep the EV ready to take power.
        const bool ev_ready = not std::holds_alternative<PowerDelivery::Stop>(phase);
        ctx.send_request(power_delivery::create_dc_request(progress, tuple_id, make_dc_ev_status(dc_params, ev_ready),
                                                           charging_complete, std::move(profile)));
    } else {
        ctx.send_request(power_delivery::create_ac_request(progress, tuple_id, std::move(profile)));
    }
    return ctx.create_state<PowerDelivery>(phase);
}

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery (ISO 15118-2)");
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();
    if (expect_response<message_2::PowerDeliveryResponse>(m_ctx, *variant) == nullptr) {
        return Result::stopping();
    }

    const bool is_dc = is_dc_mode(m_ctx.params().energy_transfer_mode);
    return std::visit([this, is_dc](const auto& phase) { return accepted(m_ctx, is_dc, phase); }, m_phase);
}

} // namespace iso15118::ev::d2::state
