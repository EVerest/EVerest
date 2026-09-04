// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "EvSimulator.hpp" // for Conf, ev_managerImplBase, ev_board_supportIntf, etc.
#include "Events.hpp"
#include "ScenarioDispatcher.hpp"
#include "SocIntegrator.hpp" // for OnBatteryFull enum cached on FsmContext
#include "StateBase.hpp"

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

// Peer-action function-pointer pack. FsmContext invokes peer behaviors
// through these callbacks rather than calling `call_*` directly so that
// (a) the FSM stays decoupled from the ev-cli generated `*Intf` types (their
// `call_*` methods are non-virtual and require a real ModuleAdapter), and
// (b) tests can record calls without mocking the full interfaces.
//
// The real runtime wires these to `mod.r_*->call_*` and to
// `mod.p_ev_manager->publish_*`.
//
// The functions are grouped by the peer they belong to, each carrying a single
// `present` flag. "Is the ISO peer wired" is then one check rather than 14
// scattered nullable-function tests, and a partially wired peer (some functions
// set, some null) cannot be expressed: build_peer_actions sets a peer's
// functions and its `present` flag in the same block, so by construction
// `present == true` IFF every function of that peer is wired.
struct BspPeer {
    std::function<void(::types::ev_board_support::EvCpState)> set_cp;
    std::function<void(bool)> allow_power_on;
    std::function<void(float)> set_ac_max_current;
    std::function<void(bool)> set_three_phases;
    std::function<void(bool)> diode_fail;
    std::function<void(float /*rcd_mA*/)> set_rcd_error;
    // EvSimulator must enable the simulated BSP peer (e.g. YetiSimulator)
    // before its simulation loop runs — without this call the peer's
    // simulation_step is gated off, so set_cp updates never propagate to
    // CP-voltage / state-machine event publication on the EVSE side.
    std::function<void(bool)> enable;
    // The ev_board_support requirement is mandatory (min_connections=1) so the
    // real runtime always sets this. The flag is kept for uniformity with the
    // optional peers and so test fixtures can model an unwired BSP.
    bool present{false};
};

struct IsoPeer {
    std::function<bool(::types::iso15118::EnergyTransferMode, ::types::iso15118::PaymentOption,
                       int32_t /*departure_time_s*/, int32_t /*e_amount_wh*/, bool /*force_payment_option*/)>
        start_charging;
    std::function<void()> stop_charging;
    std::function<void()> pause_charging;
    std::function<void(float /*soc_pct*/)> update_soc;
    std::function<void()> enable_sae_j2847_v2g_v2h;
    std::function<void(const ::types::iso15118::DcEvBPTParameters&)> set_bpt_dc_params;
    std::function<void(const ::types::iso15118::DcEvParameters&)> set_dc_params;
    bool present{false};
};

struct SlacPeer {
    std::function<bool()> trigger_matching;
    bool present{false};
};

struct KvsPeer {
    std::function<void(const std::string& /*key*/, const std::string& /*json*/)> store;
    // Returns nullopt when the key is missing or stored as a non-string variant
    // alternative; returns optional<string>{""} when an empty payload is
    // persisted (kvs_load distinguishes these two cases).
    std::function<std::optional<std::string>(const std::string& /*key*/)> load_raw;
    bool present{false};
};

// Internal-side publishers (consumed by EvAPI / other in-module subscribers).
// Type-erased so FsmContext does not need ev_managerImplBase& injection.
struct InternalPublisher {
    std::function<void(const ::types::board_support_common::BspEvent&)> bsp_event;
    std::function<void(const ::types::evse_manager::EVInfo&)> ev_info;
    bool present{false};
};

// Out-of-band error raise/clear seam. The MQTT command router only parses the
// payload and enqueues a RaiseErrorCmd / ClearErrorCmd; the loop thread flushes
// it and drives these callbacks, so error raise/clear cannot interleave with
// Faulted::enter's fault publishing on a different thread. Type-erased for the
// same reason as InternalPublisher: FsmContext stays decoupled from
// ev_managerImplBase, and tests can record raise/clear without a real
// ModuleAdapter.
struct ErrorSink {
    std::function<void(const std::string& /*type*/, const std::string& /*sub_type*/, const std::string& /*message*/,
                       Everest::error::Severity)>
        raise;
    std::function<void(const std::string& /*type*/, const std::optional<std::string>& /*sub_type*/)> clear;
    // True when an error with the given type (+ optional sub_type) is currently
    // active in the framework. Used to surface a Rejected ack for a clear that
    // would otherwise be a silent no-op.
    std::function<bool(const std::string& /*type*/, const std::string& /*sub_type*/)> is_active;
    bool present{false};
};

struct PeerActions {
    BspPeer bsp;
    IsoPeer iso;
    SlacPeer slac;
    KvsPeer kvs;
    InternalPublisher publisher;
    ErrorSink error;
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

// Session-scoped state. Bundling the fields that are set together at session
// begin and torn down together at session end (Unplugged::enter /
// Faulted::enter / transition_to_disabled) makes "no active session" vs.
// "active session of mode X with these params" a single value, so teardown
// is one `vars.session.reset()` and cannot forget a field. Per-mode field
// presence (bpt only on D20, mcs only on DcIsoD20) is enforced upstream by
// the SessionConfigParams variant, so the running session state is uniform
// and a flat struct (not a variant) is sufficient.
struct Session {
    // Default kept deterministic (rather than an indeterminate enum) for
    // test fixtures that build a Session without an explicit mode; the
    // production session-begin path always assigns mode explicitly.
    API_types::ev_simulator::ChargeMode mode{API_types::ev_simulator::ChargeMode::AcIec};
    std::optional<API_types::ev_simulator::PaymentOption> payment;
    std::optional<API_types::ev_simulator::BptParams> bpt;
    bool mcs_enabled{false};
    // Curve pending splice into the scenario dispatcher on the next
    // Charging::enter. Set by Plugged when a session config carries a curve;
    // consumed (cleared) by Charging::enter after splicing. Tearing down the
    // session clears it too, so a curve from a prior session cannot leak.
    std::optional<API_types::ev_simulator::ChargingCurve> pending_curve;
};

struct SimVars {
    float battery_capacity_wh{60000};
    float battery_charge_wh{18000};
    float soc_pct{30.0f};
    float pwm_duty_cycle{0};
    // nullopt == no active session. Replaces the formerly independent
    // charge_mode / payment / bpt / mcs_enabled / pending_curve fields.
    std::optional<Session> session;
    std::string slac_state{"UNMATCHED"};
    // True once a SLAC UNMATCHED (D-LINK_TERMINATE) is observed after a match,
    // i.e. the SECC tore the SLAC link down. On AC this happens on pause; on DC
    // the link survives a pause, so this stays false. The EV-initiated resume
    // path (BcbToggling) re-matches SLAC for every ISO mode (gated on the charge
    // mode, not this flag) because the SECC re-enters MATCHING on the BCB wake-up
    // and waits for a fresh both-sides match; this flag is an additional trigger
    // so a non-ISO resume that still saw an UNMATCHED re-matches too. Cleared
    // when SlacMatching observes the new MATCHED.
    bool slac_unmatched{false};
    int32_t bcb_remaining{0};
    // Set by BcbToggling before it hands control to V2GNegotiating on an
    // EV-initiated resume. It tells V2GNegotiating to defer iso_start_charging
    // (and the 60 s deadline) until the SECC re-applies the CP PWM, mirroring
    // the proven EvManager resume sequence (iso_start_bcb_toggle ->
    // iso_wait_pwm_is_running -> iso_start_v2g_session). Issuing start_charging
    // before the SECC has woken up races its handshake and is the source of the
    // unstable resume. The first-session path (SlacMatching -> V2GNegotiating)
    // leaves this false and starts immediately.
    bool resume_awaiting_pwm{false};
    // Tracks whether a Josev V2G comm session is live. Josev runs exactly one
    // session per start_charging and publishes v2g_session_finished (->
    // IsoV2GFinished) when each session's loop returns, including on a pause.
    // Set when start_charging is issued (FsmContext::iso_start_charging),
    // cleared on IsoV2GFinished (EvSimRuntime::apply_passthrough_vars) and reset
    // on a fresh plug (Plugged::enter). The EV must not begin a resume (BCB
    // wake-up + re-SLAC) while the paused session is still tearing down: the
    // previous session's lagging SessionStop would otherwise reach the SECC
    // after the link is re-established and clobber it (SECC drops to
    // D-LINK_PAUSE + PWM off and never re-applies the charging PWM, so the
    // resume hangs). Gated in Paused::feed(ResumeSession).
    bool iso_session_active{false};
    // Set when ResumeSession arrives in Paused while iso_session_active is still
    // true; the resume (BcbToggling) is then deferred until IsoV2GFinished
    // arrives. Reset on each Paused entry.
    bool resume_pending{false};
    // The BCB edge count to apply when a deferred resume is released into
    // bcb_remaining (6 for an implicit ResumeSession, 2*count for an explicit
    // BcbToggle). Carried across the defer so the user-requested count survives
    // until IsoV2GFinished (or the bounded fallback deadline) fires.
    int32_t bcb_pending{0};
    std::optional<API_types::ev_simulator::FaultReport> last_fault;
    float charging_current_a{16.0f};
    bool three_phases{true};
    // Most recent AC limit communicated by the EVSE over ISO 15118
    // (ac_evse_max_current, or a per-phase current derived from
    // ac_evse_target_power). nullopt until the charger sends one; once set,
    // the applied charge current is clamped to min(configured, this limit)
    // so a SetChargingCurrent or session restart cannot exceed it.
    std::optional<float> evse_ac_max_current_a;
    std::optional<ActiveRamp> active_ramp;
    // DC live current/voltage. evse_dc_present_current_a holds the live
    // measured present current once a peer EvInfo (apply_passthrough_vars in
    // EvSimRuntime) reports one; it stays nullopt until then. SocIntegrator
    // reads it via effective_dc_current_a(), which closes the loop on the live
    // value when present (un-clamped, so BPT discharge can be negative) and
    // otherwise falls back open-loop to the configured cfg.dc_target_current.
    // dc_present_voltage_v is seeded from cfg.dc_target_voltage by the
    // FsmContext ctor and overwritten by a live present voltage when reported.
    std::optional<float> evse_dc_present_current_a;
    float dc_present_voltage_v{0.0f};
    // Edge-detection state for on_battery_full policies. SocIntegrator sets
    // this to true the first tick SoC reaches cfg.battery_full_threshold_pct
    // and clears it when SoC drops back below; the stop_session / pause_if_iso
    // policies fire only on the false -> true transition.
    bool was_full{false};
    int32_t departure_time_s{86400};
    int32_t e_amount_wh{0};
    bool force_payment_option{false};

    // Thin read accessors so the many call sites that previously read
    // vars.charge_mode / vars.bpt / vars.mcs_enabled / vars.payment keep the
    // same optional-shaped surface without reaching into vars.session.
    std::optional<API_types::ev_simulator::ChargeMode> charge_mode() const {
        if (session) {
            return session->mode;
        }
        return std::nullopt;
    }
    std::optional<API_types::ev_simulator::BptParams> bpt() const {
        return session ? session->bpt : std::nullopt;
    }
    bool mcs_enabled() const {
        return session && session->mcs_enabled;
    }
    std::optional<API_types::ev_simulator::PaymentOption> payment() const {
        return session ? session->payment : std::nullopt;
    }
    std::optional<API_types::ev_simulator::ChargingCurve> pending_curve() const {
        return session ? session->pending_curve : std::nullopt;
    }
};

struct PersistedState {
    bool plugged_in{false};
    std::optional<API_types::ev_simulator::SessionConfigParams> configured_session;
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
    using ScenarioTimerArm = std::function<void(std::chrono::milliseconds)>;

    FsmContext(PeerActions actions, Publisher pub, TimerArm timer_arm, TimerCancel timer_cancel, TickArm tick_arm,
               TickDisarm tick_disarm, ScenarioEnqueue enqueue_event, ScenarioTimerArm scenario_timer_arm,
               const Conf& cfg, const ev_API::Topics& topics);

    SimVars vars;
    PeerActions peer_actions;
    ScenarioDispatcher scenario;
    const Conf& cfg;
    // Parsed once from cfg.on_battery_full in the ctor; SocIntegrator reads
    // this on every tick instead of re-parsing the config string.
    OnBatteryFull on_battery_full_policy{OnBatteryFull::Clamp};

    // Latched session spec from the most recent accepted configure_session.
    // Lives on FsmContext (not SimVars) so it survives session teardown
    // (Unplugged::enter / Faulted::enter / transition_to_disabled all reset
    // vars.session but leave this intact) and is consumed at every plug.
    std::optional<API_types::ev_simulator::SessionConfigParams> configured_session;

    // CP / power shortcuts
    void set_cp(::types::ev_board_support::EvCpState);
    void allow_power_on(bool);

    // Enable / disable the BSP peer's simulation loop. Required because the
    // peer (e.g. YetiSimulator) gates its CP-voltage propagation and
    // state-machine event publication on its own simulation_enabled flag;
    // without this call set_cp updates are written into the peer but never
    // emerge as bsp_event / bsp_measurement publications back to us.
    void enable_bsp(bool);

    // BSP fault state clear (used by transition_to_disabled)
    void clear_diode_fail();
    void clear_rcd_error();

    // Set the EV's desired AC current and apply it clamped. Records the
    // intent (vars.charging_current_a = desired_a) and then routes through
    // bsp_apply_ac_params_clamped so the BSP sees min(desired, EVSE limit).
    void set_desired_ac_params(float desired_a, bool three_phases);

    // Pure apply: push the desired AC current to the BSP clamped against the
    // most recent EVSE limit (vars.evse_ac_max_current_a). When no EVSE limit
    // has been received the desired current is applied verbatim; otherwise the
    // smaller of the two is used. This NEVER writes vars.charging_current_a
    // (the desired/intent), so a transient EVSE ceiling can be lifted later and
    // the applied current recovers toward the retained desired.
    void bsp_apply_ac_params_clamped(float desired_a, bool three_phases);

    // Applied AC current = the EV desired (vars.charging_current_a) clamped to
    // the most recent EVSE limit (vars.evse_ac_max_current_a), via the same
    // clamp helper bsp_apply_ac_params_clamped() uses. The SoC energy integrator
    // (read side) and the BSP push (write side) therefore apply one identical
    // clamp and cannot diverge.
    float effective_ac_current_a() const;

    // Delivered DC current the SoC integrator should integrate. When a live
    // present current has been reported (vars.evse_dc_present_current_a) it is
    // returned verbatim — including negative values for BPT discharge — so the
    // closed loop tracks measured delivery. Otherwise it falls back open-loop
    // to cfg.dc_target_current clamped to [0, cfg.dc_max_current_limit], so DC
    // SoC still advances when no peer EvInfo producer is wired.
    float effective_dc_current_a() const;

    // Record the EVSE-communicated AC current limit (from
    // ac_evse_max_current) into vars.evse_ac_max_current_a.
    void note_evse_ac_max_current(float max_current_a);

    // Derive a per-phase current from an ac_evse_target_power payload and
    // record it as the EVSE limit: power / (nominal_voltage * active_phase_count),
    // where active_phase_count is the number of phases the charger drives,
    // inferred from the present per-phase targets. A payload without a
    // target_active_power leaves the limit unchanged.
    void note_evse_ac_target_power(const ::types::iso15118::AcTargetPower& target_power);

    // ISO 15118 shortcuts (guard peer_actions.iso.present)
    bool iso_start_charging(API_types::ev_simulator::ChargeMode, std::optional<API_types::ev_simulator::PaymentOption>,
                            int32_t departure_time_s, int32_t e_amount_wh);
    void iso_stop_charging();
    void iso_pause_charging();
    void iso_update_soc(float pct);

    // SLAC shortcut (guard peer_actions.slac.present)
    bool slac_trigger_matching();

    // Cross-boot persisted state. Held private because the plugged_in /
    // configured_session pair is an invariant unit: it is written from several
    // states (Plugged::enter, Unplugged::enter, transition_to_disabled) and
    // configure_session, and must stay consistent with what kvs_save
    // serializes. Mutation goes through the two narrow setters; reads go
    // through persisted_state(). kvs_load / kvs_save touch the field directly.
    void mark_plugged_in(bool plugged_in);
    void remember_session_config(const std::optional<API_types::ev_simulator::SessionConfigParams>& sp);
    const PersistedState& persisted_state() const {
        return persisted;
    }

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
    void arm_scenario_timer(std::chrono::milliseconds ms) {
        scenario_timer_arm_(ms);
    }

    // Out-of-band error raise/clear, invoked from the loop thread when a
    // RaiseErrorCmd / ClearErrorCmd is flushed. clear_error mirrors the prior
    // command-router behavior: when no matching error is active the clear is a
    // no-op, so a Rejected command_ack is published so the caller learns the
    // request did nothing.
    void raise_error(const RaiseErrorCmd& cmd);
    void clear_error(const ClearErrorCmd& cmd);

    // configure_session interceptor, invoked from the loop thread when a
    // ConfigureSession (SessionConfigParams) is flushed in on_wake — same
    // pre-FSM seam as raise_error/clear_error. Validates the spec; on success
    // latches it into configured_session, persists it, and publishes an
    // Accepted command_ack; on failure publishes a Rejected command_ack and
    // leaves configured_session unchanged.
    void configure_session(const API_types::ev_simulator::SessionConfigParams& sp);

    // Spec well-formedness check shared by configure_session: curve non-empty
    // and strictly monotonic t_offset_ms, plus ISO/SLAC peer presence for ISO
    // charge modes. Returns false and sets reject_reason on the first
    // violation.
    bool validate_session_config(const API_types::ev_simulator::SessionConfigParams& sp,
                                 std::string& reject_reason) const;

    // External publish helpers.
    void publish_e2m_state(API_types::ev_simulator::FsmState);
    void publish_e2m_fault(API_types::ev_simulator::FaultReport);
    void publish_e2m_iso_session_event(API_types::ev_simulator::IsoSessionEvent);
    void publish_e2m_ev_info();
    void publish_e2m_slac_state();
    void publish_e2m_bsp_event(const ::types::board_support_common::BspEvent&);
    void publish_e2m_bsp_measurement(const BspMeasurementPayload&);
    void publish_e2m_command_ack(const std::string& command, const std::string& reason);        // Rejected
    void publish_e2m_command_ack_accepted(const std::string& command, const std::string& note); // Accepted

    // Internal-side mirror (consumed by EvAPI)
    void publish_internal_bsp_event(const ::types::board_support_common::BspEvent&);
    void publish_internal_ev_info();

private:
    PersistedState persisted;
    Publisher publisher_;
    TimerArm timer_arm_;
    TimerCancel timer_cancel_;
    TickArm tick_arm_;
    TickDisarm tick_disarm_;
    ScenarioEnqueue enqueue_event_;
    ScenarioTimerArm scenario_timer_arm_;
    const ev_API::Topics& topics_;
};

// True for the ISO 15118 charge modes (AcIso2 / AcIsoD20 / DcIso2 / DcIsoD20),
// i.e. the modes that run SLAC. AcIec is plain IEC 61851 (no SLAC) and is NOT
// an ISO mode. Shared so the SoC integrator, session-config validation, and
// the resume re-match path apply one identical definition.
bool is_iso_mode(API_types::ev_simulator::ChargeMode m);

// Free helpers — implemented in FsmContext.cpp so the state header for
// Faulted / Disabled can be included only at the implementation site,
// avoiding a cycle with state .hpps that forward-declare FsmContext.
StateBase::Result transition_to_fault(FsmContext& ctx, const API_types::ev_simulator::InjectFaultParams& p);
StateBase::Result handle_query_state(FsmContext& ctx, API_types::ev_simulator::FsmState s);
StateBase::Result transition_to_disabled(FsmContext& ctx);

} // namespace module
