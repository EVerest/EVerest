# EvSimulator Simplification & Dead-Code Removal — Implementation Plan

## Overview

Remove dead code and over-engineered shells from the EvSimulator module and dedupe its test suite, with **zero behavior change**. The existing ~7,400-line test suite is the characterization net: every workstream is green→green (pure dead-code removal has no red phase; behavior-preserving refactors are pinned by the unchanged suite before and after).

Driven by research: `thoughts/shared/research/2026-05-18-evsimulator-simplification.md`.

## Current State Analysis

- `PeerHandles` (struct `FsmContext.hpp:31`, member `:242`, ctor param `:237`, built `EvSimRuntime.cpp:45-50`, passed `:78`) is **never read in production**. `build_peer_actions()` binds straight off `mod.r_*` (`EvSimRuntime.cpp:103-113`). Test mirror `MockPeerHandles` (`PeerMocks.hpp:105`), fixture field/arg (`TestFixture.hpp:121,155`, `StateTransitionsTest.cpp:2564`).
- `EvSimRuntime::disarm_scenario_timer()` declared `EvSimRuntime.hpp:53`, defined `EvSimRuntime.cpp:336-338`, **zero callers** (only named in a `TimerFdFlushTest.cpp:5` comment).
- Every state's `feed()` ends with a uniform blanket-reject tail: `case EK::X: ctx.publish_e2m_command_ack("verb","<one state reason>"); return {false,nullptr};` × ~9, plus the `-Werror=switch` exhaustive label list. Verbs are byte-stable per command across all states (verified). Semantic/validation acks (`curve has empty points`, `no ev_slac peer`, `count must be > 0`, `set_charging_current not supported in DC/ISO-20 mode`, `InjectFault requires session`, `AC IEC: ISO verbs not applicable`, …) are NOT boilerplate.
- The `BspEvent`→`Disconnected`→`Unplugged` block is copy-pasted verbatim in 7 states (`Plugged.cpp:174`, `SlacMatching.cpp:52`, `V2GNegotiating.cpp:49`, `Charging.cpp:97`, `Paused.cpp:58`, `BcbToggling.cpp:52`, `Stopping.cpp:30`).
- `ScenarioDispatcher` has 12 `build_*` free functions + a second dispatch `switch (name)` (`:326-370`) that is the `-Werror=switch` exhaustiveness guard.
- `RampInterpolator` and `SocIntegrator` are stateless classes exposing a single `static step()` — class-as-namespace.
- Test suite: helper quartet copy-pasted across 4 TUs; exact-duplicate cases across `StateTransitionsTest` / `SilentFailureFixesTest` / `ScenarioDispatcherTest` / `BcbTogglingTest`; `CommCheckHandlerTeardownTest` is the sole repo coverage of upstream `CommCheckHandlerBase` teardown; `EventsTest.cpp:70` runtime-`CHECK`s a compile-time `static_assert`; `RuntimeTickTimerTest` has 3 composition variants of one contract; large parametric families in `StateTransitionsTest` / `SocIntegratorTest`.

## Desired End State

EvSimulator has no vestigial `PeerHandles`/`disarm_scenario_timer`; blanket-reject tails and the Disconnected block are expressed through `StateBase` helpers + a central `command_verb(EventKind)`; scenario building is one exhaustive-switch function; ramp/soc are free functions; the test suite has no duplicated helpers or exact-duplicate cases and the large mechanical families are Catch2-parameterized. **All existing tests still pass with identical assertion counts.** `-Werror=switch` exhaustiveness is preserved everywhere (no `feed()` arm deleted, scenario switch kept, `command_verb` is its own exhaustive switch).

### Key Discoveries
- `build_peer_actions()` reads `mod.r_*` directly, not `peers` — `EvSimRuntime.cpp:103-113`.
- `command_verb` strings are externally observable in `command_ack` payloads and asserted by `contains_substr` — any drift fails CI.
- Scenario dispatch `switch` doubles as the exhaustiveness guard — comment `ScenarioDispatcher.cpp:364`.
- `everest_api_types/tests` is a serialize/hash-only target — no clean relocation home for `CommCheckHandlerTeardownTest`.

## What We're NOT Doing

- NOT collapsing `IsoAcMaxCurrentEvt` / `IsoAcTargetPowerEvt` (open question: EvManager drives `call_set_ac_max_current` from them — flagged as separate follow-up).
- NOT changing the silent-tail / base-`feed()` convention; `-Werror=switch` per-state exhaustive switches stay.
- NOT merging `apply_passthrough_vars` / `publish_passthrough_external`.
- NOT touching `SocIntegrator` logic (`static bool warned`, two-clamp, cosmetic `std::clamp`) — de-class shell only.
- NOT touching scenario inline-flush / `arm_next` / loop-rewind (load-bearing, documented).
- NOT folding semantic/validation acks into the reject helper (blanket-only).
- NOT relocating `CommCheckHandlerTeardownTest` (no clean target; sole coverage).
- NOT addressing EvManager↔EvSimulator overlap (transitional; EvManager is being replaced).
- NO behavior change anywhere.

## Implementation Approach

All work lands directly on `feat/ev-simulator` (current HEAD `737f672ec`) as a sequence of separate commits — no side branches (per user instruction). Workstreams are still independent in content but applied **sequentially** in dependency order: WS-A, WS-C, WS-D (disjoint, any order), then WS-B (after WS-A), then WS-E (after A–D, since the suite is their safety net).

Per workstream, the loop is: build + full `EvSimulator` ctest green at current HEAD → make the change → `./format-changed.sh` the touched files → build + ctest green with **identical pass count** → one commit (Signed-off-by, no co-author, terse subject).

Commit sequence on `feat/ev-simulator`: WS-A → WS-C → WS-D → WS-B → WS-E (one commit each; WS-E's E4 may be several commits, one per parametrized family, each behind its parity gate).

---

## WS-A — Dead code removal (commit 1 on `feat/ev-simulator`)

### Overview
Delete `PeerHandles` entirely and the unused `disarm_scenario_timer`.

### Changes Required

#### FsmContext
**Files**: `main/FsmContext.hpp`, `main/FsmContext.cpp`
- Delete `struct PeerHandles` (`FsmContext.hpp:31`).
- Delete member `PeerHandles peers;` (`:242`).
- Remove the `PeerHandles` ctor parameter (`:237`) and its initializer (`FsmContext.cpp:48,52`).
- Fix the two comments at `FsmContext.hpp:261,268` that say "guard peers.* nullptr" → reference `peer_actions.*.present` (the actual guard).

#### EvSimRuntime
**Files**: `main/EvSimRuntime.cpp`, `main/EvSimRuntime.hpp`
- Delete the `PeerHandles peers{...}` block (`EvSimRuntime.cpp:45-50`) and the `peers` argument in the `make_unique<FsmContext>` call (`:78`).
- Delete `disarm_scenario_timer()` declaration (`EvSimRuntime.hpp:53`) and definition (`EvSimRuntime.cpp:336-338`).

#### Tests
**Files**: `tests/TestFixture.hpp`, `tests/PeerMocks.hpp`, `tests/StateTransitionsTest.cpp`, `tests/TimerFdFlushTest.cpp`
- Remove `PeerHandles peers{};` (`TestFixture.hpp:121`) and the `peers,` ctor arg (`:155`).
- Delete `struct MockPeerHandles` (`PeerMocks.hpp:105`) if unreferenced after the above.
- Remove `fx.peers,` arg at `StateTransitionsTest.cpp:2564`.
- Drop `disarm_scenario_timer` from the `TimerFdFlushTest.cpp:5` comment.

### Success Criteria

#### Automated Verification
- [ ] Builds: `ninja -C build EvSimulator -j$(nproc)`
- [ ] Tests pass, count unchanged vs base: `cd build && ctest -R EvSimulator --output-on-failure`
- [ ] Format clean: `./format-changed.sh <touched .cpp/.hpp>`
- [ ] `grep -rn "PeerHandles\|disarm_scenario_timer" modules/EV/EvSimulator` returns nothing

#### Manual Verification
- [ ] Diff review confirms only deletions + comment fixes; no logic moved.

**Implementation Note**: pause for human confirmation after automated criteria pass before WS-B consumes this branch's pattern.

---

## WS-C — Scenario builder fold (commit 2 on `feat/ev-simulator`)

### Overview
Replace the 12 `build_*` free functions + the `start()` dispatch switch with one `build_scenario_steps(api::ScenarioName)` whose body is the single exhaustive switch.

### Changes Required

**File**: `main/ScenarioDispatcher.cpp`, `main/ScenarioDispatcher.hpp`
- Add free function `std::vector<ScenarioStep> build_scenario_steps(api::ScenarioName)` (anonymous namespace in the `.cpp`). Body = one `switch (name)` with each `case` containing the current `build_*` body inline, `return`ing the vector. No `default` that hides missing cases — keep it exhaustive so `-Werror=switch` forces a decision per new `ScenarioName`; the out-of-range wire-cast guard becomes an explicit `default:` returning `{}` (empty) preserved only for the cast-from-wire case.
- `ScenarioDispatcher::start()` (`:322-373`): replace the 12-case switch with `steps_ = build_scenario_steps(name); if (steps_.empty()) { ctx.publish_e2m_command_ack("run_scenario", "unknown scenario name"); return; } arm_next(ctx);`. (Same ack string, same behavior for unknown.)
- Delete the 12 `build_*` definitions.

### Success Criteria

#### Automated Verification
- [ ] Builds: `ninja -C build EvSimulator -j$(nproc)`
- [ ] `ctest -R EvSimulator` pass count unchanged (esp. `ScenarioDispatcherTest`)
- [ ] Format clean: `./format-changed.sh main/ScenarioDispatcher.cpp main/ScenarioDispatcher.hpp`
- [ ] `grep -c "build_ac_iec_basic\|build_dc_iso" main/ScenarioDispatcher.cpp` == 0

#### Manual Verification
- [ ] Diff review: every old `build_*` body landed verbatim in its `case`; timings/params unchanged.

**Implementation Note**: pause for human confirmation after automated criteria pass.

---

## WS-D — De-class ramp & soc (commit 3 on `feat/ev-simulator`)

### Overview
Convert `RampInterpolator` and `SocIntegrator` from stateless single-`static`-method classes to free functions. Filenames unchanged. **No logic touched.**

### Changes Required

**Files**: `main/RampInterpolator.hpp/.cpp`, `main/SocIntegrator.hpp/.cpp`, `main/EvSimRuntime.cpp`, `tests/RampInterpolatorTest.cpp`, `tests/SocIntegratorTest.cpp`, `tests/RuntimeTickTimerTest.cpp`
- `RampInterpolator::step` → free `void ramp_step(FsmContext&, std::chrono::steady_clock::time_point)` in `namespace module`; delete the class wrapper, keep the doc comment on the free function.
- `SocIntegrator::step` → free `void soc_step(FsmContext&)` in `namespace module`; delete the class wrapper. Body byte-identical (including `static bool warned`, both clamps).
- Update call sites: `EvSimRuntime.cpp:302` (`ramp_step(...)`), the `on_tick` SoC call (`soc_step(*ctx)`), `RampInterpolatorTest.cpp` (~10), `SocIntegratorTest.cpp`, `RuntimeTickTimerTest.cpp:58` and its comment block.

### Success Criteria

#### Automated Verification
- [ ] Builds: `ninja -C build EvSimulator -j$(nproc)`
- [ ] `ctest -R EvSimulator` pass count unchanged
- [ ] Format clean: `./format-changed.sh` on the 7 touched files
- [ ] `grep -rn "RampInterpolator::\|SocIntegrator::" modules/EV/EvSimulator` == 0

#### Manual Verification
- [ ] Diff review: only class-shell → free-function; arithmetic/clamp/warn lines unchanged.

**Implementation Note**: pause for human confirmation after automated criteria pass.

---

## WS-B — StateBase helpers (commit 4 on `feat/ev-simulator`)

### Overview
Centralize the blanket-reject tail and the Disconnected block. Runs after WS-A merges (consumes the cleaned `FsmContext`/test fixture). Behavior-preserving: identical ack strings and transitions; `-Werror=switch` preserved (rejected labels stay as fall-through cases).

### Changes Required

#### Central verb map
**Files**: `main/Events.hpp`, `main/Events.cpp`
- Add free `std::string_view command_verb(EventKind)` implemented as its own exhaustive `switch` (so a new `EventKind` forces a verb decision at compile time). Returns the exact existing literals (`"start_session"`, `"stop_session"`, `"pause_session"`, `"resume_session"`, `"set_charging_current"`, `"set_soc"`, `"bcb_toggle"`, `"clear_fault"`, `"run_scenario"`, `"plug"`, `"unplug"`, `"inject_fault"`, …). Non-command kinds: return `""` (never reached by `reject`).

#### StateBase helpers
**Files**: `main/StateBase.hpp`, `main/StateBase.cpp`
- `protected Result reject(const Event& ev, std::string_view reason)`: `ctx.publish_e2m_command_ack(std::string{command_verb(kind_of(ev))}, std::string{reason}); return {false, nullptr};`
- `protected Result handle_disconnect(const Event& ev)`: if `BspEventPayload` and `event_to_string(...) == "Disconnected"` → `{false, std::make_unique<Unplugged>(ctx)}` else `{true, nullptr}`. (Needs `Unplugged` include in `StateBase.cpp`; verify no cyclic-include — if so, keep helper in `FsmContext.cpp` next to `transition_to_fault` and call from states.)

#### State refactor (11 states under `main/states/`)
- Replace each state's blanket-reject arms with fall-through-grouped labels → single `return reject(ev, "<state reason>");`. Keep all labels (exhaustiveness). Leave semantic/validation acks as individual arms untouched.
- Replace the 7 copied `BspEvent` blocks with `case EK::BspEvent: return handle_disconnect(ev);`.

### Success Criteria

#### Automated Verification
- [ ] Builds: `ninja -C build EvSimulator -j$(nproc)`
- [ ] `ctest -R EvSimulator` pass count unchanged (ack-string assertions in `StateTransitionsTest` are the guard)
- [ ] Format clean: `./format-changed.sh` on touched files
- [ ] No `-Werror=switch` warning (build is `-Werror=switch`); `grep -rn "publish_e2m_command_ack" main/states | wc -l` drops only on blanket arms, semantic acks count unchanged

#### Manual Verification
- [ ] Spot-check 3 states (one in-progress, one Disabled, one Paused): grouped reject emits identical verb+reason; semantic acks intact.
- [ ] Confirm `command_verb` literals byte-match the pre-change grep output.

**Implementation Note**: pause for human confirmation after automated criteria pass before WS-E parametrizes these tests.

---

## WS-E — Test suite cleanup (commit 5+ on `feat/ev-simulator`)

### Overview
Dedupe and parameterize. Runs last; the parametrization sub-step requires WS-A/B/C/D green (the suite is their net).

### Changes Required

#### E2 — Hoist shared helpers
**File**: `tests/TestFixture.hpp` (+ 4 TUs)
- Move `contains_substr`, `topic_recorded`, `payload_for`, `index_of_substr` into `TestFixture.hpp` under `namespace module::test`. Delete the copies in `StateTransitionsTest.cpp:33-56`, `BcbTogglingTest.cpp:22-26`, `SocIntegratorTest.cpp:73-76`, `RampInterpolatorTest.cpp:43-47`.

#### E1 — CommCheck note (no relocation)
**File**: `tests/CommCheckHandlerTeardownTest.cpp`
- Add a one-line header comment: intentionally pins upstream `everest_api_types::CommCheckHandlerBase` teardown (sole repo coverage; do not relocate).

#### E3 — Dedupe exact duplicates (StateTransitionsTest = canonical keeper)
- Delete `SilentFailureFixesTest.cpp:43-51` (≡ `StateTransitionsTest.cpp:391-401`), `:204-253` (≡ `:1016-1042`), `:256-280` (≡ `:1060-1075`), the `transition_to_fault` same-type dup (`:301-316` ≡ `:63-76`), the bcb-reseed dup (`:353-360` ≡ `BcbTogglingTest.cpp:47-52`), and `ScenarioDispatcherTest.cpp:508-528` (≡ `StateTransitionsTest.cpp:2123-2137`). Keep novel SilentFailure cases (from_json throws, parse_on_battery_full, PeerActions-presence, error-sink).

#### E5 — Low-risk collapses
- `RuntimeTickTimerTest.cpp:67-138`: collapse 3 composition variants to 1 ("ramp steps AND SoC advances in one fire").
- `EventsTest.cpp:70-72`: delete the runtime `CHECK(variant_size==30)` (compile-time `static_assert` in `Events.hpp` already enforces it); keep the `kind_of` mapping test.

#### E4 — Gated parametrization (one family at a time)
For each family in order — `StateTransitionsTest` iso-mode (`:140-340`, 14), QueryState (7), InjectFault→Faulted (7), then `SocIntegratorTest` parametric (`:89-350`, ~12):
1. Record baseline: `ctest -R EvSimulator` assertion+test count.
2. Convert scaffold to a Catch2 `GENERATE`/table loop. **`CHECK`/`CHECK_THAT` expressions byte-identical** — only setup/`SECTION` boilerplate becomes the table.
3. Re-run; assertion+test count and pass set MUST be identical. If not, revert that family, leave it un-parametrized, note it.

### Success Criteria

#### Automated Verification
- [ ] Builds: `ninja -C build EvSimulator -j$(nproc)`
- [ ] `ctest -R EvSimulator --output-on-failure` all pass
- [ ] Per E4 family: `ctest -R EvSimulator | tail` test/assertion totals equal the recorded baseline (parity gate)
- [ ] Format clean: `./format-changed.sh` on touched test files
- [ ] Helper quartet defined once: `grep -rn "bool contains_substr" tests | wc -l` == 1

#### Manual Verification
- [ ] Diff review: deleted cases are exact dups (side-by-side with canonical); no unique assertion lost.
- [ ] E4: spot-check one converted family — table rows reproduce the original per-section inputs exactly.

**Implementation Note**: pause for human confirmation after automated criteria pass.

---

## Testing Strategy

- **Characterization-net invariant**: WS-A/B/C/D change no behavior; the *unchanged* test suite passing with identical counts before and after each is the proof. No new production tests are written (nothing new to specify); pure dead-code deletion (WS-A) needs no red phase.
- **WS-B guard**: `StateTransitionsTest` already asserts `command_ack` verb+reason via `contains_substr` — these are the regression detector for the central `command_verb` map.
- **WS-E parity gate**: assertion/test count equality before↔after each parametrized family; revert-on-mismatch.

### Manual Testing Steps
1. After all branches: run full `ctest -R EvSimulator` — all green.
2. Run `tests/core_tests/evsimulator_smoke_test.py` (SIL end-to-end) once to confirm the module still drives a session — `cd tests && pytest --everest-prefix ../build/dist core_tests/evsimulator_smoke_test.py`.
3. Diff each branch vs `feat/ev-simulator`; confirm scope matches "What We're NOT Doing".

## Performance Considerations

None. All changes are structural; the only runtime delta is one fewer copied `PeerHandles` and centralized helper calls (negligible, single-thread loop).

## Migration Notes

None — internal module refactor, no persisted format / interface / MQTT-API change. `command_ack` payload strings are explicitly preserved byte-for-byte.

## Integration Order

Sequential commits on `feat/ev-simulator` (no branches): WS-A → WS-C → WS-D → WS-B → WS-E. Each commit is independently green (build + `ctest -R EvSimulator`, identical pass count) before the next starts. WS-B starts after WS-A is committed; WS-E starts after A–D are committed. No merge step, no cross-branch conflicts.

## References

- Research: `thoughts/shared/research/2026-05-18-evsimulator-simplification.md`
- Progress log: `~/.claude/plans/2026-05-18-evsimulator-simplification-progress.md`
- Key anchors: `main/EvSimRuntime.cpp:45-50,103-113,302,336-338`; `main/FsmContext.hpp:31,237,242`; `main/ScenarioDispatcher.cpp:51-373`; `main/states/SlacMatching.cpp:36-113`; `tests/StateTransitionsTest.cpp:33-56,140-340`

## Open Questions

None. (`IsoAcMaxCurrentEvt`/`IsoAcTargetPowerEvt` intentionality is deferred as out-of-scope, not an open question for this plan.)
