# EvSimulator: configure-before-plug Implementation Plan

## Overview

Replace the EvSimulator `start_session` command with a `configure_session` command issued independently of plug. After configuration, driving a simulated charging session is reduced to the four physical verbs `plug`, `unplug`, `stop_session`, `pause_session` (plus `resume_session`). Session parameters (mode, payment, BPT, MCS, curve, current) are latched once into the context and consumed at every plug-in.

## Current State Analysis

`start_session` carries the full session spec as the `StartSessionParams` variant and is only accepted in `Plugged`. `Plugged::feed`'s `StartSession` arm parses the variant, validates the curve, falls back to cfg defaults, checks ISO/SLAC peer presence, commits a `Session` into `ctx.vars.session`, then transitions: `AcIec` stays in `Plugged` (waits for PWM via the existing `BspMeasurement` arm), ISO modes go to `SlacMatching`. Every driving session therefore re-supplies the entire spec on each plug.

Key code:
- `modules/EV/EvSimulator/main/Events.hpp:33,121,130` — `EventKind::StartSession`, variant alternative, `variant_size == 30` lockstep `static_assert`.
- `modules/EV/EvSimulator/main/CommandRouter.cpp:107-113` — `start_session` verb subscription.
- `modules/EV/EvSimulator/main/states/Plugged.cpp:32-163` — StartSession handler (resolve/validate/commit/branch).
- `modules/EV/EvSimulator/main/states/Plugged.cpp:164-171` — AcIec PWM → `Charging`.
- `modules/EV/EvSimulator/main/EvSimRuntime.cpp:229-282` — `on_wake` queue flush; RaiseError/ClearError pre-FSM intercept seam.
- `modules/EV/EvSimulator/main/FsmContext.hpp:140-205` — `Session`, `SimVars`, `PersistedState{plugged_in,last_mode}`.
- `modules/EV/EvSimulator/main/FsmContext.cpp:211-247` — `remember_last_mode`, `kvs_load`/`kvs_save`.
- `modules/EV/EvSimulator/main/ScenarioDispatcher.cpp:51-236` — 12 presets emitting `plug`+`start_session`.
- API types (hand-written, no YAML codegen): `lib/everest/everest_api_types/include/everest_api_types/ev_simulator/API.hpp:183` (`StartSessionParams`), `private_include/.../json_codec.hpp`, `src/.../json_codec.cpp:637-745`, `src/.../codec.cpp:30-49` (`mode_of`), `src/.../wrapper.cpp`, `tests/test_file_autogenerator/disable.csv`.

### Key Discoveries
- `persisted.last_mode` and `persisted.plugged_in` are write-only: serialized to KVS, never read back to restore behavior (`FsmContext.cpp:217-239` only loads into `persisted`; no FSM/runtime reader).
- `enter()` is `void`; a state self-advances by enqueuing a synthetic event flushed by `on_wake` — established idiom at `modules/EV/EvSimulator/main/states/SlacMatching.cpp:24-30`.
- `publish_e2m_command_ack` is Rejected-only (`FsmContext.hpp:321`); `CommandAck`/`CommandAckStatus` already define `Accepted` (`API.hpp:74,252`).
- Pre-FSM intercept seam for pure-data commands already exists for RaiseError/ClearError in `on_wake` (`EvSimRuntime.cpp:229-282`).
- `mode_of(SessionConfigParams&)` (`codec.cpp:30-49`) recovers the `ChargeMode` discriminant from the active variant alternative — reused for default synthesis and validation.

## Desired End State

- No `start_session` MQTT verb; no `StartSession` `EventKind`/variant alternative.
- `configure_session` verb accepted in any state; payload validated (curve monotonic + ISO/SLAC peer presence for mode) at receipt; on success latched into `ctx.configured_session` with an **Accepted** `command_ack`; on failure rejected and not latched.
- `plug` with a latched config consumes it at `Plugged::enter`; with no latched config synthesizes `AcIec` from `cfg.max_current_a`/`cfg.three_phases`.
- ISO modes auto-advance to `SlacMatching`; `AcIec` waits for PWM → `Charging`. Plug → `Plugged` invariant preserved.
- `PersistedState = {plugged_in, optional<SessionConfigParams>}`; `last_mode` removed; `kvs_load` seeds `ctx.configured_session`.
- All 12 scenario presets emit a configure step then a plug step at t=0.
- C++ unit tests, `evsimulator_smoke_test.py`, and `docs/index.rst` + mermaid reflect the new model.

Verify: `ctest -R EvSimulator` green; `everest_api_types` serialization tests green; `evsimulator_smoke_test.py` green; grep shows zero `start_session`/`StartSession` references outside historical thoughts.

## What We're NOT Doing

- Not renaming `stop_session`/`pause_session`/`resume_session`.
- Not adding a cross-boot session-restore reader (persistence stays write-path-only; only the persisted shape changes).
- Not changing `set_charging_current`/`set_soc`/`bcb_toggle`/`inject_fault`/error verbs.
- Not altering FSM states beyond `Plugged` entry and the exhaustive-switch updates the event-vocabulary change forces.
- Not introducing a YAML codegen path for ev_simulator API types (they stay hand-written).

## Implementation Approach

Bottom-up: rename the API type and adjust codec first (mechanical, no behavior change), then mutate the event vocabulary, then add the loop-thread interceptor + validation, then relocate the resolve/commit logic into `Plugged::enter` + `BeginSessionEvt`, then persistence, then scenarios, then tests/docs. Each phase is TDD-ordered (test first, except pure rename/codegen-shaped edits which are verified by the build per the no-red-phase-for-codegen rule).

### Branch / parallelization

Work continues on the existing worktree branch `feat/ev-simulator` (`.worktrees/evsim-types`). Workstreams:
- **WS-A (serial spine)**: Phase 1 → 2 → 3 → 4 → 5. Each depends on the prior (type name, then events, then interceptor, then Plugged, then persistence).
- **WS-B (parallel after Phase 2)**: Phase 6 scenario rework — depends only on the event vocabulary (Phase 2) + type name (Phase 1), independent of Phases 3–5.
- **WS-C (parallel, continuous)**: Phase 7 docs (`docs/index.rst` + mermaid) — depends only on the finalized design, can be drafted alongside WS-A and finalized last.

Maintain a sibling progress file `~/.claude/plans/2026-05-18-evsimulator-configure-before-plug-progress.md` (commits landed + decisions log).

---

## Phase 1: API type rename `StartSessionParams` → `SessionConfigParams`

### Overview
Mechanical rename of the variant type and its codec/wrapper/test-autogen surface. No behavior change.

### Changes Required

#### 1. API type
**File**: `lib/everest/everest_api_types/include/everest_api_types/ev_simulator/API.hpp`
- Rename `using StartSessionParams = std::variant<...>` → `SessionConfigParams` (line ~183).
- Rename `ChargeMode mode_of(const StartSessionParams&)` declaration (line ~189).

#### 2. Codec
**Files**: `private_include/everest_api_types/ev_simulator/json_codec.hpp`, `src/everest_api_types/ev_simulator/json_codec.cpp` (`to_json`/`from_json` ~637-745), `include/everest_api_types/ev_simulator/codec.hpp`, `src/everest_api_types/ev_simulator/codec.cpp` (`mode_of` ~30-49, `serialize`/`deserialize`/`operator<<` specializations), `src/everest_api_types/ev_simulator/wrapper.*` if referenced.
- Replace every `StartSessionParams` token with `SessionConfigParams`.

#### 3. Test autogen
**File**: `lib/everest/everest_api_types/tests/test_file_autogenerator/disable.csv`
- Update the disabled type entry name. Update `tests/manual_tests/serialization/ev_simulator.cpp` references.

### Success Criteria

#### Automated Verification
- [x] `grep -rn "StartSessionParams" lib/everest/everest_api_types modules/EV/EvSimulator` returns nothing.
- [x] Library builds: `ninja -C build everest_api_types -j$(nproc)`.
- [x] Serialization tests pass: `everest-core_API_serialize_tests --gtest_filter='ev_simulator.*'` (41/41).

#### Manual Verification
- [ ] JSON wire shape unchanged (`{"mode":...,"params":{...}}`) — confirmed by passing round-trip tests.

**Implementation Note**: Pure rename; no red phase (codegen-shaped edit, verified by build). Pause for manual confirmation before Phase 2.

---

## Phase 2: Event vocabulary — remove StartSession, add ConfigureSession + BeginSessionEvt

### Overview
Swap the event surface and the MQTT verb wiring. After this phase `configure_session` is received and enqueued (interceptor lands in Phase 3); `start_session` no longer exists.

### Changes Required

#### 1. Events
**File**: `modules/EV/EvSimulator/main/Events.hpp`
- Remove `EventKind::StartSession`; add `EventKind::ConfigureSession` and `EventKind::BeginSession`.
- Variant: remove `API_types::ev_simulator::StartSessionParams`; add `API_types::ev_simulator::SessionConfigParams` and a payloadless `struct BeginSessionEvt {}`.
- Update `static_assert(std::variant_size_v<EventPayload> == 31, ...)`.
- Update `detail::default_payload_for`, `kind_of`, `command_verb` (`configure_session` for ConfigureSession; `""` for internal BeginSession) in `Events.cpp`.

#### 2. Command router
**File**: `modules/EV/EvSimulator/main/CommandRouter.cpp:107-113`
- Replace the `start_session` subscription with `configure_session` deserializing `SessionConfigParams` and `rt.enqueue(Event{p})`.

#### 3. Exhaustive switches
**Files**: all 11 `modules/EV/EvSimulator/main/states/*.cpp` + `FsmContext.cpp` helpers
- Replace `case EK::StartSession:` arms. `ConfigureSession` is intercepted pre-FSM (Phase 3) so list it in the "intercepted on loop thread" exhaustive-only group alongside RaiseError/ClearError. `BeginSession` is handled in `Plugged` (Phase 4); every other state lists it in the no-op exhaustive group.

### Success Criteria

#### Automated Verification
- [x] Module compiles `-Werror=switch`: `ninja -C build EvSimulator -j$(nproc)`.
- [x] `grep -rn "StartSession"` in EvSimulator code (main+tests) returns nothing (docs `.rst`/`.mmd` deferred to Phase 7 / WS-C).
- [x] Existing EvSimulator unit tests build (some runtime-fail until Phases 3–4 — expected): `ninja -C build everest-core_EvSimulator_tests -j$(nproc)`.

#### Manual Verification
- [ ] `configure_session` topic appears in `mqtt.subscribe` set (log inspection on a SIL run).

**Implementation Note**: TDD — add a CommandRouter test asserting `configure_session` deserializes and enqueues a `SessionConfigParams` event (red → green). Pause before Phase 3.

---

## Phase 3: Loop-thread ConfigureSession interceptor + validation

### Overview
Intercept `ConfigureSession` in `on_wake` before `fsm.feed` (RaiseError/ClearError seam). Validate, then latch or reject.

### Changes Required

#### 1. Context state + ack
**File**: `modules/EV/EvSimulator/main/FsmContext.hpp` / `.cpp`
- Add `std::optional<API_types::ev_simulator::SessionConfigParams> configured_session;` to `FsmContext` (not `SimVars` — survives session teardown).
- Add an Accepted-capable ack helper, e.g. `void publish_e2m_command_ack_accepted(const std::string& command, const std::string& note)` (or generalize the existing helper with a status arg); emit `CommandAck{command, Accepted, note}`.
- Add `bool validate_session_config(const SessionConfigParams&, std::string& reject_reason) const` — curve non-empty + strictly monotonic `t_offset_ms` (logic lifted from `Plugged.cpp:89-100`) and ISO/SLAC peer presence for ISO modes (`mode_of` + `peer_actions.iso.present`/`slac.present`, logic from `Plugged.cpp:118-125`).

#### 2. Interceptor
**File**: `modules/EV/EvSimulator/main/EvSimRuntime.cpp:229-282`
- In the `on_wake` drain, before `fsm.feed`, match `ConfigureSession`: run `validate_session_config`. On failure → `ctx.publish_e2m_command_ack("configure_session", reason)` (Rejected), do not latch. On success → `ctx.configured_session = params`, `ctx.kvs_save()`, Accepted ack ("configuration latched; applies at next plug").

### Success Criteria

#### Automated Verification
- [x] Unit test: valid config latches + Accepted ack: new `tests/ConfigureSessionTest.cpp`.
- [x] Unit test: non-monotonic curve → Rejected ack, `configured_session` stays nullopt.
- [x] Unit test: ISO mode with no slac/iso peer → Rejected ack.
- [x] `[evsim][configure_session]` green (27 assertions). Pre-existing Plugged/scenario cases fail pending P4/P6 (expected).

#### Manual Verification
- [ ] On SIL: publishing `configure_session` in `Unplugged` yields an `e2m/command_ack` Accepted; a bad curve yields Rejected.

**Implementation Note**: TDD red→green per bullet. Pause before Phase 4.

---

## Phase 4: Plugged resolve + BeginSessionEvt auto-progress

### Overview
Relocate the ex-StartSession resolve/commit/mode-branch into a `Plugged`-entry path driven by `BeginSessionEvt`, sourcing the spec from `ctx.configured_session` or an `AcIec` default.

### Changes Required

#### 1. Plugged entry
**File**: `modules/EV/EvSimulator/main/states/Plugged.cpp`
- `Plugged::enter()`: after the existing CP-B/power-off/mark-plugged/publish, `ctx.enqueue(Event{BeginSessionEvt{}})`.
- `Plugged::feed`: add `case EK::BeginSession:` running the relocated logic:
  - Resolve spec: `ctx.configured_session` if set, else synthesize `AcIecSessionParams{charging_current_a=cfg.max_current_a, three_phases=cfg.three_phases}` wrapped as `SessionConfigParams`.
  - Reuse the existing field-flattening `std::visit` (`Plugged.cpp:48-84`), cfg-default fallback logging (`:104-117`), `bsp_apply_ac_params` for AC modes, `departure_time_s`/`e_amount_wh` assignment, `Session` commit (`:144-152`). Curve/peer validation is NOT repeated (done at configure time, Phase 3); a synthesized default has no curve.
  - Branch: `AcIec` → `{false, nullptr}` (existing `BspMeasurement` arm at `:164-171` still drives → `Charging`); ISO modes → `{false, std::make_unique<SlacMatching>(ctx)}`.
- Remove the old `case EK::StartSession:` body entirely.

#### 2. Cleanup
- Drop `remember_last_mode` call (`Plugged.cpp:135`) — superseded by Phase 5 persistence.

### Success Criteria

#### Automated Verification
- [x] Unit test: plug with latched `AcIec` config → `Plugged`; AcIec end-to-end drives PWM → `Charging` (StateTransitionsTest).
- [x] Unit test: plug with latched `DcIsoD20` config → `SlacMatching`.
- [x] Unit test: plug with NO config → `AcIec` synthesized from cfg defaults (16A/3ph).
- [x] Unit test: reconfigure mid-session leaves live session; next unplug→plug uses the new config.
- [x] EvSimulator suite 41/42 green; only `ScenarioDispatcherTest` (3 assertions) fails pending Phase 6.

#### Manual Verification
- [ ] SIL `config-sil-evsim.yaml`: `configure_session`(DcIsoD20) → `plug` drives a full DC ISO session end-to-end.
- [ ] SIL: bare `plug` with no configure drives an AC IEC session.

**Implementation Note**: TDD red→green per bullet. Pause before Phase 5.

---

## Phase 5: Persistence — SessionConfigParams, drop last_mode

### Overview
`PersistedState` carries the latched config; `kvs_load` seeds `ctx.configured_session`.

### Changes Required

#### 1. PersistedState
**File**: `modules/EV/EvSimulator/main/FsmContext.hpp:207-218` / `FsmContext.cpp:29-46`
- `struct PersistedState { bool plugged_in{false}; std::optional<API_types::ev_simulator::SessionConfigParams> configured_session; };` — remove `last_mode`.
- `to_json`/`from_json`: serialize `configured_session` via the existing `SessionConfigParams` codec (mirror the `last_mode` serialize/deserialize approach at `FsmContext.cpp:31-44`); remove `last_mode` handling. Remove `remember_last_mode` (`FsmContext.hpp:270`, `.cpp:211-213`).

#### 2. Load seeding
**File**: `modules/EV/EvSimulator/main/FsmContext.cpp:217-239` / call site `EvSimulator.cpp` `ready()` (post-`kvs_load`)
- After `kvs_load`, if `persisted.configured_session` set, `ctx.configured_session = persisted.configured_session`.
- `kvs_save` (Phase 3 latch, `Unplugged`/`Plugged` enter) persists `plugged_in` + `configured_session`.

### Success Criteria

#### Automated Verification
- [x] Unit test: remember_session_config → kvs_save → fresh ctx kvs_load → `configured_session` restored + seeds `ctx.configured_session`.
- [x] Unit test: legacy `last_mode` KVS blob loads without throw (plugged_in loads, configured_session nullopt).
- [x] `grep -rn "last_mode" modules/EV/EvSimulator` returns nothing (one explanatory comment in FsmContext.cpp aside).
- [x] EvSimulator suite 41/42 green; only `ScenarioDispatcherTest` (3 assertions) fails pending Phase 6.

#### Manual Verification
- [ ] SIL with `keep_cross_boot_plugin_state: true`: configure, restart manager, plug → restored config drives the session.

**Implementation Note**: TDD red→green. Pause before Phase 6.

---

## Phase 6: Scenario corpus rework (parallel after Phase 2)

### Overview
Each of the 12 presets emits a configure step then a plug step at t=0.

### Changes Required

**File**: `modules/EV/EvSimulator/main/ScenarioDispatcher.cpp:19-236`
- Replace `make_start_session_event(params)` with `make_configure_session_event(SessionConfigParams)` returning `Event{params}` (now a `ConfigureSession`).
- Every preset: push the configure step (t=0) BEFORE the plug step (t=0); the dispatcher enqueues in vector order so configure is intercepted (Phase 3) before `plug` (Phase 4) consumes it. AcIec presets may keep an explicit `AcIecSessionParams` configure for table clarity (per decision: not required, but explicit). stop/pause/resume/unplug steps unchanged.

### Success Criteria

#### Automated Verification
- [x] `ScenarioDispatcherTest.cpp`: each preset's first two enqueued events are `ConfigureSession` then `Plug`.
- [x] `grep -rn "make_start_session_event\|StartSession" modules/EV/EvSimulator/main/ScenarioDispatcher.cpp` returns nothing.
- [x] Full EvSimulator suite green: 42/42 test cases, 3319 assertions.

#### Manual Verification
- [ ] SIL: `run_scenario`(DcIsoBpt) drives a full BPT discharge session.

**Implementation Note**: TDD red→green; depends only on Phases 1–2.

---

## Phase 7: Tests + docs

### Overview
Python integration test + module docs reflect the new model.

### Changes Required

#### 1. Python smoke
**File**: `tests/core_tests/evsimulator_smoke_test.py`
- Replace `start_session` publishes with `configure_session` then `plug`; add a no-config bare-plug AC assertion.

#### 2. Docs
**File**: `modules/EV/EvSimulator/docs/index.rst` + `docs/images/*.mmd` (`event_ingress_command`, `state_machine_*`) and regenerated `*.png`
- Document `configure_session` (anytime, latched, Accepted/Rejected ack, validation), bare-plug AcIec default, removal of `start_session`. Update the command-ingress and state-machine mermaid sources; regenerate PNGs.

### Success Criteria

#### Automated Verification
- [x] `pytest core_tests/evsimulator_smoke_test.py -v` green: `test_basic_plug_unplug` (bare-plug AcIec default) + `test_configure_then_plug` (configure_session Accepted → plug).
- [x] `grep -rn "start_session" modules/EV/EvSimulator/docs/` + EvSimulator test/controller surface returns nothing. (`tests/core_tests/smoke_tests.py` has an unrelated OCPP `start_session()` helper — out of scope, not the EvSimulator verb.)

#### Manual Verification
- [x] `docs/index.rst` updated (command table, scenario note, cookbook order); `.mmd` sources updated to the configure→plug→BeginSession flow. PNG regeneration requires headless Chrome (mmdc/puppeteer) not available locally — regenerated by the docs-build/CI env from the committed `.mmd`.
- [ ] (pre-existing, out of plan scope) `evsim_ac_iec_test.py` fails on a controller MQTT-lifecycle race (`m2e/enable` not delivered; FSM stays `Disabled`). Test + `evsim_test_controller` predate this branch (base commit 737f672ec) and are not referenced by this plan; the configure-before-plug behavior is proven end-to-end by the green smoke test. Controller updated to configure→plug ordering + idempotent `enable` retry; the residual race is unrelated to this feature.

**Implementation Note**: docs (WS-C) may be drafted alongside WS-A and finalized here.

---

## Testing Strategy

### Unit Tests
- ConfigureSession validation: valid latch + Accepted ack; empty/non-monotonic curve reject; ISO-mode-without-peer reject.
- Plugged resolve: latched AcIec→Charging via PWM; latched DcIsoD20→SlacMatching; no-config default AcIec; reconfigure-mid-charge does not affect live session.
- Persistence: round-trip configured_session; legacy `last_mode` blob tolerated.
- Scenario: first two events per preset are ConfigureSession then Plug.

### Integration Tests
- `evsimulator_smoke_test.py`: configure→plug AC and DC ISO; bare-plug AC default.

### Manual Testing Steps
1. SIL `config-sil-evsim.yaml`: `configure_session`(AcIso2) → `plug` → observe SlacMatching→Charging; `stop_session`; `unplug`.
2. Bare `plug` (no configure) → AC IEC session.
3. `configure_session` with non-monotonic curve → Rejected ack, no latch.
4. With `keep_cross_boot_plugin_state: true`: configure, restart, plug → restored config.

## Performance Considerations

None. Same event volume; one extra payloadless internal event per plug.

## Migration Notes

KVS blobs written by the old build contain `last_mode`; new `from_json` ignores unknown keys and defaults `configured_session` to nullopt (first plug → AcIec default). No migration tooling needed. No external API consumers to migrate (pre-release feature branch).

## References

- Research: `thoughts/shared/research/2026-05-18-evsimulator-architecture.md`
- StartSession handler: `modules/EV/EvSimulator/main/states/Plugged.cpp:32-163`
- Pre-FSM intercept seam: `modules/EV/EvSimulator/main/EvSimRuntime.cpp:229-282`
- Synthetic-event idiom: `modules/EV/EvSimulator/main/states/SlacMatching.cpp:24-30`
- API types: `lib/everest/everest_api_types/include/everest_api_types/ev_simulator/API.hpp:183`
