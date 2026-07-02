---
date: 2026-05-18T00:00:00Z
researcher: Martin Litre
git_commit: 7a21978c817298c4e1739415df6d1a906e62c944
branch: feat/ev-simulator
repository: everest-core
topic: "EvSimulator module — what it is and how it works"
tags: [research, codebase, EvSimulator, simulation, fsm, mqtt, ev-side]
status: complete
last_updated: 2026-05-18
last_updated_by: Martin Litre
---

# Research: EvSimulator module — what it is and how it works

**Date**: 2026-05-18
**Researcher**: Martin Litre
**Git Commit**: 7a21978c817298c4e1739415df6d1a906e62c944
**Branch**: feat/ev-simulator
**Repository**: everest-core
**Worktree**: `.worktrees/evsim-types`

## Research Question
What is the `EvSimulator` module in the `evsim-types` worktree, and how does it work?

## Summary

`EvSimulator` is a statically-typed replacement for the legacy `EvManager`. It models a vehicle during a charging session using an `fsm::v2::FSM` state machine driven by one `epoll`-based event loop on a dedicated background thread. External control arrives as JSON-over-MQTT on versioned topics (`everest_api/v1/ev_simulator/<id>/m2e/<verb>`); state and telemetry are published back on the matching `e2m/<var>` topics. It also implements the EvManager-compatible `ev_manager` provided interface so existing framework subscribers keep working.

Key design points:
- Single owning thread for the FSM and all peer (BSP/ISO/SLAC/KVS) calls — no FSM-level locking.
- MQTT and peer callbacks only parse + enqueue; they never touch FSM state.
- Four `timerfd`s multiplexed on one epoll loop: wake, per-state deadline, SoC tick, scenario step.
- 11 FSM states covering AC-IEC, ISO15118, SLAC, pause/resume (BCB toggle), fault injection, and disable.
- Scenario presets + SoC integrator make sessions self-driving for tests.

Module root: `modules/EV/EvSimulator/`.

## Detailed Findings

### Interfaces (`modules/EV/EvSimulator/manifest.yaml`)

**Provides** (`manifest.yaml:98-101`):
- `ev_manager` — publishes `bsp_event` and `ev_info`; EvManager-compatible.

**Requires** (`manifest.yaml:102-116`):
- `ev_board_support` (mandatory, ≥1) — CP state, power permission, AC params, diode fail, RCD error; source of BSP event/measurement/ev_info.
- `ev` / `ISO15118_ev` (optional 0–1) — `start/stop/pause_charging`, `update_soc`, BPT/SAE params.
- `slac` / `ev_slac` (optional 0–1) — `trigger_matching`.
- `kvs` (optional 0–1) — persist plug state when `keep_cross_boot_plugin_state=true`.
- `enable_external_mqtt: true` (`manifest.yaml:117`) — raw MQTT for the versioned topic API.

### Lifecycle (`EvSimulator.cpp`)

- `init()` (`EvSimulator.cpp:32-36`): `invoke_init(p_ev_manager)` (no-op) + `topics.setup(info.id, "ev_simulator", 1)`.
- `ready()` (`EvSimulator.cpp:38-81`):
  1. `invoke_ready(p_ev_manager)` (no-op).
  2. Construct `EvSimRuntime` → builds `PeerActions` + `FsmContext` (seeds `SimVars` from `Conf`, parses `on_battery_full` at `FsmContext.cpp:48-79`).
  3. `register_m2e_subscriptions()` — MQTT command handlers.
  4. `register_peer_subscriptions()` — BSP/SLAC/ISO peer subscriptions.
  5. `kvs_load()` — load `PersistedState` before loop thread starts (race-free vs queued events).
  6. `comm_check.start(...)` + heartbeat generator.
  7. Spawn `loop_thread` → `runtime->run(loop_online)`.
- `run()` (`EvSimRuntime.cpp:189-213`): register 4 fds, construct `FSM<StateBase>` starting in `Disabled`, blocking `loop.run(online)`.
- Destructor (`EvSimulator.cpp:84-103`): stop heartbeat, `loop_online=false`, `wake()`, join thread; runtime dtor detaches MQTT subscriptions before queue/ctx teardown.

### State Machine (`main/states/`)

`fsm::v2::FSM<StateBase>`; `StateBase::Result{bool unhandled, unique_ptr<StateBase> new_state}`. `StateBase::leave()` always `ctx.cancel_timer()` after `on_leave()`.

| State | enter() side effects | Key transitions |
|---|---|---|
| `Disabled` | publish `state=Disabled` | `Enable`→`Unplugged` |
| `Unplugged` | CP→A, power off, reset session/fault, `kvs_save`, `scenario.reset` | `Plug`→`Plugged`; `RunScenario` in place |
| `Plugged` | CP→B, power off, `mark_plugged_in(true)`, `kvs_save` | `StartSession(AcIec)` stays; `StartSession(ISO/SLAC)`→`SlacMatching`; valid-PWM BspMeasurement (AcIec)→`Charging`; `Unplug`→`Unplugged` |
| `SlacMatching` | `slac_trigger_matching()`, 30s deadline | `SlacState(MATCHED)`→`V2GNegotiating`; deadline→`Faulted(SlacTimeout)` |
| `V2GNegotiating` | `iso_start_charging()` (ETM from ChargeMode/BPT/MCS), 60s deadline | `IsoPowerReady`→`Charging`; `IsoStop`/`Unplug`→`Stopping`; deadline→`Faulted(V2GTimeout)` |
| `Charging` | CP→C, power on, arm periodic tick, splice pending ChargingCurve | `StopSession(AcIec)`→`Unplugged`; `StopSession(ISO)`→`Stopping`; `Pause`/`IsoPause`→`Paused`; PWM-off (AcIec)→`ChargingPwmPaused`; `SetChargingCurrent` ramp/immediate |
| `ChargingPwmPaused` | CP→B, power off | valid PWM→`Charging`; `StopSession`→`Unplugged` |
| `Paused` | CP→B, power off, `iso_pause_charging()`, 1h deadline | `Resume`→`BcbToggling(6)`; `BcbToggle`→`BcbToggling(count*2)`; deadline/`Unplug`→`Stopping` |
| `BcbToggling` | CP→B, 250ms deadline | each deadline toggles CP B/C, decrement; 0→`V2GNegotiating` |
| `Stopping` | `iso_stop_charging()`, power off, CP→B, 10s deadline | `IsoV2GFinished`/deadline→`Unplugged` |
| `Faulted` | reset session, drive BSP fault (diode_fail/rcd_error/CP→E), publish fault+state | `ClearFault`/`Unplug`→`Unplugged`; `on_leave` clears fault |

All states (except themselves): `InjectFault`→`Faulted`, `Disable`→`Disabled`. Disconnect handled uniformly by `StateBase::handle_disconnect()`.

### SoC tick (`main/SocIntegrator.cpp:81-175`)

`on_tick()` every `tick_interval_ms` while `Charging`:
1. `ramp_step()` — interpolate current `start_a`→`target_a` over duration, `bsp_apply_ac_params()`.
2. `soc_step()` — power: AC `current*voltage*phases`, DC `present_current*present_voltage`; integrate Wh, update `soc_pct`, apply `on_battery_full` policy (clamp / idle_at_full / stop_session / pause_if_iso) on rising edge at `battery_full_threshold_pct`; publish `e2m/ev_info` + `p_ev_manager.publish_ev_info()`; `iso_update_soc()`. DC present values overwritten live from ISO peer `ev_info`.

### Scenario dispatcher (`main/ScenarioDispatcher.cpp`)

`start(name, ctx)` builds timestamped step-list from presets: `AcIecBasic`, `AcIsoBasic`, `DcIsoBasic`, `AcIecPauseResume`, `DcIsoPauseResume`, `AcIsoD20Basic`, `DcIsoD20Basic`, `DcIsoBpt`, `DcIsoMcs`, `AcIecRampUp`, `DcIsoTaper`, `DiodeFailSmoke`. `at<=0` immediate; future via `scenario_timer_fd`. `mark_loop`/`try_rewind_loop` for ChargingCurve looping; `Charging::enter()` splices `pending_curve`.

### MQTT topics

Inbound `everest_api/v1/ev_simulator/<id>/m2e/<verb>`, outbound `e2m/<var>` (`Topics.cpp:37-39`, `EvSimulator.cpp:35`).

Inbound (`CommandRouter.cpp:34-181`): `enable`, `plug`, `unplug`, `stop_session`, `pause_session`, `resume_session`, `clear_fault`, `query_state`, `set_soc`, `start_session`, `set_charging_current`, `inject_fault`, `bcb_toggle`, `run_scenario`, `communication_check` (direct, not queued), `raise_error`, `clear_error`. All run on framework MQTT worker thread → parse + `rt.enqueue()` only.

Outbound (`FsmContext.cpp`): `state`, `fault`, `iso_session_event`, `ev_info`, `slac_state`, `bsp_event`, `bsp_measurement` (gated by `publish_bsp_measurements`), `command_ack`, `heartbeat`.

Peer subs (`PeerSubscriptions.cpp`): BSP always (`bsp_event`/`bsp_measurement`/`ev_info`); SLAC `subscribe_state`; ISO `ev_power_ready`/`stop_from_charger`/`v2g_session_finished`/`dc_power_on`/`pause_from_charger`/`ac_evse_max_current`/`ac_evse_target_power`. BSP event + EvInfo also forwarded to `p_ev_manager` provided interface and external `e2m`.

### Config (`manifest.yaml:4-97` → `Conf` `EvSimulator.hpp:42-64`)

`connector_id`, `ac_nominal_voltage`(230), `max_current_a`(16), `three_phases`(true), `dc_max_current_limit`(300), `dc_max_power_limit`(150000), `dc_max_voltage_limit`(900), `dc_energy_capacity`(60000), `dc_target_current`(5), `dc_target_voltage`(200), `soc_initial_pct`(30), `departure_time_s`(86400), `e_amount_wh`(0), `force_payment_option`(false), `keep_cross_boot_plugin_state`(false), `publish_bsp_measurements`(false), `tick_interval_ms`(100), `on_battery_full`("clamp"), `battery_full_threshold_pct`(100), `cfg_communication_check_to_s`(5), `cfg_heartbeat_interval_ms`(1000).

### Threading & timers

Threads: main EVerest (init/ready), `loop_thread` (sole FSM/peer owner, blocks in epoll), framework `external_mqtt_worker_thread` (parse+enqueue only), CommCheck heartbeat thread.

`enqueue()` (`EvSimRuntime.cpp:224-227`) pushes `thread_safe_queue<Event>` + bumps `wake_fd` eventfd; epoll `on_wake()` (`EvSimRuntime.cpp:229-282`) drains queue. Eventfd counter is durable kernel state → race-free vs early events.

Four timerfds in `EvSimRuntime`: `wake_fd` (enqueue wake), `state_timer_fd` (per-state deadline; armed `FsmContext::arm_timer`, cancelled `StateBase::leave`), `tick_fd` (SoC tick; armed `Charging::enter`, disarmed `Charging::on_leave`), `scenario_timer_fd` (scenario steps). One `fd_event_handler` serializes all → no extra locking.

## Code References

- `modules/EV/EvSimulator/EvSimulator.cpp:32-103` — init/ready/dtor
- `modules/EV/EvSimulator/manifest.yaml:4-117` — config + provides/requires
- `modules/EV/EvSimulator/EvSimulator.hpp:42-64` — `Conf`
- `modules/EV/EvSimulator/main/EvSimRuntime.cpp:44-303` — runtime, run loop, wake/tick
- `modules/EV/EvSimulator/main/FsmContext.cpp:48-79` — SimVars seed + policy parse
- `modules/EV/EvSimulator/main/SocIntegrator.cpp:81-175` — ramp + SoC integration
- `modules/EV/EvSimulator/main/CommandRouter.cpp:34-181` — MQTT command handlers
- `modules/EV/EvSimulator/main/PeerSubscriptions.cpp` — peer subscriptions
- `modules/EV/EvSimulator/main/ScenarioDispatcher.cpp` — scenario presets
- `modules/EV/EvSimulator/main/Events.hpp` — event types
- `modules/EV/EvSimulator/main/EventDispatch.cpp:33-54` — FSM exception isolation
- `modules/EV/EvSimulator/main/states/*.{cpp,hpp}` — 11 FSM states
- `modules/EV/EvSimulator/ev_manager/ev_managerImpl.cpp` — provided-interface shim (no-op init/ready)
- `modules/EV/EvSimulator/tests/` — 13 GTest binaries (state transitions, ramp, SoC, scenario, command router, silent-failure fixes, timer flush)
- `tests/core_tests/evsimulator_smoke_test.py` — Python integration smoke test
- `config/config-sil-evsim*.yaml` — 5 SIL wiring configs (AC, DC, DC-d20, DC-isomux, DC-bpt)
- `modules/EV/EvSimulator/docs/index.rst` + `docs/images/*.mmd/*.png` — architecture + state-machine diagrams

## Architecture Insights

- **Single-writer FSM**: all mutation on `loop_thread`; callbacks only enqueue → no FSM locks, deterministic event order.
- **Durable wake via eventfd**: queue + eventfd counter avoids the classic "event before loop start" race.
- **fd multiplexing**: 4 timerfds on one epoll = serialized timer + event handling, simplest correct concurrency model.
- **Versioned MQTT API**: `everest_api/v1/...` topic scheme decouples external control from internal interface contracts.
- **Backward compat**: provided `ev_manager` interface keeps EvManager subscribers working unchanged; EvSimulator is a drop-in.
- **Self-driving for tests**: scenario presets + SoC integrator let a session run end-to-end without an external orchestrator.

## Historical Context (from thoughts/)

- `.worktrees/evsim-types/thoughts/shared/research/2026-05-18-evsimulator-simplification.md` — can the module be simplified?
- `.worktrees/evsim-types/thoughts/shared/plans/2026-05-18-evsimulator-simplification.md` — simplification + dead-code removal plan
- `thoughts/shared/plans/2026-05-11-evsimulator-module.md` — original module plan v2 (post-grill)
- `thoughts/shared/plans/2026-05-13-evsimulator-scenarios-and-tests.md` — scenario expansion + Python test corpus
- `thoughts/shared/plans/2026-05-18-evsim-review-fixes.md` — review-fix implementation plan
- `thoughts/shared/research/2026-05-13-evsimulator-scenarios-plan-review.md` — scenario plan review
- `thoughts/shared/research/2026-05-11-evmanager-architecture.md` — legacy EvManager (predecessor) architecture
- `thoughts/shared/research/2026-05-11-pyevjosev-tcp-kill-from-evmanager.md` — EvManager/PyEvJosev TCP teardown question

## Open Questions

- Relationship/migration path between `EvSimulator` and legacy `EvManager` (deprecation timeline?).
- Whether the simplification plan (2026-05-18) has landed at commit `7a21978c8` or is still pending.
