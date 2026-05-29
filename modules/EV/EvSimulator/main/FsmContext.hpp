// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "EvSimulator.hpp" // for Conf, ev_managerImplBase, ev_board_supportIntf, etc.
#include "Events.hpp"
#include "ScenarioDispatcher.hpp"
#include "StateBase.hpp"

#include <everest/util/async/monitor.hpp>
#include <everest_api_types/ev_simulator/API.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <generated/types/board_support_common.hpp>
#include <generated/types/ev_board_support.hpp>
#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>
#include <generated/types/slac.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>

namespace module {

namespace API_types = everest::lib::API::V1_0::types;
namespace ev_API = everest::lib::API; // for ev_API::Topics

struct PeerHandles {
    ev_board_supportIntf* bsp{nullptr};
    ISO15118_evIntf* iso{nullptr};
    ev_slacIntf* slac{nullptr};
    kvsIntf* kvs{nullptr};
};

// Peer-action function-pointer pack. FsmContext invokes peer behaviors
// through these callbacks rather than calling `call_*` directly so that
// (a) the FSM stays decoupled from the ev-cli generated `*Intf` types (their
// `call_*` methods are non-virtual and require a real ModuleAdapter), and
// (b) tests can record calls without mocking the full interfaces.
//
// The real runtime wires these to `mod.r_*->call_*` and to
// `mod.p_ev_manager->publish_*`. Unset members are silently no-op'd.
struct PeerActions {
    std::function<void(::types::ev_board_support::EvCpState)> bsp_set_cp;
    std::function<void(bool)> bsp_allow_power_on;
    std::function<void(float)> bsp_set_ac_max_current;
    std::function<void(bool)> bsp_set_three_phases;
    std::function<void(bool)> bsp_diode_fail;
    std::function<void(float /*rcd_mA*/)> bsp_set_rcd_error;

    std::function<bool(::types::iso15118::EnergyTransferMode, ::types::iso15118::PaymentOption,
                       int32_t /*departure_time_s*/, int32_t /*e_amount_wh*/, bool /*force_payment_option*/)>
        iso_start_charging;
    std::function<void()> iso_stop_charging;
    std::function<void()> iso_pause_charging;
    std::function<void(float /*soc_pct*/)> iso_update_soc;
    std::function<void()> iso_enable_sae_j2847_v2g_v2h;
    std::function<void(const ::types::iso15118::DcEvBPTParameters&)> iso_set_bpt_dc_params;
    // DC params seam — currently unset; populated when DC peer support is wired.

    std::function<bool()> slac_trigger_matching;

    std::function<void(const std::string& /*key*/, const std::string& /*json*/)> kvs_store;
    std::function<std::string(const std::string& /*key*/)> kvs_load_raw; // returns "" for missing

    // Internal-side publishers (consumed by EvAPI / other in-module subscribers).
    // Type-erased so FsmContext does not need ev_managerImplBase& injection.
    std::function<void(const ::types::board_support_common::BspEvent&)> publish_internal_bsp_event;
    std::function<void(const ::types::evse_manager::EVInfo&)> publish_internal_ev_info;
};

// Linear current ramp captured when SetChargingCurrent carries a non-zero
// ramp_ms. The tick handler interpolates charging_current_a from start_a to
// target_a between start_at and end_at, then clears the optional.
struct ActiveRamp {
    float start_a;
    float target_a;
    bool three_phases;
    std::chrono::steady_clock::time_point start_at;
    std::chrono::steady_clock::time_point end_at;
};

struct SimVars {
    float battery_capacity_wh{60000};
    float battery_charge_wh{18000};
    float soc_pct{30.0f};
    float pwm_duty_cycle{0};
    std::optional<API_types::ev_simulator::ChargeMode> charge_mode;
    std::optional<API_types::ev_simulator::BptParams> bpt;
    std::optional<API_types::ev_simulator::McsProfile> mcs;
    std::string slac_state{"UNMATCHED"};
    int32_t bcb_remaining{0};
    std::optional<API_types::ev_simulator::FaultReport> last_fault;
    float charging_current_a{16.0f};
    bool three_phases{true};
    std::optional<ActiveRamp> active_ramp;
    // DC live current/voltage. Seeded from cfg.dc_target_current /
    // dc_target_voltage by FsmContext ctor and overwritten by peer EvInfo
    // (apply_passthrough_vars in EvSimRuntime) so SocIntegrator integrates
    // actual delivered DC power rather than the static cfg target.
    float dc_present_current_a{0.0f};
    float dc_present_voltage_v{0.0f};
    // Edge-detection state for on_battery_full policies. SocIntegrator sets
    // this to true the first tick SoC reaches cfg.battery_full_threshold_pct
    // and clears it when SoC drops back below; the stop_session / pause_if_iso
    // policies fire only on the false -> true transition.
    bool was_full{false};
    int32_t departure_time_s{86400};
    int32_t e_amount_wh{0};
    bool force_payment_option{false};
};

struct SimSnapshot {
    API_types::ev_simulator::FsmState current_state{API_types::ev_simulator::FsmState::Disabled};
    std::optional<API_types::ev_simulator::ChargeMode> charge_mode;
    std::optional<API_types::ev_simulator::FaultReport> last_fault;
    float soc_pct{30.0f};
};

struct PersistedState {
    bool plugged_in{false};
    std::optional<API_types::ev_simulator::ChargeMode> last_mode;
    std::optional<API_types::ev_simulator::ScenarioName> last_scenario;
};

// Hand-rolled JSON conversion. We can't use NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE
// here because nlohmann::json's adl resolution can't find to_json/from_json
// for the API_types enums (their json_codec is in `private_include`). The
// optional enum fields are persisted as their public string form via
// `serialize`/`deserialize` from the typed API codec.
void to_json(nlohmann::json& j, const PersistedState& s);
void from_json(const nlohmann::json& j, PersistedState& s);

class FsmContext {
public:
    using Publisher = std::function<void(const std::string&, const std::string&)>;
    using TimerArm = std::function<void(std::chrono::milliseconds)>;
    using TimerCancel = std::function<void()>;
    using TickArm = std::function<void(int)>;
    using TickDisarm = std::function<void()>;
    using ScenarioEnqueue = std::function<void(Event)>;
    using ScenarioTimerArm = std::function<void(std::chrono::seconds)>;

    FsmContext(PeerHandles peers, PeerActions actions, Publisher pub, TimerArm timer_arm, TimerCancel timer_cancel,
               TickArm tick_arm, TickDisarm tick_disarm, ScenarioEnqueue enqueue_event,
               ScenarioTimerArm scenario_timer_arm, const Conf& cfg, const ev_API::Topics& topics);

    SimVars vars;
    PersistedState persisted;
    everest::lib::util::monitor<SimSnapshot> snapshot;
    PeerHandles peers;
    PeerActions peer_actions;
    ScenarioDispatcher scenario;
    const Conf& cfg;

    // CP / power shortcuts
    void set_cp(::types::ev_board_support::EvCpState);
    void allow_power_on(bool);

    // BSP fault state clear (used by transition_to_disabled)
    void clear_diode_fail();
    void clear_rcd_error();

    // AC params shortcut
    void bsp_apply_ac_params(float current_a, bool three_phases);

    // ISO 15118 shortcuts (guard peers.iso nullptr via peer_actions presence)
    bool iso_start_charging(API_types::ev_simulator::ChargeMode, std::optional<API_types::ev_simulator::PaymentOption>,
                            int32_t departure_time_s, int32_t e_amount_wh);
    void iso_stop_charging();
    void iso_pause_charging();
    void iso_update_soc(float pct);

    // SLAC shortcut (guard peers.slac nullptr via peer_actions presence)
    bool slac_trigger_matching();

    // KVS shortcuts (silent no-op if peer_actions members unset OR
    // cfg.keep_cross_boot_plugin_state == false)
    void kvs_load();
    void kvs_save();

    // State timer (per-state deadline; default StateBase::leave() cancels)
    void arm_timer(std::chrono::milliseconds ms) {
        timer_arm_(ms);
    }
    void cancel_timer() {
        timer_cancel_();
    }

    // SoC tick (Charging.enter arms; Charging.leave disarms)
    void arm_tick(int interval_ms) {
        tick_arm_(interval_ms);
    }
    void disarm_tick() {
        tick_disarm_();
    }

    // Scenario seams: states reach the runtime queue and scenario timer
    // through injected callbacks so they never depend on EvSimRuntime.
    void enqueue(Event ev) {
        enqueue_event_(std::move(ev));
    }
    void arm_scenario_timer(std::chrono::seconds s) {
        scenario_timer_arm_(s);
    }

    // External publish helpers (each also updates `snapshot` for
    // snapshot-observable fields).
    void publish_e2m_state(API_types::ev_simulator::FsmState);
    void publish_e2m_fault(API_types::ev_simulator::FaultReport);
    void publish_e2m_iso_session_event(API_types::ev_simulator::IsoSessionEvent);
    void publish_e2m_ev_info();
    void publish_e2m_slac_state();
    void publish_e2m_bsp_event(const ::types::board_support_common::BspEvent&);
    void publish_e2m_bsp_measurement(const BspMeasurementPayload&);
    void publish_e2m_command_ack(const std::string& command, const std::string& reason); // Rejected only

    // Internal-side mirror (consumed by EvAPI)
    void publish_internal_bsp_event(const ::types::board_support_common::BspEvent&);
    void publish_internal_ev_info();

private:
    Publisher publisher_;
    TimerArm timer_arm_;
    TimerCancel timer_cancel_;
    TickArm tick_arm_;
    TickDisarm tick_disarm_;
    ScenarioEnqueue enqueue_event_;
    ScenarioTimerArm scenario_timer_arm_;
    const ev_API::Topics& topics_;
};

// Free helpers — implemented in FsmContext.cpp so the state header for
// Faulted / Disabled can be included only at the implementation site,
// avoiding a cycle with state .hpps that forward-declare FsmContext.
StateBase::Result transition_to_fault(FsmContext& ctx, const API_types::ev_simulator::InjectFaultParams& p);
StateBase::Result handle_query_state(FsmContext& ctx, API_types::ev_simulator::FsmState s);
StateBase::Result transition_to_disabled(FsmContext& ctx);

} // namespace module
