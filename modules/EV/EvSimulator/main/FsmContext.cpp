// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "FsmContext.hpp"

#include "states/Disabled.hpp"
#include "states/Faulted.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/ev_simulator/codec.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <type_traits>
#include <variant>

namespace module {

namespace {

std::string kvs_key(int connector_id) {
    return "evsim_" + std::to_string(connector_id) + "_state";
}

} // namespace

bool is_iso_mode(API_types::ev_simulator::ChargeMode m) {
    using CM = API_types::ev_simulator::ChargeMode;
    return m == CM::AcIso2 || m == CM::AcIsoD20 || m == CM::DcIso2 || m == CM::DcIsoD20;
}

// PersistedState <-> JSON. Enum members are stored via their public typed-API
// serializer to avoid pulling in the private `json_codec.hpp`.
//
// Unknown keys (e.g. a `last_scenario` from an older blob) are silently
// dropped by nlohmann::json's `find` / `value` lookups, so a KVS payload
// produced before the field was removed still round-trips correctly.
void to_json(nlohmann::json& j, const PersistedState& s) {
    j = nlohmann::json{{"plugged_in", s.plugged_in}};
    if (s.configured_session) {
        j["configured_session"] = nlohmann::json::parse(API_types::ev_simulator::serialize(*s.configured_session));
    }
}

void from_json(const nlohmann::json& j, PersistedState& s) {
    // Use `at` rather than `value(..., false)` so a missing or wrong-type
    // "plugged_in" field surfaces as a json::exception caught by kvs_load and
    // logged at error level. The previous `value` form silently fabricated
    // `false` for a corrupt payload missing the field.
    s.plugged_in = j.at("plugged_in").get<bool>();
    s.configured_session.reset();
    // Unknown keys (e.g. a `last_mode` from a pre-configure_session blob) are
    // ignored by this find, so legacy KVS payloads load without throwing.
    if (auto it = j.find("configured_session"); it != j.end() && !it->is_null()) {
        s.configured_session =
            API_types::ev_simulator::deserialize<API_types::ev_simulator::SessionConfigParams>(it->dump());
    }
}

FsmContext::FsmContext(PeerActions actions, Publisher pub, TimerArm timer_arm, TimerCancel timer_cancel,
                       TickArm tick_arm, TickDisarm tick_disarm, ScenarioEnqueue enqueue_event,
                       ScenarioTimerArm scenario_timer_arm, const Conf& cfg_, const ev_API::Topics& topics) :
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
    // Validate cfg.on_battery_full once at construction. parse_on_battery_full
    // throws std::invalid_argument on an unknown value; the throw propagates
    // out of the FsmContext ctor and aborts module init so a typo in the
    // config (e.g. "stopsesion") is caught loudly instead of silently
    // downgrading to Clamp on every SocIntegrator tick.
    on_battery_full_policy = parse_on_battery_full(cfg.on_battery_full);

    // Seed SimVars from Conf.
    vars.battery_capacity_wh = static_cast<float>(cfg.dc_energy_capacity);
    vars.soc_pct = static_cast<float>(cfg.soc_initial_pct);
    vars.battery_charge_wh = vars.battery_capacity_wh * (vars.soc_pct / 100.0f);
    vars.charging_current_a = static_cast<float>(cfg.max_current_a);
    vars.three_phases = cfg.three_phases;
    vars.departure_time_s = cfg.departure_time_s;
    vars.e_amount_wh = cfg.e_amount_wh;
    vars.force_payment_option = cfg.force_payment_option;
    // Seed the DC bus voltage so the open-loop fallback in
    // effective_dc_current_a() integrates against a sane voltage before any
    // live present voltage is reported. The present current optional
    // (vars.evse_dc_present_current_a) default-constructs to nullopt, so until
    // a peer EvInfo arrives the integrator uses the cfg.dc_target_current
    // open-loop fallback.
    vars.dc_present_voltage_v = static_cast<float>(cfg.dc_target_voltage);
}

// ---- CP / power shortcuts ----------------------------------------------

void FsmContext::set_cp(::types::ev_board_support::EvCpState s) {
    if (peer_actions.bsp.present) {
        peer_actions.bsp.set_cp(s);
    }
}

void FsmContext::allow_power_on(bool on) {
    if (peer_actions.bsp.present) {
        peer_actions.bsp.allow_power_on(on);
    }
}

void FsmContext::enable_bsp(bool on) {
    if (peer_actions.bsp.present) {
        peer_actions.bsp.enable(on);
    }
}

void FsmContext::clear_diode_fail() {
    if (peer_actions.bsp.present) {
        peer_actions.bsp.diode_fail(false);
    }
}

void FsmContext::clear_rcd_error() {
    if (peer_actions.bsp.present) {
        peer_actions.bsp.set_rcd_error(0.0f);
    }
}

// ---- AC params shortcut ------------------------------------------------

namespace {
// Clamp an EV desired AC current to an optional EVSE-communicated ceiling. The
// single clamp implementation shared by effective_ac_current_a() (the SoC read
// side) and bsp_apply_ac_params_clamped() (the BSP push side), so the
// integrated and applied currents apply one identical clamp and cannot diverge.
float clamp_ac_current(float desired_a, const std::optional<float>& ceiling) {
    return ceiling.has_value() ? std::min(desired_a, *ceiling) : desired_a;
}
} // namespace

void FsmContext::set_desired_ac_params(float desired_a, bool three_phases_) {
    vars.charging_current_a = desired_a; // EV desired (intent)
    bsp_apply_ac_params_clamped(desired_a, three_phases_);
}

void FsmContext::bsp_apply_ac_params_clamped(float desired_a, bool three_phases_) {
    const float effective = clamp_ac_current(desired_a, vars.evse_ac_max_current_a);
    if (peer_actions.bsp.present) {
        peer_actions.bsp.set_ac_max_current(effective);
        peer_actions.bsp.set_three_phases(three_phases_);
    }
    // Tracking three_phases is fine; the desired current (charging_current_a)
    // is left untouched so a transient EVSE ceiling can never destroy it.
    vars.three_phases = three_phases_;
}

float FsmContext::effective_ac_current_a() const {
    return clamp_ac_current(vars.charging_current_a, vars.evse_ac_max_current_a);
}

float FsmContext::effective_dc_current_a() const {
    // Closed loop: a reported live present current is returned un-clamped so a
    // negative value (BPT discharge) passes through as reverse-power flow.
    if (vars.evse_dc_present_current_a.has_value()) {
        return *vars.evse_dc_present_current_a;
    }
    // Open-loop fallback: no live measurement, so integrate the configured
    // target current clamped to the EV's own max limit. The post-integration
    // clamp in soc_step keeps battery_charge_wh within [0, capacity].
    return std::clamp(static_cast<float>(cfg.dc_target_current), 0.0f, static_cast<float>(cfg.dc_max_current_limit));
}

void FsmContext::note_evse_ac_max_current(float max_current_a) {
    vars.evse_ac_max_current_a = max_current_a;
}

void FsmContext::note_evse_ac_target_power(const ::types::iso15118::AcTargetPower& target_power) {
    if (!target_power.target_active_power.has_value()) {
        return;
    }
    // Count the phases the charger is actually driving: start from the
    // configured phase count and drop a phase for each per-phase target the
    // EVSE omits (a charger driving only L1 reports no L2/L3 target), then
    // derive per-phase current as total power / (voltage * phase_count).
    int phase_count = cfg.three_phases ? 3 : 1;
    if (!target_power.target_active_power_L2.has_value() && cfg.three_phases) {
        phase_count--;
    }
    if (!target_power.target_active_power_L3.has_value() && cfg.three_phases) {
        phase_count--;
    }
    const double voltage = cfg.ac_nominal_voltage;
    if (voltage <= 0.0 || phase_count <= 0) {
        return;
    }
    const double current = *target_power.target_active_power / (voltage * phase_count);
    vars.evse_ac_max_current_a = static_cast<float>(current);
}

// ---- ISO 15118 shortcuts ----------------------------------------------

bool FsmContext::iso_start_charging(API_types::ev_simulator::ChargeMode mode,
                                    std::optional<API_types::ev_simulator::PaymentOption> payment_opt,
                                    int32_t departure, int32_t e_amount) {
    using CM = API_types::ev_simulator::ChargeMode;
    using ETM = ::types::iso15118::EnergyTransferMode;

    // BPT applies only to D20 modes — non-D20 modes ignore the session bpt.
    const auto session_bpt = vars.bpt();
    const bool bpt_active = session_bpt.has_value() && (mode == CM::AcIsoD20 || mode == CM::DcIsoD20);
    // MCS applies only to DcIsoD20 — Plugged rejects mcs on other modes; here
    // we still gate on the mode so unexpected callers don't silently flip etm.
    const bool mcs_active = vars.mcs_enabled() && mode == CM::DcIsoD20;

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
    // The ISO peer's presence flag implies every iso_* function is wired
    // (build_peer_actions sets them together), so the inner sub-calls below
    // need no separate guards.
    if (!peer_actions.iso.present) {
        return false;
    }

    // DC modes report the EV's DC limits/targets to the SECC. Without this the
    // EV's target_current stays 0 and the SECC commands 0 A. BPT/MCS modes set
    // these too; the bpt_active block below additionally adds discharge params.
    if (mode == CM::DcIso2 || mode == CM::DcIsoD20) {
        ::types::iso15118::DcEvParameters dc_params{};
        dc_params.target_current = static_cast<float>(cfg.dc_target_current);
        dc_params.target_voltage = static_cast<float>(cfg.dc_target_voltage);
        dc_params.max_current_limit = static_cast<float>(cfg.dc_max_current_limit);
        dc_params.max_power_limit = static_cast<float>(cfg.dc_max_power_limit);
        dc_params.max_voltage_limit = static_cast<float>(cfg.dc_max_voltage_limit);
        dc_params.energy_capacity = static_cast<float>(cfg.dc_energy_capacity);
        peer_actions.iso.set_dc_params(dc_params);
    }

    if (bpt_active) {
        peer_actions.iso.enable_sae_j2847_v2g_v2h();
        ::types::iso15118::DcEvBPTParameters internal_bpt{};
        internal_bpt.discharge_max_current_limit = session_bpt->discharge_max_current_limit;
        internal_bpt.discharge_max_power_limit = session_bpt->discharge_max_power_limit;
        internal_bpt.discharge_target_current = session_bpt->discharge_target_current;
        internal_bpt.discharge_minimal_soc = session_bpt->discharge_minimal_soc;
        peer_actions.iso.set_bpt_dc_params(internal_bpt);
    }

    auto payment = payment_opt.value_or(API_types::ev_simulator::PaymentOption::ExternalPayment);
    auto internal_payment = (payment == API_types::ev_simulator::PaymentOption::Contract)
                                ? ::types::iso15118::PaymentOption::Contract
                                : ::types::iso15118::PaymentOption::ExternalPayment;
    const bool started =
        peer_actions.iso.start_charging(etm, internal_payment, departure, e_amount, vars.force_payment_option);
    if (started) {
        // A Josev V2G comm session is now live. The resume gate in Paused waits
        // for this session's IsoV2GFinished before starting a new one.
        vars.iso_session_active = true;
    }
    return started;
}

void FsmContext::iso_stop_charging() {
    if (peer_actions.iso.present) {
        peer_actions.iso.stop_charging();
    }
}

void FsmContext::iso_pause_charging() {
    if (peer_actions.iso.present) {
        peer_actions.iso.pause_charging();
    }
}

void FsmContext::iso_update_soc(float pct) {
    if (peer_actions.iso.present) {
        peer_actions.iso.update_soc(pct);
    }
}

// ---- SLAC shortcut -----------------------------------------------------

bool FsmContext::slac_trigger_matching() {
    if (!peer_actions.slac.present) {
        return false;
    }
    return peer_actions.slac.trigger_matching();
}

// ---- Persisted state ---------------------------------------------------

void FsmContext::mark_plugged_in(bool plugged_in) {
    persisted.plugged_in = plugged_in;
}

void FsmContext::remember_session_config(const std::optional<API_types::ev_simulator::SessionConfigParams>& sp) {
    persisted.configured_session = sp;
}

// ---- KVS shortcuts -----------------------------------------------------

void FsmContext::kvs_load() {
    if (!cfg.keep_cross_boot_plugin_state || !peer_actions.kvs.present) {
        return;
    }
    auto raw_opt = peer_actions.kvs.load_raw(kvs_key(cfg.connector_id));
    if (!raw_opt.has_value()) {
        return; // first boot or key unset
    }
    const auto& raw = *raw_opt;
    if (raw.empty()) {
        EVLOG_warning << "EvSimulator: KVS load returned empty payload for key " << kvs_key(cfg.connector_id)
                      << "; defaulting persisted state";
        persisted = PersistedState{};
        return;
    }
    try {
        persisted = nlohmann::json::parse(raw).get<PersistedState>();
        // Seed the live latched config from what was persisted so a
        // cross-boot plug consumes the pre-restart configure_session.
        configured_session = persisted.configured_session;
    } catch (const nlohmann::json::exception& e) {
        EVLOG_error << "EvSimulator: KVS load failed for key " << kvs_key(cfg.connector_id) << ": " << e.what()
                    << " payload=" << raw;
        persisted = PersistedState{};
    }
}

void FsmContext::kvs_save() {
    if (!cfg.keep_cross_boot_plugin_state || !peer_actions.kvs.present) {
        return;
    }
    auto payload = nlohmann::json(persisted).dump();
    peer_actions.kvs.store(kvs_key(cfg.connector_id), payload);
}

// ---- Out-of-band error raise/clear -------------------------------------

void FsmContext::raise_error(const RaiseErrorCmd& cmd) {
    if (!peer_actions.error.present) {
        return;
    }
    peer_actions.error.raise(cmd.type, cmd.sub_type, cmd.message, cmd.severity);
}

void FsmContext::clear_error(const ClearErrorCmd& cmd) {
    if (!peer_actions.error.present) {
        return;
    }
    const auto sub = cmd.sub_type.value_or("");
    // Surface a Rejected CommandAck when the framework has no matching active
    // error: clear_error otherwise returns an empty list silently, so the user
    // never learns the request was a no-op.
    if (peer_actions.error.is_active && !peer_actions.error.is_active(cmd.type, sub)) {
        publish_e2m_command_ack("clear_error", "no such error active");
        return;
    }
    peer_actions.error.clear(cmd.type, cmd.sub_type);
}

// ---- configure_session interceptor -------------------------------------

bool FsmContext::validate_session_config(const API_types::ev_simulator::SessionConfigParams& sp,
                                         std::string& reject_reason) const {
    namespace api = API_types::ev_simulator;

    // Every SessionConfigParams alternative carries an optional curve; pull it
    // out with a generic visitor (no per-alternative branching needed).
    std::optional<api::ChargingCurve> curve;
    std::visit([&](const auto& v) { curve = v.curve; }, sp);
    if (curve.has_value()) {
        if (curve->points.empty()) {
            reject_reason = "curve has empty points";
            return false;
        }
        for (std::size_t i = 1; i < curve->points.size(); ++i) {
            if (curve->points[i].t_offset_ms <= curve->points[i - 1].t_offset_ms) {
                reject_reason = "curve points not monotonic";
                return false;
            }
        }
    }

    const api::ChargeMode mode = api::mode_of(sp);
    if (is_iso_mode(mode) && (!peer_actions.iso.present || !peer_actions.slac.present)) {
        reject_reason = "no ev_slac peer";
        return false;
    }
    return true;
}

void FsmContext::configure_session(const API_types::ev_simulator::SessionConfigParams& sp) {
    std::string reject_reason;
    if (!validate_session_config(sp, reject_reason)) {
        publish_e2m_command_ack("configure_session", reject_reason);
        return;
    }
    configured_session = sp;
    remember_session_config(sp);
    kvs_save();
    publish_e2m_command_ack_accepted("configure_session", "configuration latched; applies at next plug");
}

// ---- External publish helpers ------------------------------------------

void FsmContext::publish_e2m_state(API_types::ev_simulator::FsmState s) {
    using namespace API_types::ev_simulator;
    publisher_(topics_.everest_to_extern("state"), serialize(s));
}

void FsmContext::publish_e2m_fault(API_types::ev_simulator::FaultReport f) {
    using namespace API_types::ev_simulator;
    publisher_(topics_.everest_to_extern("fault"), serialize(f));
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
}

void FsmContext::publish_e2m_slac_state() {
    using namespace API_types::ev_simulator;
    // vars.slac_state mirrors the internal ::types::slac enum names
    // ("UNMATCHED"/"MATCHING"/"MATCHED"); translate to the external enum.
    SlacStateKind k = SlacStateKind::Unmatched;
    if (vars.slac_state == "MATCHING") {
        k = SlacStateKind::Matching;
    } else if (vars.slac_state == "MATCHED") {
        k = SlacStateKind::Matched;
    }
    SlacState s{k};
    publisher_(topics_.everest_to_extern("slac_state"), serialize(s));
}

void FsmContext::publish_e2m_bsp_event(const ::types::board_support_common::BspEvent& e) {
    using namespace API_types::ev_simulator;
    BspEventKind kind = BspEventKind::Disconnected;
    switch (e.event) {
    case ::types::board_support_common::Event::A:
        kind = BspEventKind::A;
        break;
    case ::types::board_support_common::Event::B:
        kind = BspEventKind::B;
        break;
    case ::types::board_support_common::Event::C:
        kind = BspEventKind::C;
        break;
    case ::types::board_support_common::Event::D:
        kind = BspEventKind::D;
        break;
    case ::types::board_support_common::Event::E:
        kind = BspEventKind::E;
        break;
    case ::types::board_support_common::Event::F:
        kind = BspEventKind::F;
        break;
    case ::types::board_support_common::Event::PowerOn:
        kind = BspEventKind::PowerOn;
        break;
    case ::types::board_support_common::Event::PowerOff:
        kind = BspEventKind::PowerOff;
        break;
    case ::types::board_support_common::Event::Disconnected:
        kind = BspEventKind::Disconnected;
        break;
    }
    BspEvent ext{kind};
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

void FsmContext::publish_e2m_command_ack_accepted(const std::string& command, const std::string& note) {
    using namespace API_types::ev_simulator;
    CommandAck ack{command, CommandAckStatus::Accepted, note};
    publisher_(topics_.everest_to_extern("command_ack"), serialize(ack));
}

void FsmContext::publish_internal_bsp_event(const ::types::board_support_common::BspEvent& e) {
    if (peer_actions.publisher.present) {
        peer_actions.publisher.bsp_event(e);
    }
}

void FsmContext::publish_internal_ev_info() {
    if (!peer_actions.publisher.present) {
        return;
    }
    ::types::evse_manager::EVInfo ev_info{};
    ev_info.soc = vars.soc_pct;
    ev_info.battery_capacity = vars.battery_capacity_wh;
    peer_actions.publisher.ev_info(ev_info);
}

// ---- Free helpers -------------------------------------------------------

StateBase::Result transition_to_fault(FsmContext& ctx, const API_types::ev_simulator::InjectFaultParams& p) {
    // Message precedence: a message carried on the InjectFault payload itself
    // (a state synthesizing a fault on a peer precondition failure) wins. It
    // travels with the event, so an intervening transition cannot leave it
    // stale. Only if the payload carries no message do we fall back to a
    // same-type last_fault so existing descriptive context is not clobbered
    // by this catch-all replacement.
    std::optional<std::string> message = p.message;
    if (!message && ctx.vars.last_fault && ctx.vars.last_fault->type == p.type) {
        message = ctx.vars.last_fault->message;
    }
    ctx.vars.last_fault = API_types::ev_simulator::FaultReport{p.type, message, p.rcd_mA};
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
    ctx.vars.session.reset();
    ctx.vars.last_fault.reset();
    ctx.vars.was_full = false;
    ctx.mark_plugged_in(false);
    ctx.kvs_save();
    // Park the BSP peer's simulation loop. The set_cp(A) and
    // allow_power_on(false) above must run first so the peer observes a
    // clean teardown before its simulation_step is suspended.
    ctx.enable_bsp(false);
    return {false, std::make_unique<Disabled>(ctx)};
}

} // namespace module
