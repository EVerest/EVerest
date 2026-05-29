// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "FsmContext.hpp"

#include "states/Disabled.hpp"
#include "states/Faulted.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <string>

namespace module {

namespace {

std::string kvs_key(int connector_id) {
    return "evsim_" + std::to_string(connector_id) + "_state";
}

} // namespace

// PersistedState <-> JSON. Enum members are stored via their public typed-API
// serializer to avoid pulling in the private `json_codec.hpp`.
void to_json(nlohmann::json& j, const PersistedState& s) {
    j = nlohmann::json{{"plugged_in", s.plugged_in}};
    if (s.last_mode) {
        j["last_mode"] = nlohmann::json::parse(API_types::ev_simulator::serialize(*s.last_mode));
    }
    if (s.last_scenario) {
        j["last_scenario"] = nlohmann::json::parse(API_types::ev_simulator::serialize(*s.last_scenario));
    }
}

void from_json(const nlohmann::json& j, PersistedState& s) {
    s.plugged_in = j.value("plugged_in", false);
    s.last_mode.reset();
    s.last_scenario.reset();
    if (auto it = j.find("last_mode"); it != j.end() && !it->is_null()) {
        s.last_mode = API_types::ev_simulator::deserialize<API_types::ev_simulator::ChargeMode>(it->dump());
    }
    if (auto it = j.find("last_scenario"); it != j.end() && !it->is_null()) {
        s.last_scenario = API_types::ev_simulator::deserialize<API_types::ev_simulator::ScenarioName>(it->dump());
    }
}

FsmContext::FsmContext(PeerHandles peers_, PeerActions actions, Publisher pub, TimerArm timer_arm,
                       TimerCancel timer_cancel, TickArm tick_arm, TickDisarm tick_disarm,
                       ScenarioEnqueue enqueue_event, ScenarioTimerArm scenario_timer_arm, const Conf& cfg_,
                       const ev_API::Topics& topics) :
    peers(peers_),
    peer_actions(std::move(actions)),
    cfg(cfg_),
    publisher_(std::move(pub)),
    timer_arm_(std::move(timer_arm)),
    timer_cancel_(std::move(timer_cancel)),
    tick_arm_(std::move(tick_arm)),
    tick_disarm_(std::move(tick_disarm)),
    enqueue_event_(std::move(enqueue_event)),
    scenario_timer_arm_(std::move(scenario_timer_arm)),
    topics_(topics) {
    // Seed SimVars from Conf.
    vars.battery_capacity_wh = static_cast<float>(cfg.dc_energy_capacity);
    vars.soc_pct = static_cast<float>(cfg.soc_initial_pct);
    vars.battery_charge_wh = vars.battery_capacity_wh * (vars.soc_pct / 100.0f);
    vars.charging_current_a = static_cast<float>(cfg.max_current_a);
    vars.three_phases = cfg.three_phases;
    vars.departure_time_s = cfg.departure_time_s;
    vars.e_amount_wh = cfg.e_amount_wh;
    vars.force_payment_option = cfg.force_payment_option;
    vars.dc_present_current_a = static_cast<float>(cfg.dc_target_current);
    vars.dc_present_voltage_v = static_cast<float>(cfg.dc_target_voltage);
}

// ---- CP / power shortcuts ----------------------------------------------

void FsmContext::set_cp(::types::ev_board_support::EvCpState s) {
    if (peer_actions.bsp_set_cp) {
        peer_actions.bsp_set_cp(s);
    }
}

void FsmContext::allow_power_on(bool on) {
    if (peer_actions.bsp_allow_power_on) {
        peer_actions.bsp_allow_power_on(on);
    }
}

void FsmContext::clear_diode_fail() {
    if (peer_actions.bsp_diode_fail) {
        peer_actions.bsp_diode_fail(false);
    }
}

void FsmContext::clear_rcd_error() {
    if (peer_actions.bsp_set_rcd_error) {
        peer_actions.bsp_set_rcd_error(0.0f);
    }
}

// ---- AC params shortcut ------------------------------------------------

void FsmContext::bsp_apply_ac_params(float current_a, bool three_phases_) {
    if (peer_actions.bsp_set_ac_max_current) {
        peer_actions.bsp_set_ac_max_current(current_a);
    }
    if (peer_actions.bsp_set_three_phases) {
        peer_actions.bsp_set_three_phases(three_phases_);
    }
    vars.charging_current_a = current_a;
    vars.three_phases = three_phases_;
}

// ---- ISO 15118 shortcuts ----------------------------------------------

bool FsmContext::iso_start_charging(API_types::ev_simulator::ChargeMode mode,
                                    std::optional<API_types::ev_simulator::PaymentOption> payment_opt,
                                    int32_t departure, int32_t e_amount) {
    using CM = API_types::ev_simulator::ChargeMode;
    using ETM = ::types::iso15118::EnergyTransferMode;

    // BPT applies only to D20 modes — non-D20 modes ignore vars.bpt.
    const bool bpt_active = vars.bpt.has_value() && (mode == CM::AcIsoD20 || mode == CM::DcIsoD20);
    // MCS applies only to DcIsoD20 — Plugged rejects mcs on other modes; here
    // we still gate on the mode so unexpected callers don't silently flip etm.
    const bool mcs_active = vars.mcs.has_value() && mode == CM::DcIsoD20;

    ETM etm;
    switch (mode) {
    case CM::AcIec:
        return false;
    case CM::AcIso2:
        etm = vars.three_phases ? ETM::AC_three_phase_core : ETM::AC_single_phase_core;
        break;
    case CM::AcIsoD20:
        etm = bpt_active ? ETM::AC_BPT : (vars.three_phases ? ETM::AC_three_phase_core : ETM::AC_single_phase_core);
        break;
    case CM::DcIso2:
        etm = ETM::DC_extended;
        break;
    case CM::DcIsoD20:
        if (mcs_active) {
            etm = bpt_active ? ETM::MCS_BPT : ETM::MCS;
        } else {
            etm = bpt_active ? ETM::DC_BPT : ETM::DC;
        }
        break;
    }
    if (!peer_actions.iso_start_charging) {
        return false;
    }

    if (bpt_active) {
        if (peer_actions.iso_enable_sae_j2847_v2g_v2h) {
            peer_actions.iso_enable_sae_j2847_v2g_v2h();
        }
        if (peer_actions.iso_set_bpt_dc_params) {
            ::types::iso15118::DcEvBPTParameters internal_bpt{};
            internal_bpt.discharge_max_current_limit = vars.bpt->discharge_max_current_limit;
            internal_bpt.discharge_max_power_limit = vars.bpt->discharge_max_power_limit;
            internal_bpt.discharge_target_current = vars.bpt->discharge_target_current;
            internal_bpt.discharge_minimal_soc = vars.bpt->discharge_minimal_soc;
            peer_actions.iso_set_bpt_dc_params(internal_bpt);
        }
    }

    auto payment = payment_opt.value_or(API_types::ev_simulator::PaymentOption::ExternalPayment);
    auto internal_payment = (payment == API_types::ev_simulator::PaymentOption::Contract)
                                ? ::types::iso15118::PaymentOption::Contract
                                : ::types::iso15118::PaymentOption::ExternalPayment;
    return peer_actions.iso_start_charging(etm, internal_payment, departure, e_amount, vars.force_payment_option);
}

void FsmContext::iso_stop_charging() {
    if (peer_actions.iso_stop_charging) {
        peer_actions.iso_stop_charging();
    }
}

void FsmContext::iso_pause_charging() {
    if (peer_actions.iso_pause_charging) {
        peer_actions.iso_pause_charging();
    }
}

void FsmContext::iso_update_soc(float pct) {
    if (peer_actions.iso_update_soc) {
        peer_actions.iso_update_soc(pct);
    }
}

// ---- SLAC shortcut -----------------------------------------------------

bool FsmContext::slac_trigger_matching() {
    if (!peer_actions.slac_trigger_matching) {
        return false;
    }
    return peer_actions.slac_trigger_matching();
}

// ---- KVS shortcuts -----------------------------------------------------

void FsmContext::kvs_load() {
    if (!cfg.keep_cross_boot_plugin_state || !peer_actions.kvs_load_raw) {
        return;
    }
    auto raw = peer_actions.kvs_load_raw(kvs_key(cfg.connector_id));
    if (raw.empty()) {
        return; // first boot or unset
    }
    try {
        persisted = nlohmann::json::parse(raw).get<PersistedState>();
    } catch (...) {
        EVLOG_warning << "EvSimulator: KVS load corrupted, ignoring";
    }
}

void FsmContext::kvs_save() {
    if (!cfg.keep_cross_boot_plugin_state || !peer_actions.kvs_store) {
        return;
    }
    auto payload = nlohmann::json(persisted).dump();
    peer_actions.kvs_store(kvs_key(cfg.connector_id), payload);
}

// ---- External publish helpers ------------------------------------------

void FsmContext::publish_e2m_state(API_types::ev_simulator::FsmState s) {
    using namespace API_types::ev_simulator;
    publisher_(topics_.everest_to_extern("state"), serialize(s));
    snapshot.handle()->current_state = s;
}

void FsmContext::publish_e2m_fault(API_types::ev_simulator::FaultReport f) {
    using namespace API_types::ev_simulator;
    publisher_(topics_.everest_to_extern("fault"), serialize(f));
    snapshot.handle()->last_fault = f;
}

void FsmContext::publish_e2m_iso_session_event(API_types::ev_simulator::IsoSessionEvent e) {
    using namespace API_types::ev_simulator;
    publisher_(topics_.everest_to_extern("iso_session_event"), serialize(e));
}

void FsmContext::publish_e2m_ev_info() {
    using namespace API_types::ev_simulator;
    EvInfo info{vars.soc_pct, vars.battery_capacity_wh, vars.battery_charge_wh,
                /*target_current_a*/ 0.0f, /*target_voltage_v*/ 0.0f};
    publisher_(topics_.everest_to_extern("ev_info"), serialize(info));
    snapshot.handle()->soc_pct = vars.soc_pct;
}

void FsmContext::publish_e2m_slac_state() {
    using namespace API_types::ev_simulator;
    SlacState s{vars.slac_state};
    publisher_(topics_.everest_to_extern("slac_state"), serialize(s));
}

void FsmContext::publish_e2m_bsp_event(const ::types::board_support_common::BspEvent& e) {
    using namespace API_types::ev_simulator;
    BspEvent ext;
    ext.event = ::types::board_support_common::event_to_string(e.event);
    publisher_(topics_.everest_to_extern("bsp_event"), serialize(ext));
}

void FsmContext::publish_e2m_bsp_measurement(const BspMeasurementPayload& m) {
    // BspMeasurement isn't part of the typed ev_simulator API surface yet;
    // pass through as raw nlohmann::json.
    nlohmann::json j{{"cp_pwm_duty_cycle", m.cp_pwm_duty_cycle}};
    if (m.rcd_current_mA) {
        j["rcd_current_mA"] = *m.rcd_current_mA;
    }
    publisher_(topics_.everest_to_extern("bsp_measurement"), j.dump());
}

void FsmContext::publish_e2m_command_ack(const std::string& command, const std::string& reason) {
    using namespace API_types::ev_simulator;
    CommandAck ack{command, CommandAckStatus::Rejected, reason};
    publisher_(topics_.everest_to_extern("command_ack"), serialize(ack));
}

void FsmContext::publish_internal_bsp_event(const ::types::board_support_common::BspEvent& e) {
    if (peer_actions.publish_internal_bsp_event) {
        peer_actions.publish_internal_bsp_event(e);
    }
}

void FsmContext::publish_internal_ev_info() {
    if (!peer_actions.publish_internal_ev_info) {
        return;
    }
    ::types::evse_manager::EVInfo ev_info{};
    ev_info.soc = vars.soc_pct;
    ev_info.battery_capacity = vars.battery_capacity_wh;
    peer_actions.publish_internal_ev_info(ev_info);
}

// ---- Free helpers -------------------------------------------------------

StateBase::Result transition_to_fault(FsmContext& ctx, const API_types::ev_simulator::InjectFaultParams& p) {
    ctx.vars.last_fault = API_types::ev_simulator::FaultReport{p.type, std::nullopt, p.rcd_mA};
    return {false, std::make_unique<Faulted>(ctx)};
}

StateBase::Result handle_query_state(FsmContext& ctx, API_types::ev_simulator::FsmState s) {
    ctx.publish_e2m_state(s);
    return {false, nullptr};
}

StateBase::Result transition_to_disabled(FsmContext& ctx) {
    ctx.clear_diode_fail();
    ctx.clear_rcd_error();
    ctx.set_cp(::types::ev_board_support::EvCpState::A);
    ctx.allow_power_on(false);
    ctx.iso_stop_charging();
    ctx.vars.charge_mode.reset();
    ctx.vars.bpt.reset();
    ctx.vars.mcs.reset();
    ctx.vars.last_fault.reset();
    ctx.persisted.plugged_in = false;
    ctx.kvs_save();
    return {false, std::make_unique<Disabled>(ctx)};
}

} // namespace module
