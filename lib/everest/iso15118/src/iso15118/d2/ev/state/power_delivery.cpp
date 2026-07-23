// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/ev/state/power_delivery.hpp>

#include <iso15118/d2/ev/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/ev/state/charging_status.hpp>
#include <iso15118/d2/ev/state/current_demand.hpp>
#include <iso15118/d2/ev/state/session_stop.hpp>
#include <iso15118/d2/ev/state/welding_detection.hpp>
#include <iso15118/d2/ev/timeouts.hpp>

#include <iso15118/detail/d2/ev/state/power_delivery.hpp>
#include <iso15118/detail/d2/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d2::ev::state {

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

    // The PMaxSchedule holds at most 12 entries and the ChargingProfile at most 24, so every entry fits.
    for (const auto& pmax_entry : schedule) {
        dt::ProfileEntry entry;
        entry.start = pmax_entry.start;
        entry.max_power = pmax_entry.p_max;
        profile.profile_entry.push_back(entry);
    }
    return profile;
}

message_2::PowerDeliveryRequest create_dc_request(dt::ChargeProgress charge_progress, uint8_t sa_schedule_tuple_id,
                                                  const dt::DC_EVStatus& dc_ev_status, bool charging_complete) {
    message_2::PowerDeliveryRequest req;
    req.charge_progress = charge_progress;
    req.sa_schedule_tuple_id = sa_schedule_tuple_id;

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

Result handle_response(const message_2::PowerDeliveryResponse& res) {
    return {res.response_code < dt::ResponseCode::FAILED};
}

} // namespace power_delivery

using namespace power_delivery;

void PowerDelivery::enter() {
    m_ctx.log.enter_state("PowerDelivery");
}

d2::ev::Result PowerDelivery::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        const auto progress = (m_phase == Phase::Start)         ? dt::ChargeProgress::Start
                              : (m_phase == Phase::Renegotiate) ? dt::ChargeProgress::Renegotiate
                                                                : dt::ChargeProgress::Stop;
        const bool is_dc = is_dc_mode(m_ctx.session_config.requested_energy_transfer_mode);
        const auto tuple_id = m_ctx.evse_info.sa_schedule_tuple_id;

        // ev_power_ready(true) is published at ChargeParameterDiscovery-Finished (Josev parity), not here.

        message_2::PowerDeliveryRequest req;
        if (is_dc) {
            // Table 103 ([V2G2-375]): ChargingComplete means the EV is fully charged (100 % SOC) --
            // not just that the session is ending. Derive it from the reported SoC so an
            // EVSE-requested or user stop below full charge reports false.
            const bool charging_complete = (m_ctx.dc_cache.present_soc >= 100);
            req = create_dc_request(progress, tuple_id, make_dc_ev_status(m_ctx, true), charging_complete);
        } else {
            std::optional<dt::ChargingProfile> profile{std::nullopt};
            if (m_phase == Phase::Start) {
                // ChargingProfile is mandatory for the AC PowerDelivery(Start) request. Derive the
                // fallback power (W) from the EV voltage/current limits, not from the energy amount (Wh).
                const float fallback_power_w =
                    m_ctx.session_config.ac_ev_max_voltage * m_ctx.session_config.ac_ev_max_current;
                profile = build_charging_profile(m_ctx.evse_info.selected_pmax_schedule, fallback_power_w);
            }
            req = create_ac_request(progress, tuple_id, std::move(profile));
        }

        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_POWER_DELIVERY_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_dc_control_event(m_ctx);
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("PowerDelivery message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_2::PowerDeliveryResponse>()) {
        const auto result = handle_response(*res);

        if (not result.valid) {
            m_ctx.log("PowerDelivery failed (response code >= FAILED), terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        const bool is_dc = is_dc_mode(m_ctx.session_config.requested_energy_transfer_mode);

        if (m_phase == Phase::Start) {
            if (is_dc) {
                return m_ctx.create_state<CurrentDemand>();
            }
            return m_ctx.create_state<ChargingStatus>();
        }

        if (m_phase == Phase::Renegotiate) {
            // Schedule renegotiation: the session stays alive; re-run ChargeParameterDiscovery to obtain a
            // new SAScheduleList (for DC this re-enters CableCheck -> PreCharge before charging resumes).
            return m_ctx.create_state<ChargeParameterDiscovery>();
        }

        // Phase::Stop
        if (is_dc) {
            return m_ctx.create_state<WeldingDetection>();
        }
        return m_ctx.create_state<SessionStop>();
    }

    m_ctx.log("expected PowerDeliveryRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::ev::state
