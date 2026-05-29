---
date: 2026-05-18T13:24:49+02:00
researcher: Martin Litre
git_commit: 737f672ec8086d014d1951663b4d3d2d362be9e0
branch: feat/ev-simulator
repository: everest-core
topic: "EvSimulator module — can it be simplified?"
tags: [research, codebase, EvSimulator, fsm, simplification, tests]
status: complete
last_updated: 2026-05-18
last_updated_by: Martin Litre
---

# Research: EvSimulator module — can it be simplified?

**Date**: 2026-05-18T13:24:49+02:00
**Researcher**: Martin Litre
**Git Commit**: 737f672ec8086d014d1951663b4d3d2d362be9e0
**Branch**: feat/ev-simulator
**Repository**: everest-core

## Research Question

Can the EvSimulator module be simplified?

## Summary

Yes — but the gains are concentrated, not uniform. The runtime/FSM **architecture is sound**: the epoll loop, four-fd model, typed event variant, and `fsm::v2::FSM` host each map one purpose to one mechanism and should stay. The simplification payoff is in **repeated boilerplate**, not structural redesign:

- **Highest yield, lowest risk**: per-state rejected-command blocks and the copy-pasted `BspEvent`-Disconnected block (7 states) — together ~40–50 % of every `feed()` body, zero behavioral variation.
- **Dead code**: `FsmContext::peers` (`PeerHandles`) stored, never read in production; `EvSimRuntime::disarm_scenario_timer()` defined, never called.
- **Data-as-code**: 12 `build_*` scenario functions (~170 lines) are pure step tables collapsible to one indexed table.
- **Class-as-namespace**: `RampInterpolator` (and `SocIntegrator`) are single `static` methods with no instance state — could be free functions.
- **Test bloat**: ~900–1100 of `StateTransitionsTest.cpp`'s 2626 lines are structural ceremony recoverable via data-driven Catch2 parameterization; exact-duplicate cases exist between `StateTransitionsTest` / `SilentFailureFixesTest` / `ScenarioDispatcherTest`; two test files (`CommCheckHandlerTeardownTest`, parts of `EventsTest`) test upstream-library / compile-time facts, not module behavior.

Nothing found warrants reverting the typed rewrite. EvManager overlap is behavioral only (no shared code); EvSimulator manifest declares itself EvManager's replacement, so the duplication is transitional, not a defect to fix here.

## Detailed Findings

### Module shape (baseline)

~3,700 lines core (`main/` + `ev_manager/` + root), ~7,400 lines tests, 806-line `doc.rst`. Entry: `EvSimulator.cpp:38` (`ready()`) builds `EvSimRuntime`, registers subs + kvs_load, starts `comm_check`, spawns `loop_thread` running `runtime->run()` (epoll loop). FSM = hand-built states hosted in header-only `fsm::v2::FSM<StateBase>` (`lib/everest/util/include/everest/util/fsm/fsm.hpp:92-134`); only the flat `FSM`, not `NestedFSM`.

### A. FSM states — highest-yield target

- **Rejected-command boilerplate** — 8 states carry near-identical `case EK::X: ctx.publish_e2m_command_ack("x","reason"); return {false,nullptr};` tails: `Unplugged.cpp:51-74`, `Plugged.cpp:189-212`, `SlacMatching.cpp:67-92`, `V2GNegotiating.cpp:64-90`, `Paused.cpp:73-93`, `Stopping.cpp:45-71`, `BcbToggling.cpp:67-93`, `Disabled.cpp:27-56`. Only the reason string + rejected set vary. A `StateBase` helper plus a per-state rejected-set table collapses SlacMatching/V2GNegotiating/BcbToggling/Stopping `feed()` to near-trivial bodies.
- **`BspEvent`-Disconnected block** — exact 5-line block copy-pasted in 7 states: `Plugged.cpp:174-179`, `SlacMatching.cpp:52-57`, `V2GNegotiating.cpp:49-54`, `Charging.cpp:97-102`, `Paused.cpp:58-63`, `BcbToggling.cpp:52-57`, `Stopping.cpp:30-35`. Centralize as a `StateBase` protected helper or free function next to `transition_to_fault` (`FsmContext.cpp:383-415`). Also removes a runtime `event_to_string(...) == "Disconnected"` string compare at 7 sites.
- **Exhaustive silent tail** — every state ends with a copy-pasted comment + unreachable `return {true,nullptr};` after the `-Werror=switch` exhaustive switch (`Unplugged.cpp:89-97`, `SlacMatching.cpp:104-112`, …). Collapsible only by changing the base `feed()` convention; medium risk.
- **Synthetic-fault enqueue in `enter()`** (`SlacMatching.cpp:24-29`, `V2GNegotiating.cpp:26-30`) is a justified workaround: `fsm::v2` calls `enter()` after the state is installed, so a state cannot transition from its own `enter()`. Leave as-is (changing it means modifying the `fsm::v2` library).

### B. Runtime core — dead code + minor merges

- **`FsmContext::peers` (`PeerHandles`) is vestigial** — declared `FsmContext.hpp:242`, set `FsmContext.cpp:52` / `EvSimRuntime.cpp:45-50`, never read in production (`Plugged.cpp:120` reads `peer_actions`, not `peers`). All peer calls go through `PeerActions peer_actions`. Removable; touches `FsmContext` ctor signature + test fixture.
- **`EvSimRuntime::disarm_scenario_timer()` dead** — declared `EvSimRuntime.hpp:53`, defined `EvSimRuntime.cpp:336-338`, zero call sites (only named in a `TimerFdFlushTest.cpp:5` comment). Pure dead code.
- **`apply_passthrough_vars` + `publish_passthrough_external`** are two sequential `kind_of()` visits over the same event (`EvSimRuntime.cpp:267,279`), non-overlapping arms — mergeable into one switch at minor readability cost. Low priority.
- **`IsoAcMaxCurrentEvt` / `IsoAcTargetPowerEvt`** are wake-only: payloads dropped at subscription (`PeerSubscriptions.cpp:52-54`), unhandled in all 12 states. Could collapse to one empty tag; benefit minor, touches the `Events.hpp:129` static_assert + every state switch. Note: EvManager actually drives `call_set_ac_max_current` from these (`car_simulatorImpl.cpp:355-366`) — confirm the drop is intentional before collapsing.
- **Four timerfd + one eventfd is NOT excess** — each maps to exactly one concern (state deadline / tick / scenario / cross-thread wake). Single-timer multiplexing would add bookkeeping, not remove it. Keep.
- **EvManager vs EvSimulator**: same 4 interfaces, both provide `ev_manager`, both mirror BSP/EVInfo + integrate SoC + KVS cross-boot. No shared code. EvSimulator manifest (`manifest.yaml:3`) declares itself EvManager's replacement → overlap is transitional.

### C. Scenario / SoC / Ramp / API

- **`ScenarioDispatcher` 12 `build_*` functions** (`ScenarioDispatcher.cpp:51-249`, ~170 lines) are pure data with identical push-return shape; collapse to one `vector<ScenarioStep>` table indexed by `ScenarioName`, switch (`326-372`) → bounds-checked lookup. Low risk, high line savings.
- **The inline-flush in `arm_next` is load-bearing**, not accidental: `timerfd_settime` treats `it_value==0` as *disarm*, so zero-offset opening steps (`{0ms,Plug}`,`{0ms,StartSession}`) must be drained synchronously (`ScenarioDispatcher.cpp:293-296`); the `max_iterations` guard (`301-309`) and the `try_rewind_loop` in `on_timer_fire` (`407-410`) are real corner-case handling. Do not "simplify" these.
- **`RampInterpolator`** — single `static step()`, no instance state (`RampInterpolator.cpp:13-47`): class-as-namespace; convert to free function `ramp_step(FsmContext&, time_point)`. Trivial, zero logic change.
- **`SocIntegrator`** — also a single `static step()`. Two-clamp structure (capacity clamp `:117-118`, threshold trim `:129-135`) is **not** redundant (different upper bounds, `Clamp` policy distinction). Manual SoC clamp `:140-144` == `std::clamp` spelled out (cosmetic). `static bool warned` (`:87-92`) doesn't reset between unit-test runs — latent test-isolation smell.
- **`ev_managerImpl`** — two empty bodies (`ev_managerImpl.cpp:9-14`); ev-cli mandated shell, already minimal. The real typed API surface is `CommandRouter.cpp:34-181` (17 MQTT subs via `everest_api_types` codec). No boilerplate to remove here.
- No `everest::util` primitive duplicates logic in these subsystems (`std::clamp` covers clamping; `thread_safe_queue` already used for the event queue).

### D. Test suite — largest raw line savings

- **Copy-pasted helpers** across TUs: `contains_substr` (`StateTransitionsTest.cpp:33-37`, `BcbTogglingTest.cpp:22-26`, `SocIntegratorTest.cpp:73-76`, `RampInterpolatorTest.cpp:43-47`), `topic_recorded`/`payload_for`/`index_of_substr` (`StateTransitionsTest.cpp:40-56`). Move to `TestFixture.hpp` — ~40 lines, zero risk.
- **`StateTransitionsTest.cpp` (2626 lines)**: ~900–1100 lines are structural ceremony. Data-table candidates: 14 `iso_start_charging` mode-mapping sections (`:140-340`, ~140→~30), 7 QueryState sections, 7 InjectFault→Faulted sections (~60→~15).
- **Exact / near duplicates** removable: `SilentFailureFixesTest.cpp:43-51` ≡ `StateTransitionsTest.cpp:391-401` (kvs missing key); `SilentFailureFixesTest.cpp:204-253` ≡ `StateTransitionsTest.cpp:1016-1042` (no-slac-peer fault); `SilentFailureFixesTest.cpp:256-280` ≡ `StateTransitionsTest.cpp:1060-1075`; `ScenarioDispatcherTest.cpp:508-528` ≡ `StateTransitionsTest.cpp:2123-2137` (empty-curve). Keep the *novel* SilentFailure cases (from_json throws, parse_on_battery_full, error-sink).
- **Misplaced tests**: `CommCheckHandlerTeardownTest.cpp` tests `everest_api_types` `CommCheckHandlerBase` (upstream lib), not module code. `EventsTest.cpp:70-72` is a runtime `CHECK` duplicating a production `static_assert`. `RuntimeTickTimerTest.cpp:55-138` replays a private `on_tick` body; the 3 composition variants reduce to 1.
- `SocIntegratorTest.cpp:89-350` — ~12 near-identical parametric sections → one `{mode,current,phases,voltage,tick,...}` table (~200→~50).
- Well-factored already (leave): `CommandRouterTest.cpp`, `BcbTogglingTest.cpp`, `RampInterpolatorTest.cpp`.

## Code References

- `modules/EV/EvSimulator/main/states/*.cpp` — rejected-command + BspEvent-Disconnected duplication (see §A for per-file lines)
- `modules/EV/EvSimulator/main/FsmContext.hpp:242` / `.cpp:52` — vestigial `peers`
- `modules/EV/EvSimulator/main/EvSimRuntime.cpp:336-338` — dead `disarm_scenario_timer`
- `modules/EV/EvSimulator/main/EvSimRuntime.cpp:267,279` — two-pass passthrough switch
- `modules/EV/EvSimulator/main/ScenarioDispatcher.cpp:51-249` — 12 build_* → table
- `modules/EV/EvSimulator/main/ScenarioDispatcher.cpp:291-319,401-411` — load-bearing inline-flush (do not touch)
- `modules/EV/EvSimulator/main/RampInterpolator.cpp:13-47` — class-as-namespace
- `modules/EV/EvSimulator/main/SocIntegrator.cpp:87-92,117-135,140-144` — static-warned, two clamps, cosmetic clamp
- `modules/EV/EvSimulator/tests/StateTransitionsTest.cpp:33-56,140-340` — copied helpers, data-table candidates
- `modules/EV/EvSimulator/tests/SilentFailureFixesTest.cpp:43-51,204-280` — duplicates of StateTransitionsTest
- `modules/EV/EvSimulator/tests/ScenarioDispatcherTest.cpp:508-528` — exact duplicate
- `modules/EV/EvSimulator/tests/CommCheckHandlerTeardownTest.cpp` — tests upstream lib

## Architecture Insights

- **Keep the architecture, cut the boilerplate.** The single-thread epoll + typed event variant + `fsm::v2` host is a clean one-mechanism-per-concern design. Every "could this be simpler" probe into the *machinery* (four fds, dual scenario code path, two SoC clamps, synthetic-fault enqueue) resolved to "load-bearing, documented, justified."
- The real complexity is **horizontal repetition** across 11 state classes and ~175 test SECTIONs — the kind that shrinks with a base-class helper + a couple of data tables, not redesign.
- `peers`/`disarm_scenario_timer` dead code and the misplaced library tests suggest a few rewrite-era leftovers worth a cleanup pass.

## Recommended ordering (if acted on)

1. Delete dead code (`peers`, `disarm_scenario_timer`) — isolated, low risk.
2. Extract `StateBase` helpers: `reject(cmd, reason)` + `handle_disconnect()` — collapses §A across 8/7 states.
3. `ScenarioDispatcher` build_* → table.
4. `RampInterpolator` → free function.
5. Test: hoist shared helpers to `TestFixture.hpp`; delete exact-duplicate cases; relocate `CommCheckHandlerTeardownTest`; data-table the StateTransitions/SocIntegrator parametric families.

Each step is independently shippable and TDD-amenable (existing tests pin behavior before refactor).

## Historical Context (from thoughts/)

None — `thoughts/` did not exist before this document; this is the first research entry for the module.

## Related Research

None yet.

## Open Questions

- Is dropping `ac_evse_max_current` / `ac_evse_target_power` payloads intentional (EvManager drives `call_set_ac_max_current` from them)? Confirms whether `IsoAcMaxCurrentEvt`/`IsoAcTargetPowerEvt` can collapse.
- Is `SilentFailureFixesTest` meant as a permanent regression-doc file? If yes, keep duplicates as intentional anchors; if no, dedupe.
- EvManager retirement timeline — when EvManager is removed the behavioral overlap disappears on its own.
