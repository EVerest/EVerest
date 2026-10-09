// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/state/power_delivery.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/ev/d2/state/charging_status.hpp>
#include <iso15118/ev/d2/state/current_demand.hpp>
#include <iso15118/ev/d2/state/session_stop.hpp>
#include <iso15118/ev/d2/state/welding_detection.hpp>
#include <iso15118/ev/detail/d2/context_helper.hpp>
#include <iso15118/ev/detail/d2/state/power_delivery.hpp>
#include <iso15118/ev/detail/d2/state/state_helper.hpp>

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

void PowerDelivery::enter() {
    logf_debug("Enter state: PowerDelivery (ISO 15118-2)");

    const auto progress = (m_phase == Phase::Start)         ? dt::ChargeProgress::Start
                          : (m_phase == Phase::Renegotiate) ? dt::ChargeProgress::Renegotiate
                                                            : dt::ChargeProgress::Stop;
    const bool is_dc = is_dc_mode(m_ctx.params().energy_transfer_mode);
    const auto dc_params = m_ctx.get_dc_params();

    // [V2G2-673]: the ChargingProfile accompanies Start in both modes, stating the power the EV intends
    // to draw within the selected SAScheduleTuple. Stop/Renegotiate announce nothing. The fallback power
    // comes from the EV's own voltage/current limits, not from an energy amount.
    std::optional<dt::ChargingProfile> profile{std::nullopt};
    if (m_phase == Phase::Start) {
        const float fallback_power_w =
            is_dc ? (dc_params.max_charge_power > 0.0f ? dc_params.max_charge_power
                                                       : dc_params.max_voltage * dc_params.max_charge_current)
                  : m_ctx.params().ac.ev_max_voltage * m_ctx.params().ac.ev_max_current;
        profile = power_delivery::build_charging_profile(m_ctx.evse_info.selected_pmax_schedule, fallback_power_w);
    }

    const auto tuple_id = m_ctx.evse_info.sa_schedule_tuple_id;
    if (is_dc) {
        // Table 103 [V2G2-375]: ChargingComplete means fully charged, not merely that the session ends.
        const bool charging_complete = (dc_params.present_soc >= 100.0);
        // EVReady drops with the Stop request; Start and Renegotiate keep the EV ready to take power.
        const bool ev_ready = (m_phase != Phase::Stop);
        m_ctx.send_request(power_delivery::create_dc_request(progress, tuple_id, make_dc_ev_status(dc_params, ev_ready),
                                                             charging_complete, std::move(profile)));
    } else {
        m_ctx.send_request(power_delivery::create_ac_request(progress, tuple_id, std::move(profile)));
    }
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

    if (m_phase == Phase::Start) {
        if (is_dc) {
            return m_ctx.create_state<CurrentDemand>();
        }
        return m_ctx.create_state<ChargingStatus>();
    }

    if (m_phase == Phase::Renegotiate) {
        // The SECC answers a renegotiation by returning to ChargeParameterDiscovery for a new
        // SAScheduleList; for DC that re-runs CableCheck -> PreCharge before charging resumes.
        return m_ctx.create_state<ChargeParameterDiscovery>();
    }

    if (is_dc) {
        return m_ctx.create_state<WeldingDetection>();
    }
    return m_ctx.create_state<SessionStop>();
}

} // namespace iso15118::ev::d2::state
