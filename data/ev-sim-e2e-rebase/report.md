# PR #2256 `feat/ev-simulator-e2e-swap` — rebase + status assessment

Every finding below is marked **VERIFIED** (I ran it or read the code) or
**INFERRED** (a judgment call from evidence).

---

## Headline

**The new module is close to done. The swap around it is not.**

Those are two different things, and the PR conflates them. `EvSimulator`
itself — 5,587 lines of module code, 7,668 lines of unit tests, 1,387 lines
of docs — is finished, coherent work. All 22 config options reach real
behavior, all 4 `on_battery_full` policies are implemented, invalid config
throws instead of silently defaulting, and the docs are a genuine 1,191-line
reference. I went looking for stubs and placeholder returns in it and did not
find any. If you merged only the module, you would be merging something good.

What is not done is the *swap*: the 45-config, 107-file migration that
retires `EvManager` as the driver of EVerest's e2e suites. That layer has
**two real regressions** — the Node-RED SIL dashboard no longer drives any
swapped config, and the `CB-EVAL-EV` hardware config lost its auto-run loop —
plus an interface added with no producer, 26 files of dead data, and a SIL
suite stabilized by serializing tests rather than by fixing the timing races
underneath. None of it is fatal, and none of it is deep. All of it is the
kind of thing that stays invisible until someone runs the thing by hand,
which is exactly what has not happened since 13 August.

Neither regression is catchable by the test suite: nothing tests the
Node-RED UI, and no CI job uses the eval-board config. That is the gap
between how this looks and what works.

To be fair to the branch, the parts that *are* testable came back green. The
build is clean on current main, all four required unit suites pass (3,582
assertions in `EvSimulator` alone), and — the result I care most about —
**pre-existing** EVerest smoke tests pass on the swapped config through the
rewritten shared controller. I also caught myself nearly reporting six
smoke-test failures as the swap's fault; they fail identically on
`origin/main`. Details in §3.

The single most useful number: ignoring whitespace, the branch is
**+24,057 / −364**. It is 98.5% additive. It barely touches existing code,
which is why 52 commits over 55 commits of drift produced exactly **one**
conflict. That also means the risky part is a small, identifiable slice, and
it can be separated from the safe part. See [What is left to do](#4-what-is-left-to-do).

---

## 1. The rebase

| | |
|---|---|
| Old tip (pre-rebase) | `a2c9ccd0246e5f8b5cd86e6aee0f9bde4122832b` |
| New tip (post-rebase) | `01ce6f52dce9983b0797303aafa5c8570d6c1193` |
| Rebased onto | `origin/main` = `6f26fb54040e835cc8163c25e9c4c4f3a09c1a95` |
| Backup ref | `refs/backup/ev-simulator-e2e-swap-pre-rebase` |
| Result | 52/52 commits replayed, 0 dropped, 0 behind `origin/main` |

**VERIFIED.** Linear rebase, not a merge. The branch is a clean
fast-forward onto current `origin/main`.

### The conflict story: one conflict, and it was genuinely mechanical

Exactly one file conflicted: `lib/everest/everest_api_types/BUILD.bazel`.
It was the purely-additive case the brief authorized me to resolve:

- `origin/main` had added `telemetry/*` to the `srcs`/`hdrs` globs.
- The branch had added `yeti_simulator/*` to the same globs.

I kept both. **VERIFIED** by checking that `telemetry` exists on
`origin/main` but *not* on the pre-rebase tip, so the two additions are
provably from different sides and neither was invented or dropped.

### Rebase integrity proof

I did not just trust "no conflicts" as "nothing lost":

- The branch touched **243** files; `origin/main` touched **540** over the
  same span; the overlap is **27** files. The other **216** files are
  branch-only.
- **All 216 branch-only files are byte-identical between the pre-rebase tip
  and the post-rebase tip** (compared by blob SHA). Zero drift. **VERIFIED.**
- On the 27 reconciled files, real (whitespace-insensitive) deletions total
  **140 lines**, and every one matches the expected pattern: removing the
  4 `auto_enable` / `auto_exec` / `auto_exec_commands` / `auto_exec_infinite`
  keys and flipping `module: EvManager` → `EvSimulator`. **VERIFIED.**
- `origin/main`'s only new test in an overlapping file,
  `test_iso15118_dc_stop_transaction_during_cable_check` in
  `smoke_tests.py`, survived the rebase. **VERIFIED.**

I did not need to propose a merge instead. The conflict load was trivial
because the branch is almost purely additive.

### Context worth having: this is the second rebase

The tip commit `01ce6f52d fix(api-types): use string_view codec contract` is
dated **13 Aug 2026** — your last day on this branch — and its own message
says the build "broke after rebasing onto main" because main had moved the
codec deserialize surface to `std::string_view`. So you were already
mid-rebase when you stopped, and the last thing you did was repair
rebase-induced API drift.

That is the top risk class here, and it is why the build matters more than
the conflict count: this kind of break does not conflict, it just stops
compiling. See [What is broken](#3-what-is-broken).

### The two suspicious files

Both were flagged in my brief. Both were committed in the same commit,
`7760b3ecf refactor(EvSimulator): typed rewrite + timing`, alongside 87
legitimate files — the signature of a `git add -A`.

**`everest_persistent_store.db` — accidental. VERIFIED.**
It is a 12 KB SQLite file with one table, `KVS(KEY, VALUE, TYPE)`, containing
**zero rows**. It is the `PersistentStore` module's runtime output: ~40 configs
(including ones on `main`) set `sqlite_db_file_path: everest_persistent_store.db`
as a *relative* path, so running any SIL config drops this file in the
working directory. It is not in `origin/main` and it is not in `.gitignore`.
It is a build artifact that got swept into a commit. Nothing reads the
committed copy.
*I have not deleted it.* Recommend: `git rm --cached` it and add it to
`.gitignore` so it stops recurring.

**`thoughts/shared/` — deliberately written, but stale. INFERRED.**
Four files, 860 lines: two research documents and two implementation plans,
dated 2026-05-18, with proper YAML frontmatter (`researcher:`, `git_commit:`,
`branch: feat/ev-simulator`). These were written on purpose — they are not
junk — but they are *your working notes*, they reference a predecessor branch
and commit SHAs that are no longer reachable, and the plan they describe
(`configure-before-plug`) has since been implemented. They now document the
past, in-tree, in a directory no other part of EVerest uses.
*I have not deleted them.* Recommend dropping them from the PR and keeping
them wherever your notes live. If you want them in-tree, they need a home
that is obviously notes, not documentation.

---

## 2. What is missing

### 2a. Code

I swept every line the branch adds for `TODO`/`FIXME`/`XXX`/`HACK`/
"not implemented"/"placeholder"/"for now". The sweep came back **remarkably
clean** — this is not a branch littered with unfinished markers. Five things
are genuinely unfinished, and I found all five by tracing behavior, not by
grepping for markers.

#### (1) An interface was added that nothing publishes — the DC closed-loop SoC path is unreachable. VERIFIED.

This is the clearest half-built item in the module itself.

`interfaces/ISO15118_ev.yaml` gains two vars:

```yaml
  dc_evse_present_current:
    description: EVSE-delivered present current during DC charging (A)
  dc_evse_present_voltage:
    description: EVSE-delivered present voltage during DC charging (V)
```

The consumer side is fully wired: `PeerSubscriptions.cpp:63` subscribes to
both and routes them into `vars.evse_dc_present_current_a`, which flips
`SocIntegrator` from open-loop (integrate the *configured* target current) to
closed-loop (integrate the *actually delivered* current).

**No module anywhere publishes either var.** I grepped the whole tree,
including generated headers: `publish_dc_evse_present_current` has **zero
call sites**. The generated API exists
(`build/generated/.../ISO15118_ev/Implementation.hpp:71`), so the interface
change did produce a publisher method — nobody calls it. The only in-tree
provider of `ISO15118_ev` is `PyEvJosev`, which delegates to the external
Josev package.

Consequence: in every shipped config, `evse_dc_present_current_a` stays
`nullopt` forever and DC SoC is always open-loop. The feature commit
`56254f3ec feat(evsim): wire DC present current passthrough` delivers half a
feature. Your own code comment says so, in the subjunctive —
`Events.hpp:116`: "**A** producer publishing `dc_evse_present_current` ...
routes these into vars."

The trap: `SocIntegratorTest.cpp` covers the closed-loop path in four
sections, but it sets `ctx->vars.evse_dc_present_current_a = 125.0f`
**directly**, bypassing the MQTT plumbing entirely. So the path is green in
unit tests and dead in production. That is the specific gap between what
this looks like and what works.

Mitigating: the open-loop fallback is deliberate and documented
(`80e370fa6 fix(evsim): open-loop DC SoC fallback`), so this degrades rather
than breaks. Finishing it means implementing the producer in the EV-side ISO
stack, which is likely **cross-repo** work in Josev or the C++ EVCC.

#### (2) The Node-RED SIL dashboard is dead on every swapped config. VERIFIED — the most user-visible regression on the branch.

This is the one I would expect someone to hit within a minute of demoing
the branch, and no test on earth would have caught it, because nothing tests
the Node-RED UI.

Node-RED is the standard interactive way to drive EVerest SIL. Its flows
publish to the legacy `car_simulator` topics:

```
everest_external/nodered/#/carsim/cmd/enable
everest_external/nodered/#/carsim/cmd/execute_charging_session
everest_external/nodered/#/carsim/cmd/modify_charging_session
```

`EvSimulator` listens on a completely different namespace:

```
<prefix>everest_api/1/ev_simulator/<module_id>/m2e/<suffix>
```

Not the same topic, not the same root, no overlap. And **there is no bridge**:
I grepped `EvSimulator` for `nodered` and `everest_external` — zero hits — and
`execute_charging_session` is implemented *only* by
`modules/EV/EvManager/main/car_simulatorImpl.cpp`. VERIFIED both ways.

**The branch does not touch a single Node-RED flow file.** VERIFIED —
`git diff --name-only origin/main..HEAD | grep -iE "nodered|flows.json"` is
empty. Meanwhile four flows in `config/nodered/` map 1:1 onto configs the
branch swapped to `EvSimulator`:

| Node-RED flow | its config | now driven by |
|---|---|---|
| `config-sil-flow.json` | `config-sil.yaml` | `EvSimulator` |
| `config-sil-dc-flow.json` | `config-sil-dc.yaml` | `EvSimulator` |
| `config-sil-energy-management-flow.json` | `config-sil-energy-management.yaml` | `EvSimulator` |
| `config-sil-two-evse-flow.json` | `config-sil-two-evse.yaml` | `EvSimulator` |

Symptom: boot `config-sil.yaml`, open the Node-RED dashboard, press the
plug-in / charging-session buttons — nothing happens. The EV does come up
(`enabled_at_startup: true` defaults on), so the stack *looks* alive, which
makes it worse: it reads as a broken simulator rather than a wiring mismatch.

Also stale and shipping in images, none updated:

- `applications/utils/docker/everest-docker-image/nodered/flows/config-sil-two-evse-flow.json` (8 `carsim` refs)
- `yocto/kirkstone/.../everest-node-red-flows/flows.json` (3 refs)
- `yocto/scarthgap/.../everest-node-red-flows/flows.json` (3 refs)
- `modules/EV/EvManager/docs/index.rst` still documents the carsim topics as *the* Node-RED interface

Related, same root cause: the shared controller dropped its legacy enable
publishes (`-everest_external/nodered/1/carsim/cmd/enable`) in favor of
`self._registry.autostart()`. That is correct for the swapped configs, and
harmless for the 5 that stayed on `EvManager` because all 5 keep
`auto_enable: true` (VERIFIED, checked each). But it removes the last
in-tree writer of those topics, confirming nothing drives them anymore.

Finishing this means porting 4+ Node-RED flows (plus Docker and two Yocto
copies) to the typed `m2e` API, or shipping a small carsim→`m2e` bridge.
Either is real work and it is **not started**.

#### (3) `config-CB-EVAL-EV.yaml` silently lost its auto-run behavior. VERIFIED — this is a real regression.

This is the one I would fix before anything else, because it is a
behavioral regression in a **hardware** config and no CI test covers it.

On `origin/main`:

```yaml
  ev_manager:
    module: EvManager
    config_module:
      auto_enable: true
      auto_exec: true
      auto_exec_infinite: true
      auto_exec_commands: wait_for_real_plugin;iso_wait_pwm_is_running;iso_wait_slac_matched;iso_start_v2g_session DC;iso_wait_pwr_ready;iso_dc_power_on;iso_wait_for_stop 60;iso_wait_v2g_session_stopped;unplug;sleep 5;
```

On the branch, all four keys are gone and the module is `EvSimulator`. This
is an eval-board config whose entire purpose is to wait for a **real physical
plug-in**, drive a full DC ISO session, unplug, and **repeat forever**
(`auto_exec_infinite`).

This is genuinely hardware, not another simulation config: it wires
`PyEvJosev` (the real ISO 15118 EV stack) to `device: cb_ev_plc` (a real PLC
modem) via `ev_board_support_API`, under its own `everest_ev` MQTT prefix.
VERIFIED. It is one of a family of `config-CB-EVAL-*` / `config-CB-SAT-*`
board configs.

It fails at five independent layers, which is why it is worth spelling out:

1. `EvSimulator`'s manifest has **no** `auto_exec` equivalent. Its
   `enabled_at_startup: true` only leaves the `Disabled` state; it runs no
   scenario.
2. The command string was extracted to
   `config/config-CB-EVAL-EV.evsim-scenarios.yaml`.
3. That sidecar is read **only** by
   `sim_registry.py::_maybe_load_sidecar_from_temp`, which lives in
   `everest-testing` — the **pytest** harness. Nothing in the EVerest runtime
   reads it. Running this config on hardware never touches the file.
4. Even inside pytest, replay is **off**: `autostart(replay_scenarios=False)`
   is the default and the only call site
   (`everest_test_controller.py:148`) uses the default.
5. Even if replay were enabled, the sequence's **first** op,
   `wait_for_real_plugin`, is the one DSL op the interpreter does not
   implement. I diffed all 13 ops used across every sidecar against the
   interpreter's handlers: `wait_for_real_plugin` is the only unhandled one,
   and `CB-EVAL-EV` is the only file that uses it.

Net effect: boot `config-CB-EVAL-EV.yaml` on an eval board and the simulated
EV enables and then sits idle forever. The repeating plug-in loop is gone.

Scope, stated fairly: no CI job and no doc references this config (I grepped
— the only hit is this report), so nothing will flag it. It bites whoever
next picks up that eval board, and it will look like broken hardware rather
than a missing config option.

For contrast, this was handled correctly elsewhere. Only **two** configs on
`main` had `auto_exec: true`, and the other one,
`config-sil-ac-temp-derating.yaml`, was **deliberately left on `EvManager`**
— which is exactly right. `CB-EVAL-EV` looks like the one that was missed.

#### (4) 26 sidecar files are dead data. VERIFIED.

The swap created 26 `.evsim-scenarios.yaml` files holding the legacy
`EvManager` command strings. They are parsed into
`_scenarios_by_module` and then **never replayed**, for the reason your own
docstring gives, and it is a good reason:

> the sidecar DSLs are the legacy EvManager auto_exec sequences ...; they
> never `plug` themselves, so they cannot start a session in the test
> harness — they only wake once the test plugs and then draw power / unplug
> on the same connector, **racing and corrupting the test-driven session**

So the extraction was done, then discovered to be unusable, then disabled
(`4518bbd52 fix(testing): skip sidecar replay in test controller`) — and the
files stayed. Harmless but misleading: 26 files that look like live test
fixtures and are inert. 43 of them held `auto_exec: false` commands, so
nothing was lost by disabling replay; the exception is `CB-EVAL-EV` above.

Either delete them, or keep only `CB-EVAL-EV`'s and give it a real consumer.

#### (5) `battery_charge_wh` is dropped on the wire, with an unenforced contract. VERIFIED.

In the `ev_simulator` API types, `EvInfo` conversion is asymmetric in both
directions, and the comments are candid about it:

- `to_internal_api` drops `battery_charge_wh` entirely — no counterpart
  exists in `types/evse_manager.yaml`.
- `to_external_api` zeroes it, and "**the caller must populate it
  post-conversion**" — a contract enforced by nothing but that comment.
- Downstream "cannot distinguish 'EV reports 0' from 'EV did not report this
  field'."
- The correct fix (`std::optional<float>` on the external side) is named and
  deferred: "breaks wire compatibility; leave as 0.0f for now."

This is a documented, deliberate limitation rather than an oversight, but it
is unfinished, the fix is known, and the "caller must remember" contract is
the kind that rots. Small item; worth an issue so it is not rediscovered.

#### One test does not test what its name says. VERIFIED.

`tests/core_tests/evsim_dc_iso2_test.py:78` —
`test_iso15118_dc_session_stop_by_evse` does not test an EVSE-initiated
stop. `EvSimulator` has no probe module and no EVSE-stop hook, so the test
injects an **EV-side** `DiodeFail` fault instead and asserts the EV leaves
`Charging`. Your docstring says exactly this, and carries the TODO:

> TODO: replace with a true EVSE-initiated stop once EvSimulator
> exposes an externally driven stop hook.

Honest in the source, invisible in a test report — the suite shows a green
`stop_by_evse` test while EVSE-initiated stop is uncovered.

**But the stated reason is a misdiagnosis, and this is cheaper to fix than
the comment implies. VERIFIED.** The probe module was never supposed to come
from the EV side — it is a standalone test module wired declaratively to the
*EVSE*:

```python
@pytest.mark.probe_module(
    connections={"evse_manager": [Requirement("connector_1", "evse")]}
)
```

Nothing about that depends on which module drives the EV. The proof is in
the tree: the reference test `test_iso15118_ac_session_stop_by_evse`
(`smoke_tests.py:513`) uses exactly this marker and calls
`probe_module.call_command("evse_manager", "stop_transaction", ...)` — and it
runs on `config-sil.yaml`, **which this branch already swapped to
`EvSimulator`**. So the pattern is already proven against `EvSimulator` on
this very branch.

So this is not a missing `EvSimulator` capability. It is a ~5-line test fix:
add the marker and call `stop_transaction`, exactly as the AC twin does. The
`TODO` asking for "an externally driven stop hook" can be closed without
writing one.

#### Things I checked that are genuinely complete

Worth stating, since "what's missing" is only meaningful against what isn't:

- **All 22 manifest config options reach real behavior. VERIFIED.** I traced
  each one to a use site. No option is read-but-ignored, and none is
  declared-but-unread.
- **All 4 `on_battery_full` policies are implemented** with rising-edge
  detection, and an unrecognized value **throws** rather than silently
  defaulting. VERIFIED.
- **The new `/errors/generic` reference on `ev_manager.yaml` is real**:
  `CommCheckHandler` raises `generic/CommunicationFault`, and `CommandRouter`
  can raise arbitrary generic errors. VERIFIED.
- **The new `VEHICLE_*` test-cert tree is not a key leak.** It follows the
  existing `everest-aux` convention exactly, including the same
  `..._PASSWORD.txt` = `123456` pattern used by `SECC_LEAF`. VERIFIED.
- **`EvManager` coexisting with `EvSimulator` is intentional**, not an
  abandoned migration — the manifest says "Coexists with EvManager; targeted
  as its replacement." VERIFIED.

### 2b. Docs

The module's own documentation is genuinely good and was kept in sync
(`fc9386184 docs(evsim): sync index.rst + ISO diagram to code`):
`index.rst` is 1,191 lines covering architecture, all 22 config options,
interfaces, the full MQTT topic API, the state machine, and a per-state
reference, plus 9 Mermaid diagrams with rendered PNGs. This is better than
most EVerest modules. The gaps are all *outside* it.

**Gap 1 — the `.evsim-scenarios.yaml` sidecar format is undocumented.
VERIFIED.** The branch invents a new on-disk file format and ships 26 of
them. It is described nowhere. `index.rst` documents the `run_scenario` MQTT
command and the 12 built-in presets, but not the sidecar. The format's only
trace in the tree is a *negative* reference — `tests/conftest.py:31`
excluding it from a config glob. A reader finding one of these files has no
way to learn what reads it (answer: only the pytest harness) or whether it is
live (answer: no). If the files stay, this needs a section; if they go, the
gap goes with them.

**Gap 2 — no migration note for the `EvManager` → `EvSimulator` swap.
VERIFIED absent; severity INFERRED.** 45 configs changed which module drives
the EV side, and the two modules have **incompatible external APIs**:
`EvManager` took `car_simulator` string commands, `EvSimulator` takes a typed
versioned MQTT API under a different topic prefix. Anything outside this
repo that drives EVerest SIL — Node-RED flows, demo scripts, customer
tooling, CI in another repo — breaks silently on these configs, and there is
no note anywhere saying so. This is the doc gap most likely to cost someone
a day.

**Gap 3 — the two new `ISO15118_ev` vars are documented as if they work.**
Their `description:` fields read as normal live vars. Nothing records that
no producer publishes them. Given finding (1), the interface is currently
advertising a capability the tree does not provide.

**Not a gap:** neither `EvSimulator` nor `EvManager` appears in the
top-level `docs/` tree, so the new module's absence there matches existing
practice rather than introducing a gap. VERIFIED — I checked both.

---

## 3. What is broken

Short version: **nothing that the rebase caused, and nothing in the C++.**
The build is clean, all four required unit suites pass, and the swap is
validated end-to-end on the SIL paths this machine can run. Every test failure
I saw reproduces on `origin/main` or is an artifact of my own parallel test
invocation — I A/B tested both rather than assume.

The two things that are actually broken are the two regressions in §2a
(Node-RED, `CB-EVAL-EV`), and **neither is a test failure** — both are
invisible to the entire test suite. That is the important point: the suite is
green where it runs, and the real damage is in places nothing tests.

### The build is clean. VERIFIED.

```
ninja -C build -j3   ->   [2584/2584]   EXIT=0
FAILED targets:              0
error diagnostics:           0
```

Configured with `-DEVEREST_EXCLUDE_MODULES=EEBUS` as required. 422 warnings,
all of the pre-existing unused-parameter / missing-initializer kind in code
this branch does not touch (e.g. `EvseManager`'s `subscribe_all_errors`
lambdas, `EvseV2G/sdp.cpp`).

This is the headline test result, because it is exactly the failure mode that
stopped you on 13 August: main had moved the api-types deserialize surface to
`std::string_view` and the branch's `ev_simulator`/`yeti_simulator` codecs
still declared the `std::string` overload, so `adl_deserialize` stopped
resolving. That class of break does not produce a merge conflict — it just
stops compiling. **55 further commits of main drift did not reintroduce it.**

### All four required unit suites pass, with real coverage. VERIFIED.

| Suite | Result |
|---|---|
| `everest-core_EvSimulator_tests` | **59 test cases, 3,582 assertions — all passed** |
| `everest-core_YetiSimulator_tests` | 12 test cases, 46 assertions — all passed |
| `everest-core_API_serialize_tests` | 499 tests in 29 suites — all passed |
| `everest-core_types_tests` | 3 test cases, 33 assertions — all passed |

I did not take `ctest`'s word for these. It reported all four "Passed" in
0.00–0.07 s, which is the signature of a target that passes because it ran
nothing — the trap my brief flagged for `ctest -R everest_api_types`. So I ran
each binary directly; the counts above are what actually executed. They are
real.

Worth calling out inside `API_serialize_tests`: both parametrizations of
`everest_api/EverestFileHashTest` passed. That is the source-hash guard over
`interfaces/*.yaml`, and "you changed two interface files, did you update the
hash guard?" is the obvious worry. The answer is that it does not need
updating: `create_file_hashes.cmake` builds the "actual" CSV by iterating the
**expected** CSV's filenames, so only the 24 listed interfaces are hashed, and
`ISO15118_ev.yaml` / `ev_manager.yaml` are not among them. Predicted
analytically, then confirmed by the run.

### Full `ctest`: 3 failures, none attributable to this branch. VERIFIED.

The complete suite is 795 tests. Three failed, all in one group:

```
Test #562: ConnectionSSL accepts a TLS 1.3 client chained to the MO root      ***Failed  10.27 sec
Test #564: ConnectionSSL writes an SSLKEYLOGFILE-format keylog when enabled   ***Failed   5.16 sec
Test #566: ConnectionSSL tears down when the peer closes during the handshake ***Failed  11.06 sec
Test #567: ConnectionSSL completes a TLS handshake against a real client      (hung; run stopped)
```

These are **not** this branch's doing, and I want to be precise about why
rather than just asserting it:

1. `ConnectionSSL` lives in `lib/everest/io`. Under `lib/`, this branch
   touches **only** `lib/everest/everest_api_types/` — 19 files, all
   `ev_simulator`/`yeti_simulator`/`CommCheckHandler`. Zero files in
   `lib/everest/io`. VERIFIED by `git diff --name-only`.
2. None of the three is in the 27-file reconciliation set, so the rebase never
   touched them.
3. The durations (5–11 s) are timeouts, not assertion failures, in TLS
   handshake tests that talk to a real client — and this machine was running
   **four concurrent agent workers at load average 11–12** while they ran.

I then settled it empirically instead of leaving it as an inference. Re-running
that whole group standalone, from the working directory it needs
(`build/lib/everest/iso15118/test/iso15118/io`, which holds its `pki/`):

```
$ ./connection_openssl_test
All tests passed (34 assertions in 7 test cases)
EXIT=0
```

Green, at the same load average. So the root cause is not the branch and not
load as such: it is **`ctest -j3` running this TLS group concurrently**. These
tests stand up a real TLS server against a real client and are not
parallel-safe (fixed ports / shared PKI state), so in a parallel run three fail
and a fourth — `ConnectionSSL completes a TLS handshake against a real client`
— hangs indefinitely rather than failing. I stopped the run at 794/795 once
that was clear.

**Conclusion: the 3 failures are an artifact of my own parallel invocation, in
code this branch does not touch. Zero test failures are attributable to the
branch or the rebase.** Worth knowing independently of this PR: `ctest -j`
against the full suite is not reliable on this repo today.

One honest caveat on the earlier numbers: because I stopped the run, 794 of 795
tests reported. The unreported one is that hanging TLS test, which passes in
isolation.

### The SIL / e2e layer: I did run it, and the swap works

I installed to `build/dist` and ran real SIL tests (full EVerest stack per
test, live mosquitto, `everest-testing` editable-installed from **this
branch's** harness source so the rewritten controller is the one under test).
I waited for the machine to quiet down first — these ran at load 1–2, not 11.

**The swap itself is validated. VERIFIED.**

| SIL run | Result |
|---|---|
| `evsimulator_smoke_test.py` (new, drives typed `m2e` API directly) | **2 passed** |
| `evsim_ac_iec_test.py` + `evsim_timing_override_test.py` (new) | **4 passed** |
| `smoke_tests.py -k "pwm_ac_session or energy_node"` (**pre-existing**) | **6 passed** |

That third row is the one that matters. Those six are *pre-existing* EVerest
smoke tests — including `test_pwm_ac_session_paused_by_ev` and
`test_pwm_ac_session_paused_by_evse` — running on the swapped
`config-sil.yaml`, driven through the **rewritten shared
`everest_test_controller.py`**, against `EvSimulator`. They pass. So the
riskiest single file on the branch does correctly drive the new module for
existing tests.

### The HLC/ISO SIL group fails — and it fails identically on `origin/main`

This is the finding I most nearly got wrong, so here it is in full.

Running the pre-existing AC ISO group on the branch:

```
$ pytest tests/core_tests/smoke_tests.py -k "iso15118_ac_session"
6 failed, 1 skipped
```

with the EV side matching SLAC, immediately unmatching, and the SECC then
timing out:

```
SLAC MATCHED
SLAC UNMATCHED
V2G communication setup timeout (18000ms) expired - signaling dlink_error to EvseManager [V2G2-723]
EVSE ISO D-LINK_ERROR.req
```

Six pre-existing smoke tests broken by the swap, in exactly the area the
branch has four "re-match SLAC" fix commits. That is a compelling story, and
it is **wrong**.

I A/B tested it. I temporarily checked out `origin/main`'s versions of the
three relevant files — `config/config-sil.yaml` (so `EvManager` drives),
`everest_test_controller.py` (main's controller), and `smoke_tests.py` — reran
the identical selection against the same binaries, then restored the branch
(tree verified clean, `HEAD` still `01ce6f52d`):

```
$ pytest tests/core_tests/smoke_tests.py -k "iso15118_ac_session"   # origin/main baseline
5 failed, 2 skipped
EVSE ISO D-LINK_ERROR.req      <- same symptom
```

Same failure, same symptom, on `origin/main`, at load 2.10. The sixth test on
the branch is `test_iso15118_ac_session_paused_by_ev`, which `main` still
skips and this branch un-skipped — so the count differs by exactly that one.

**Conclusion: the HLC/ISO SIL group is non-functional in this environment
regardless of branch, and none of those failures is attributable to the branch
or the rebase. VERIFIED by direct A/B.** These tests need network conditions
CI provides (HomePlug/SLAC simulation over a configured interface) that this
machine does not have.

Had I reported the first run without the baseline, I would have told you the
swap breaks six smoke tests. It does not.

### What remains genuinely unverified

Because the HLC stack does not work here, I could **not** exercise the
ISO 15118 paths through the swap — which is where the branch's own
pause/resume fixes and its known teardown-timing race live. Specifically
unproven either way:

- the 13 `config-sil.yaml` + 8 `config-sil-dc.yaml` **ISO** smoke tests under
  `EvSimulator`;
- whether `test_iso15118_ac_session_paused_by_ev` (the one this branch
  un-skipped) actually passes — its five HLC siblings fail here for
  environmental reasons, so this run says nothing about it;
- the new `evsim_dc_iso2_test.py` / `evsim_d20_test.py` / `evsim_bpt_mcs_test.py`
  suites;
- any `ocpp_tests`.

Those need CI, or a machine with the SIL network setup. **That is the one gate
I would insist on before merging seam 5** of the split below.

---

## 4. What is left to do

### Should 243 files be one PR? No. And the seams are unusually clean.

My read: **split it, and the split is easier than the file count suggests.**

The reason is the number from the headline. Ignoring whitespace the branch is
**+24,057 / −364**. Almost everything it does is *add a new module*; almost
nothing it does is *change existing code*. So the diff separates cleanly into
a large risk-free part and a small risky part, and right now they are stapled
together, which is why nobody has reviewed 243 files.

Real (whitespace-insensitive) line counts by area, which is what the seams
follow:

| Area | Lines | Files | Deletions |
|---|---|---|---|
| EvSimulator unit tests | 7,668 | 22 | 0 |
| EvSimulator module code | 5,587 | 48 | 0 |
| api-types (`ev_simulator`, `yeti_simulator`) | 2,247 | 15 | 4 |
| everest-testing harness | 2,243 | 7 | 61 |
| EvSimulator docs | 1,387 | 19 | 0 |
| configs (the swap) | 1,631 | 53 | 137 |
| core_tests SIL e2e (new) | 1,218 | 8 | 4 |
| `thoughts/` working notes | 860 | 4 | 0 |
| YetiSimulator + misc modules | 738 | 13 | 67 |
| ocpp_tests (config swap only) | 160 | 46 | 90 |
| interfaces | 7 | 2 | 0 |

**A five-PR stack along those seams:**

1. **api-types for `ev_simulator` + `yeti_simulator`** — 2,247 lines, 15
   files, effectively zero deletions. Self-contained, includes the
   `string_view` fix. Reviewable in an hour. **Mergeable now.**
2. **`YetiSimulator` m2e error routing** — 738 lines, 13 files, with 253
   lines of new unit tests (`RaiseErrorRouterTest`, `DiodeFaultDetectionTest`).
   Independent of everything else. **Mergeable now.**
3. **The `EvSimulator` module itself** — module + unit tests + docs = 14,642
   lines, 89 files, **zero deletions**. Big, but it is one coherent new
   module with a 1.37:1 test-to-code ratio and its own docs. Nothing uses it
   yet, so it cannot regress anything. Depends on 1. **Mergeable after 1**,
   and this is the PR that deserves the real design review.
4. **The `everest-testing` harness** — 2,243 lines, 7 files. Mostly new
   (`sim_registry.py`, `evsim_test_controller.py`, plus 1,017 lines of tests
   for the harness itself), but it **modifies the shared
   `everest_test_controller.py`** (+225/−61). That file defines
   `EverestTestController`, the class behind the `test_controller` fixture
   used by **43 test files** (7 in `core_tests`, 36 in `ocpp_tests`) —
   VERIFIED. So this is the first PR carrying real blast radius, and its
   286 changed lines deserve more review attention than the other 24,000
   combined. Depends on 1, 3.
5. **The swap** — configs + ocpp_tests + new SIL tests = 3,009 lines but
   **107 files**, and every regression I found lives here. Depends on all of
   the above.

Items 1–3 are **17,634 lines / 119 files with essentially no deletions** —
about 73% of the branch, carrying close to zero regression risk. Getting
those merged shrinks the thing that actually needs careful review to items
4–5: ~5,250 lines, and the genuinely dangerous surface is **one file**
(`everest_test_controller.py`, 286 changed lines) plus the config sweep.

One more argument for splitting at seam 5: the swap re-points **all 29
pre-existing `smoke_tests.py` tests** onto `EvSimulator`
(13 on `config-sil.yaml`, 8 on `config-sil-dc.yaml`, 6 on
`config-sil-dc-isomux.yaml`, plus 2 more) — VERIFIED. That is EVerest's
main SIL gate changing its EV driver wholesale. It should not ride in on the
back of a PR whose diff is dominated by a new module's unit tests.

**Also: strip the config re-indentation before splitting.** The config diff
is 4,401+/2,947− raw but only 1,668+/214− ignoring whitespace — roughly
**5,500 lines of pure YAML re-indentation churn** from a `yaml.dump`
round-trip (list items dedented from `  - x` to `- x` across 45 files, plus
all the `terminals:`/`position:` GUI blocks). It buries a ~4-line semantic
change per file, destroys `git blame` on those files, and is most of why the
PR looks unreviewable. Re-apply the swap as minimal edits.

### Ordered work list

Sizes: **XS** < 1h, **S** ~half day, **M** 1–3 days, **L** > 3 days.

**Do first — real regressions, both invisible to CI:**

1. **Restore `config-CB-EVAL-EV.yaml`'s auto-run loop.** Cheapest correct
   fix: revert that one config to `EvManager` (**XS**), matching what was
   already done deliberately for `config-sil-ac-temp-derating.yaml`. Proper
   fix: add a startup-scenario option to `EvSimulator` plus a
   `wait_for_real_plugin` equivalent (**S–M**). Decide which; do not leave
   it as-is.
2. **Fix the Node-RED flows.** Port the 4 `config/nodered/*.json` flows (plus
   the Docker copy and both Yocto `flows.json`) to the typed `m2e` API
   (**M**), or ship a small carsim→`m2e` bridge module so every existing flow
   and any external tooling keeps working (**S–M**, and it protects
   out-of-tree users too — probably the better buy).

**Do before merge — cheap hygiene:**

3. `git rm --cached everest_persistent_store.db` and add it to `.gitignore`
   (**XS**).
4. Drop `thoughts/shared/` from the PR — 860 lines of stale notes (**XS**).
5. Delete the 26 dead `.evsim-scenarios.yaml` sidecars, or keep only
   `CB-EVAL-EV`'s and give it a real consumer (**XS–S**, follows from item 1).
6. Fix `test_iso15118_dc_session_stop_by_evse` to use the probe module the
   way its AC twin already does, and delete the misleading `TODO` (**XS**,
   ~5 lines — see the correction in §2a).

**Do before merge — docs:**

7. Write the `EvManager` → `EvSimulator` migration note: the topic namespace
   change, the removed `auto_exec*` options, and what external tooling must
   do (**S**). This is the gap most likely to cost an outside user a day.
8. Document the `.evsim-scenarios.yaml` format, or delete it with item 5
   (**XS–S**).
9. Note on the two new `ISO15118_ev` vars that no producer publishes them yet
   (**XS**).

**Decide, then schedule:**

10. **The unpublished `ISO15118_ev` vars.** Either implement the producer in
    the EV-side ISO stack — likely **cross-repo** in Josev, **M–L** and not
    fully in your control — or drop the two vars and the subscriber from this
    PR and keep DC SoC open-loop until the producer exists (**S**). Shipping
    an interface nothing implements is the worst of the three.
11. **Convert or consciously keep the 5 remaining `EvManager` configs**
    (**S–M**). One (`config-sil-ac-temp-derating.yaml`) should stay by
    design. `config-eebus.yaml` is moot here. But
    `everest-config-ocppmulti-sil-dc-d20-eim.yaml` is used by an active
    OCPP 2.1 DER test, so `EvManager` cannot be retired yet — and the
    two-EVSE configs prove multi-connector already works under
    `EvSimulator`, so these look unconverted rather than blocked. Until this
    is done, "replaces EvManager" is not true and both modules must be
    maintained.
12. **Root-cause the SIL pause/resume teardown race** instead of serializing
    around it (**L**, and it may not be an `EvSimulator` bug at all — your
    own commit message points at the SECC's V2G read timeout and the
    over-voltage monitor staying armed into the resume CableCheck). Today the
    entire `EvSimulator` SIL suite is pinned to one xdist worker, and two
    **pre-existing** smoke tests
    (`test_iso15118_ac_session_paused_by_ev`,
    `test_iso15118_dc_session_paused_by_ev`) were moved from the
    parallelizable `ISO15118` group to the new serialized `ISO15118_SERIAL`
    group — so this branch slows CI for everyone, not just for its own tests.
13. Make the test harness fail loudly. `everest_test_controller.py` has **18**
    `if drv is None: return` silent no-ops, and `sim_registry._load` logs and
    returns on a config read failure, so a misconfigured connector produces a
    controller whose every method silently does nothing and the test dies
    later as a confusing timeout. The DSL interpreter also catches a wait
    timeout and **continues to the next step** (`sim_registry.py:241`), and
    treats an unrecognized op as a warning. Raise instead (**S**).
14. Consider re-enabling autogenerated codec tests for the 7 `ev_simulator`
    types opted out in `disable.csv` — `ChargingCurve` and all five
    `*SessionParams` variants, i.e. the most complex payloads on the new API.
    They have hand-written coverage, but the systematic round-trip check is
    off (**S**, likely needs autogenerator support for `std::variant`).

### One gate, not a work item

Before seam 5 (the swap) merges, **run the ISO/HLC SIL suites and the
`ocpp_tests` in CI**. I validated the swap on every SIL path this machine can
run, but the HLC group needs network conditions it does not have, so the
ISO 15118 paths through `EvSimulator` — including the test this branch
un-skipped and the known teardown-timing race — are still unproven either way.
Everything else in this report I could check locally; that one I could not.

Incidental, unrelated to this PR but worth knowing: `ctest -j` is not safe on
this repo today. The `ConnectionSSL` group in `lib/everest/iso15118` binds
fixed ports and shares PKI state, so running it in parallel produces three
failures and one indefinite hang; the same group passes cleanly single-threaded
(§3). Worth a `RESOURCE_LOCK` or `RUN_SERIAL` property on those tests.

### If you only do three things

Fix `CB-EVAL-EV` (XS), fix or bridge Node-RED (S–M), and land items 1–3 of
the split so 73% of this stops rotting in a stale branch. Everything else can
follow at leisure.
