// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/d2/state/power_delivery.hpp>
#include <iso15118/message/d2/charge_parameter_discovery.hpp>
#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <variant>

namespace iso15118::d2::state {

namespace dt = msg::data_types;

static dt::PhysicalValue make_phys(int16_t value, int8_t multiplier, dt::UnitSymbol unit) {
    dt::PhysicalValue pv;
    pv.value = value;
    pv.multiplier = multiplier;
    pv.unit = unit;
    return pv;
}

void ChargeParameterDiscovery::enter() {}

Result ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::ChargeParameterDiscoveryRequest>()) {
        msg::ChargeParameterDiscoveryResponse res;
        setup_header(res.header, m_ctx.session);
        res.evse_processing = dt::EvseProcessing::Finished;

        // AC EVSE parameters from config (Session::init_d2_fsm sources these
        // from the EVSE rating main.cpp configured). Max current is derived from
        // the configured power so it stays consistent with the SA-schedule P_max.
        const int32_t nominal_v = m_ctx.session_config.evse_nominal_voltage_v;
        const int32_t max_power = m_ctx.session_config.evse_max_power_w;
        const int32_t phases    = (m_ctx.session_config.evse_phase_count > 0)
                                      ? m_ctx.session_config.evse_phase_count : 1;
        // Per-phase max current = total power / (phases * voltage).
        const int16_t max_current_a =
            (nominal_v > 0) ? static_cast<int16_t>(max_power / (nominal_v * phases)) : 0;

        dt::AcEvseChargeParameter ac_param;
        ac_param.ac_evse_status.rcd = false;
        ac_param.ac_evse_status.notification_max_delay = 0;
        ac_param.ac_evse_status.evse_notification = dt::EvseNotification::None;
        ac_param.evse_nominal_voltage = make_phys(static_cast<int16_t>(nominal_v), 0, dt::UnitSymbol::V);
        ac_param.evse_max_current     = make_phys(max_current_a, 0, dt::UnitSymbol::A);
        res.evse_charge_parameter = ac_param;

        // SA schedule — use EV departure time if given, otherwise 86400 s (1 day per spec)
        // departure_time is on ev_charge_parameter (AcEvChargeParameter / DcEvChargeParameter)
        // both inherit from EvChargeParameter which carries departure_time.
        constexpr uint32_t SA_SCHEDULE_DURATION = 86400;
        uint32_t departure_duration = SA_SCHEDULE_DURATION;
        std::visit([&departure_duration](const auto& ev_param) {
            if (ev_param.departure_time.has_value() && ev_param.departure_time.value() > 0) {
                departure_duration = ev_param.departure_time.value();
            }
        }, req->ev_charge_parameter);
        // P_max from the configured EVSE power. Encode within int16 (mantissa)
        // by scaling with a x10 multiplier when it doesn't fit.
        dt::SaScheduleTuple tuple;
        tuple.sa_schedule_tuple_id = 1;
        dt::PMaxScheduleEntry entry;
        entry.time_interval.start    = 0;
        entry.time_interval.duration = departure_duration;  // [V2G2-479] duration required
        entry.p_max = (max_power <= 32767)
                          ? make_phys(static_cast<int16_t>(max_power), 0, dt::UnitSymbol::W)
                          : make_phys(static_cast<int16_t>(max_power / 10), 1, dt::UnitSymbol::W);
        tuple.pmax_schedule.push_back(entry);
        res.sa_schedule_list = dt::SaSchedules{tuple};

        response_with_code(res, dt::ResponseCode::OK);
        m_ctx.respond(res);
        return m_ctx.create_state<PowerDelivery>();
    }

    const msg::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d2::state
