# RsEvseManager architecture

Rust implementation of the EVSE manager. Target scope is behavior parity with the
C++ `EvseManager` for every deployment the shipped configs and test suite exercise:
AC basic, AC with ISO 15118 HLC, DC, and bidirectional power transfer.

This document records decisions and their reasons. It is deliberately short. The
previous Rust attempt specified seven effect execution lanes, a macrostep fact FIFO
with invariant latching, and a runtime metrics subsystem, then grew to 16,400
production lines covering AC basic alone. Everything below that is not required by
evidence is an explicit non-goal.

## Layering

```
boundary/     generated everestrs bindings, conversions, publishers
  loop        the single writer thread
  executor    safety lane + ordered lane + one lane per device and the store
core/         session lifecycle, authorization, reservation, availability, metering
  path/       PowerPath implementations (AcBasic, AcHlc, AcWithSoc, Dc) and the shared Iec reducer
  hlc/        the ISO 15118 port: boot setup and the advertised transfer mode set
```

`core` has no dependency on `everestrs`. It is compiled and tested standalone.

### The five percent fallback expiry is an `AcHlc` decision, not a reducer input

`IecInput` carries no `FivePercentFallbackExpired`, which it briefly did. The behavior is
ported and is not missing: the two `ac_x1_fallback_nominal_timeout` arms of
`Charger::run_state_machine`'s `WaitingForAuthentication` case decide the fallback from
`shared_context.hlc_charging_active` and the target state, neither of which the `Iec`
reducer holds, so a reducer input could only have restated the decision one layer below
where its inputs live. `AcHlc` arms `AcTimer::FivePercentFallback`, marks the window
`Fallback::Elapsed` when it expires and re-runs the authorization loop, which is that
timeout in full. Pinned by `enforced_five_percent_never_falls_back` and the surrounding
`TIMER_FIVE_PERCENT_FALLBACK` tests in `src/core/path/ac.rs`.

A second and separate fallback sits beside it, and this section used to deny that it
existed. It claimed that the `not implemented yet` marker in the external authorization arm
of `Charger::run_state_machine` - "AC mode: fall back to nominal PWM charging etc (not
implemented yet!)" - names a genuine C++ gap that this port does not reproduce either. That
is the inverse of the truth. The marker is stale: `Charger::dlink_error` sets `hlc_failed`,
and `hlc_failed` is the nominal PWM fallback the marker asks for. The next section is what
it does and where the port reads it.

### The high level communication failure latch is per plug in, and AC only

`Charger::dlink_error` sets `hlc_failed`. Two places read it, and the `Idle` entry of
`Charger::run_state_machine` clears it.

- The entry into `WaitingForAuthentication` derives
  `ac_hlc_enabled_current_session = config_context.ac_hlc_enabled and not hlc_failed`, and
  everything the AC high level communication path does is reachable only inside that
  conjunction: `hlc_use_5percent_current_session` is assigned under it, and so is the
  `ac_enforce_hlc` branch of the authorization loop that otherwise holds the five percent
  offer up for the whole session and says so in its own log line, "keeping 5 percent on
  until a dlink error is signalled". Once the latch is set that entry takes the "HLC is
  disabled for this session" branch instead and goes straight to `PrepareCharging` on the
  nominal duty cycle.
- `PrepareCharging` picks `update_pwm_now_if_changed_ampere` when
  `charge_mode == ChargeMode::AC and (not hlc_use_5percent_current_session or hlc_failed)`.
  That second disjunct carries the fallback on the one route which re-derives nothing: a
  `D-LINK_ERROR` arriving with the offer already off the pilot finds `pwm_running` false,
  runs no pilot detour under `[V2G3-M07-04]`, and so never re-enters
  `WaitingForAuthentication` inside that plug in.

So a vehicle that cannot carry ISO 15118 on a given plug in finishes the session as IEC
61851 basic charging, and gets no further attempt until the cable comes out. The clear is
the `Idle` entry and not the end of a session: `EvseState::Finished` moves to `Idle` only
once `flag_ev_plugged_in` has gone.

In the port `AcHlc::hlc_failed` is the latch. `AcHlc::on_data_link` is the only writer of
the failure and `AcHlc::end_session` is the only writer of the clear, the latter being the
port's single `Idle` entry, reached from the unplug and from the port leaving service and
from nowhere else. The two read sites are `AcHlc::derive_offer`, which both entries into
`WaitingForAuthentication` run, and `AcHlc::shape`, the single conversion point every duty
cycle this path offers passes through.

**DC keeps retrying, and that is deliberate.** `hlc_failed` is written on a DC port and
read by nothing there: the `WaitingForAuthentication` entry branches on the charge mode
first and its DC arm sets `hlc_use_5percent_current_session` unconditionally without
deriving `ac_hlc_enabled_current_session` at all, and the `PrepareCharging` read sits
inside `charge_mode == ChargeMode::AC`. `main` therefore has no DC fallback, and neither
does this port. The mode branch in `AcHlc::derive_offer` sits above the latch read for that
reason, and `Dc` holds no latch. Pinned by
`a_port_presenting_dc_keeps_offering_five_percent_after_a_link_error` in
`src/core/path/ac.rs` and by
`a_dc_port_keeps_retrying_high_level_communication_after_a_link_error` in
`src/core/path/dc.rs`; both fail if the latch grows a DC reader.

## The core is a pure function of events and time

```rust
impl Core {
    fn apply(&mut self, event: Event, now: Instant) -> Vec<Effect>;
}
```

`Core::apply` performs no I/O, reads no clock, spawns nothing, and locks nothing.
Time arrives as a parameter. A forty second DC cable check therefore runs
deterministically in microseconds under test.

## Concurrency

One writer thread owns all state. There are no mutexes in `core`.

The C++ module ships a 295 line `scoped_lock_timeout.hpp` that names roughly thirty
individual lock sites and dumps a backtrace on timeout, used at 65 call sites. That
is direct evidence that shared-state locking in this module was a recurring
operational problem. A single writer removes the problem and the tooling.

Everything entering the core is an `Event` on one unbounded queue: framework
callbacks, timer expiries, and effect completions. One queue means one total order.

There are no priority lanes on intake. The previous attempt reserved capacity for
`ErrorRaised` but not `ErrorCleared`, so a full queue could drop a clear and latch
the EVSE inoperative until restart. With one queue and no drop policy that outcome
is not representable.

Events are never coalesced. The tightest deadline in the module is 150 ms
(`TP_EV_VALD_STATE_DURATION_MIN`, ISO 15118-3 table 3), the C++ FSM ticks at 100 ms,
and the aggregate event rate across all subscriptions is order 10 to 100 per second
against a loop doing microseconds of work per event. The framework's own per-topic
queue grows and warns rather than dropping, so this matches its behavior. The loop
records queue depth and processing latency high water marks and warns past a
threshold.

The module is synchronous. The everestrs surface is synchronous: `handle_command`,
`handle_variable`, `handle_on_error` and `on_ready` are all `fn(&self, ..)` invoked
from the framework's scaling handler pool, and `handle_command` returns a value, so
the framework already expects the caller to block. Introducing an async runtime
creates an impedance mismatch with nothing to gain.

## Effects

`Core::apply` returns effects as values. The loop dispatches them; it never performs
them inline, because a single outbound command can block for 300 seconds
(`remote_cmd_res_timeout_seconds`) and everestrs exposes no way to bound it.

Three execution contexts, classified by what the effect is rather than by who emits
it:

- A dedicated **safety** thread executes only safety actuation: allow power on,
  supply off, CP state X1 and F, connector unlock, board support enable. It can
  never queue behind other work.
- A dedicated **ordered** thread executes every effect whose order something else
  observes. That began as the writes on the interfaces this module provides: on
  `evse_manager`, session events, the enable and disable pair, limits, ready,
  waiting for external ready, and error raises and clears; on `energy_grid` the
  flow request; on `random_delay` the countdown. One thread, so dispatch order is
  wire order. The random delay countdown is here for the ordering rather than the
  interface: the C++ publishes it before the enforced limits it explains
  (`energy_grid/energyImpl.cpp:503` and `:509`, ahead of `:525`), so a consumer
  reading both never sees a withheld limit before the countdown that accounts for
  it. It has since taken two sets of outbound calls for a second ordering
  reason - the metering transaction commands and every `ISO15118_charger`
  command - each recorded in its own section below.
- A **per device** serial lane for each of the supply, the board support, the
  isolation monitor, the over voltage monitor and the connector lock, and one for
  the store. One thread each, so commands to a device execute in dispatch order
  and through completion. `SERIAL_CONTEXTS` in `boundary/executor.rs` is the list.
Every lane is serial: one thread each, and there is no shared pool.
`Effect::context` holds the decision per variant and states how to make it for a
new one.

**There used to be a third kind of lane, an ordinary pool of three threads, and
it was retired in this commit.** It carried effects outside the device and store
domains under the rule that nothing observes the order they run in. That rule
held right up to the end, and it is what emptied the pool: the per device lanes
took persistence, setpoints, PWM, the connector lock, the isolation monitor and
the over voltage monitor, and the SLAC relays then left for the ordered lane
because the core emits them in ordered pairs. What remained classified to the
pool was `Effect::StartTimer` and `Effect::CancelTimer`, and the writer
intercepts both in `EventLoop::perform` and arms them itself, so neither ever
reached a worker. Three threads and a queue were serving nothing.

`Effect::context` therefore answers `Option<ExecContext>` rather than
`ExecContext`. `None` is not a lane and is deliberately not spelled as one: it
says the executor never runs this effect at all, which is the honest answer for
the two timer effects and would have been dishonest as a named queue. The
classification stays exhaustive, so a new variant still does not compile until it
says `Some(lane)` or `None`.

Classification is exhaustive over the `Effect` enum, so adding a variant forces the
choice. Timer effects never reach the executor; the writer arms them.

The publish thread exists because publish order is observable. A framework publish
is synchronous on the calling thread
(`MQTTAbstractionImpl::publish`, `lib/everest/framework/lib/mqtt_abstraction_impl.cpp`)
and the framework's own handler pool is serial per topic
(`lib/everest/framework/include/utils/message_handler.hpp`), so the reordering was
ours: three workers sharing one queue ran consecutive session events concurrently,
and five SIL runs produced three different orderings of the same four events. The
core emits `Authorized` and then `TransactionStarted`, and `SessionStarted` then
`AuthRequired`, within one pass, and consumers read the sequence.

An error raise and its clear are on the publish lane rather than the safety lane.
The safety lane's invariant is actuation. An `Inoperative` raise removes the EVSE
from service, but the removal is the `to_safe_state()` actuation emitted from the
same signal, already on that lane; the raise announces a decision already acted on.
It is a write on the module's own interface, and a clear overtaking its raise would
latch the EVSE inoperative, which is the same ordering argument.

A single shared worker would also restore total order, and was rejected: one
outbound command blocking for the full command timeout would then stall every
publish behind it, which is why the lanes are split by domain rather than pooled.
Batching ordered publishes
into one effect was rejected too, because it orders only within a single
`Core::apply` and two publishes from two separate applies would still overlap.

Shutdown closes intake on every lane and waits on one worker count with a
single bound. A publish thread wedged inside a framework call is abandoned at the
deadline rather than joined, exactly as a wedged device worker is, so a stuck
publish delays teardown by at most the bound and never holds the process open.

Ordering between dependent operations is the core's responsibility, not the
executor's. Sequences that contain waits, such as cable check, the supply off then
below 60 V then open contactor shutdown, and the import export switch preceding
setpoints, are state machine stages in the core driven by timers and effect
completions. Individual effects stay atomic and short.

The DC supply has two opposite orderings and the core owns the choice. A direction
switch on a live supply sends the mode before the setpoint; an energize from off
sends the setpoint before the mode, because the supply resets its internal targets
on the off transition. `Dc::apply_target` decides from `supply_mode`, which is the
same predicate the C++ branches on.

There is a third case, and it is a refusal rather than an ordering: a vehicle target
that arrives with the supply off and the relays **not** confirmed closed writes the
target and does not switch the supply on. That is the gate at
`modules/EVSE/EvseManager/EvseManager.cpp:2662`, and only the off case is gated
because `powersupply_DC_on` early returns on a supply already on. The relay
confirmation is `CPEvent::PowerOn`, which the core hands to every path.

Effect completion re-enters the loop as an event. Nothing runs detached. The C++
cable check runs on a detached thread racing the FSM and works around the resulting
unplug race explicitly; that race is not representable here.

### Effect identities are one space, because the core correlates ahead of the paths

`Core::apply`'s `Event::EffectDone` arm offers every completion to
`answer_transaction_start` before `PowerPath::on_effect_done` sees it. That makes the
core a correlator standing ahead of the paths rather than beside them, which is why
`EffectId` uniqueness cannot be a per allocator property.

It was a per allocator property once, and the result was a safety defect. `Core` held
`effect_ids` and `Dc` held its own, both `EffectIds::default()`, so both began at
`EffectId(0)`. The overlap is structural rather than a race: `StartTransaction` is
emitted when authorization is accepted, the DC cable check runs inside
`PrepareCharging`, and any powermeter slower than the ramp to the isolation voltage is
still owed a verdict when `Dc::request_self_test` asks for one. On the first session of
a port's life both awaits then named `EffectId(0)`, and the two completions were
swapped in both directions:

- The isolation self test verdict reached `answer_transaction_start` first, which
  claimed it and persisted the session on it. The cable check did not advance; its
  isolation samples produced nothing and the stage ended on the 30 s
  `SELF_TEST_TIMEOUT` armed on `TIMER_CABLE_CHECK`.

- The meter's own reply, arriving later, no longer matched
  `awaiting_transaction_start`, because the step above had cleared it. It fell through
  to `Dc::on_effect_done`, matched `pending_self_test`, and advanced the isolation
  monitor self test. `ImdStart` was emitted and sampling began on the strength of a
  powermeter reply. An isolation self test is a safety gate, and in that ordering it
  was passed by something that never measured isolation.

The first fix was one id space instead of two. `EffectIds` holds a shared counter, and a
delegate is another handle on that counter rather than a counter of its own;
`Core::new` hands the path one through `PowerPath::adopt_effect_ids` before any session
moves. The default body is a no-op, which is the whole answer for `AcBasic`, `AcHlc` and
the `AcWithSoc` that composes one: none of them awaits a completion of its own, so none
allocates an identity. `Dc` overrides it because it awaits the self test.

That closed the hole and left the invariant resting on everyone remembering to draw from
the shared counter, so ownership was then made structural rather than conventional.
`EffectId`'s field is private and `EffectIds::allocate` is its only producer, so an
identity is allocated and can no longer be written. `EffectIds<O>` and `Issued<O>` carry
an owner tag, `ByCore` or `ByPath`, in a `PhantomData` beside the counter:
`Core::awaiting_transaction_start` is an `Option<Issued<ByCore>>` and
`Dc::pending_self_test` an `Option<Issued<ByPath>>`, so neither await slot has a type
the other's identity fits, and neither takes the erased identity a completion carries
back either.

The tag is deliberately not a partition of the space. There is one `Arc<AtomicU64>` per
port; `EffectIds::<ByCore>::one_space` is its only origin, `delegate` its only other
handle, and `EffectIds` has neither `Clone` nor `Default` to make a third way. Every
`Issued<O>` erases to the same comparable `EffectId` through `effect_id`, and `answers`
compares on exactly that, so a tag can refuse an await and can never make two
identities incomparable. `EffectOwner` is sealed, so the roster is those two markers or
nothing.

`Dc` therefore holds `Option<EffectIds<ByPath>>` rather than a private counter of its
own: there is nothing left to fall back on, so a `Dc` that never adopted the space is
honestly absent and `request_self_test` fails the stage rather than asking for a verdict
nobody could attribute. `Dc` driven outside a `Core`, meaning that file's own tests,
adopts a delegate in `dc_port_with`.

`one_space` is `pub(super)`, which keeps `boundary` and `main.rs` out of the business of
starting a space. Rust has no visibility that admits a module's own body while excluding
its descendants, so `core::config` and `core::path` can still reach it, and
`one_file_outside_this_one_starts_an_effect_identity_space` in `core/effect.rs` counts
the call sites in `src/` instead: exactly one, in `core/mod.rs`. Fixture spaces route
through `EffectIds::one_space_for_tests`, which is `#[cfg(test)]` and cannot exist in a
shipped binary. Six cases in `scripts/unconstructable.py` under
`T-effect-id: an identity the core never issued` hold the rest.

Uniqueness on its own is not what pins this. A test that only checked that two
identities differ would pass against a refactor that reintroduced the swap by another
route, so the safety property is asserted directly: an isolation self test verdict must
reach the path while a billing start is outstanding, and a powermeter completion must
never start the isolation monitor. Pinned by
`a_self_test_verdict_reaches_the_path_while_billing_is_outstanding` and
`a_powermeter_completion_never_satisfies_an_isolation_self_test`, both driven in the
ordering that produced the defect, with
`a_billing_start_and_a_self_test_never_share_an_identity` for the arithmetic and
`a_delegated_handle_draws_from_one_space` on `EffectIds` itself.

The passage above predates the self test verdict being carried at all, so it uses
"verdict" for the completion of the `ImdSelfTest` command. That completion now advances
nothing: it says only that the monitor accepted the request, and the stage waits for the
monitor's published answer, which is what `EvseManager.cpp` waits on in `selftest_result`.
The identity property is unchanged and still holds, because a powermeter reply must still
never be taken for the self test the stage awaits.

No gate caught this; it was found by drawing the event flow. `identity_allocation_is_deterministic`
asserts that two allocators driven identically agree, which is the collision written
down as a feature. It reads as a determinism property and it is a real one; what it does
not say is that no second allocator may exist. That sentence is now the one thing the
types and the source gate above say together, and the test is the only place in the
crate that asks for two spaces on purpose.

### Timer identities are one space too, and for the same reason

`Core::apply` matches `TIMER_ENERGY_FLOW_REQUEST` and `TIMER_SOFT_OVER_CURRENT` before
it offers a deadline to the power path, so a path timer that collided with either would
never reach the path at all and nothing downstream would notice: the deadline simply
never fires. `ac_with_soc`'s refresh deadline shipped that way in its first revision,
against the energy flow request, and only a test that drove the whole core saw it. The
path unit tests could not: they call `on_timer` directly, which is downstream of the
match that swallowed it.

The identities are therefore blocked out by owner rather than by convenience: 200 to
208 for the AC paths, 300 to 302 for `Dc`, 400 and 401 above the trait.
`every_timer_identity_in_the_module_is_unique` in `src/core/mod.rs` enumerates all of
them and asserts pairwise inequality, so the whole class is a compile-and-run gate
rather than a convention.

The one deliberate sharing is `ac::TIMER_T_STEP`, which three `AcTimer` variants use
because only one pilot detour runs at a time and a new step must supersede the previous
one rather than race it. That is decided in `ac::timer_id`, in one place.

## What the deployment has, and what the compiler knows about it

`config::resolve` is the one place that turns `Settings` plus live `Wiring` into
the collaborators this instance runs, and it returns them as one `Deployment`
rather than as separate values, because the pairing is the invariant.

Everything used to be constructed at boot whether the deployment used it or not,
each piece carrying a flag saying whether it should act. That is why four pieces
of work on this port were finished, correct and unreachable with nothing
failing: a flag that is never true is indistinguishable from a feature that
works, and only a person reading the code can tell them apart. Four
collaborators are now absent rather than switched off.

| Collaborator | Absent when | What its absence removes |
|---|---|---|
| `HlcPort` | `hlc_enabled` is false, which is only a basic AC port | thirty `if !self.config.enabled` guards, and `HlcConfig::enabled` with them |
| `IsolationMonitor` | `imd` is not wired | `CableCheckOptions`, which it owns, and the three manifest keys behind them |
| `OverVoltageMonitor` | `over_voltage_monitor` is not wired | the threshold derivation, and with it the only constructor of `Effect::OverVoltageLimits`'s payload |
| `SessionLogger` | `session_logging` is off | `SessionLogger::enabled`, `enable()`, and three early returns two of which were already dead |

`soft_oc::Detection` was the precedent and predates this: `None` on DC, because
`check_soft_over_current` is called from the AC branch of the charging state and
from nowhere else.

**One source of truth per question, and the survivor is the one that cannot be
wrong.** `config::hlc_enabled` is the single derivation of whether a deployment
speaks ISO 15118, and it has exactly one consumer:
`HlcConfig::for_deployment`, which is the only constructor of an `HlcConfig` and
answers `None` where the predicate is false. So a configuration that exists was
derived enabled, presence is the predicate's answer rather than a second copy of
it, and there is nothing left to disagree. `PowerPath::requires_hlc()` was that
second copy and is deleted: a `bool` on the path saying what the port's presence
already says is exactly the state divergence that replaces an unreachable-code
problem with a worse one.

The boundary needs the same configuration for the two commands it answers
synchronously (`update_allowed_energy_transfer_modes` and `set_der_available`).
It takes its handle from the port through `Deployment::hlc_config`, so the handle
is `Some` exactly when the port is, by derivation rather than by agreement.
`UpdateRefusal::NoHlc` is gone for the same reason: it restated the
configuration's existence, and the boundary answers that from the absence of what
it holds.

`scripts/unconstructable.py` is what makes this a measurement rather than a
claim. It writes each of these mistakes back into a scratch copy of `src/` and
requires the compiler to refuse it, asserting the diagnostic and not merely the
failure. Twelve constructions across the four classes. A case whose anchor no
longer appears exactly once aborts rather than passing, so a case cannot go stale
while still reporting green.

## Charge modes

`PowerPath` is a trait bound once at construction, with four implementations,
paired with their high level communication port in one `match` in
`config::resolve`:

| Implementation | Selected by | Holds an `HlcPort` |
|---|---|---|
| `AcBasic` | `charge_mode: AC`, HLC not wired or disabled | no, and it is the only arm that does not |
| `AcHlc` | `charge_mode: AC`, `hlc` and `slac` wired and `ac_hlc_enabled` | yes |
| `AcWithSoc` | `charge_mode: AC` and `ac_with_soc`, which requires `slac` and `hlc` | yes |
| `Dc` | `charge_mode: DC`, which hard-requires `slac`, `hlc` and `powersupply_DC` | yes |

The trait's three mode facts carry **no default bodies**: `target_voltage_v`,
`hlc_charging_active` and `presents_fake_dc` are required, so a new path is a
compile error until it says what it is. Each default used to be the AC basic
answer, which is how the ISO 15118 stop signalling came to be inert on DC: the
fact lived on `AcHlc`, the mode independent readers above the trait asked every
path for it, and `Dc` inherited `false` from a default nobody read. The test-only
`PathRecorder` is the fifth power path and answers all three explicitly too.

All three AC implementations hold the same `Iec` reducer, whose signature stays
narrow:

```rust
fn handle(&mut self, input: IecInput) -> Vec<IecCommand>
```

New behavior becomes an `IecInput` variant, never a new method. This is what keeps
the shared reducer from widening into a second coordinator.

`PowerPath::on_path_event` follows the same rule one layer up. It takes a
`PathEvent`, not the whole `Event`: a purpose built input carrying only what a
power path can act on. `Core::apply` is the single place that narrows one from
an `Event`, and an event no path can act on never becomes a `PathEvent` at all.
Neither match carries a catch-all, so a new variant is a compile error in every
implementation rather than a silent no-op, and a variant an implementation does
nothing for gets its own arm and a stated reason.

Session lifecycle, authorization, reservation, availability arbitration, error
handling and metering are mode independent in the C++ module and live above the
trait. `PowerPath` receives `&Session` and returns effects; it does not own
lifecycle.

Facts negotiated per session are data, not types, because they change while a
session is live:

```rust
struct SessionProfile {
    transfer: Option<EnergyTransferMode>,
    bidirectional: bool,
    der: Option<DerParams>,
    pwm_start: PwmStart,
}
```

The advertised energy transfer mode set is derived, not configured, and it lives on
`core::hlc::HlcPort` above `PowerPath` rather than on a path. Its inputs arrive at
runtime: board support capabilities on AC and power supply capabilities on DC, either of
which can narrow through derating while a session is live, and every change is
republished. The AC and DC derivations branch on charge mode inside one port instead of
being a method on the trait, because the C++ keeps one `supported_energy_transfers`
monitor for both and the recomputation entry points are two different subscriptions
rather than two paths.

`core::session::EnergyTransferMode` carries every value of the wire enum, not only the
nine this module derives. That is required rather than tidy:
`update_allowed_energy_transfer_modes` hands the module a list another module chose and
the port relays it, so a wire value with no local spelling would be dropped from what the
vehicle is then offered.

`bidirectional` is computed once from its three sources. The C++ recomputes
`hack_allow_bpt_with_iso2 or sae_bidi_active or session_is_iso_d20_dc_bpt()`
identically at three sites.

Losing bidirectional capability mid-session ramps an in-flight discharge down
cleanly and continues the session unidirectionally, permitting no new discharge
while the supply reports no capability; a capability that returns permits
discharge again, ramped up from the zero the ramp down left (ADR-0018,
"Bidirectional capability loss mid-session", as revised).

Five percent versus X1 PWM start is a field on `AcHlc`, not a separate
implementation, because a five percent session falls back to nominal after
`AC_X1_FALLBACK_TO_NOMINAL_TIMEOUT_MS` while an enforced session never does.

### `AcWithSoc`: the one mode that flips, and the one place it is written

IEC 61851-1 cannot ask a vehicle how full its battery is. ISO 15118 DC charging
can, in `DcEvStatus`, so `ac_with_soc` announces a DC port over ISO 15118, lets
the vehicle run a DC charge parameter discovery, takes the state of charge out
of it and reintroduces the same vehicle to the same port as a basic AC one.
Nothing DC is ever delivered: an `ac_with_soc` deployment has no power supply
wired, every DC subscription `EvseManager` installs sits inside its
`charge_mode == "DC"` branch which this port is not in, and the DC envelope the
vehicle is told is hardcoded in `EvseManager::setup_fake_DC_mode`.

The C++ does it by re-parameterizing one state machine: `switch_DC_mode` and
`switch_AC_mode` call `Charger::setup` again with the other `ChargeMode`. That is
the only thing in the module that would make a `PowerPath` runtime mutable, and it
is why the mode is an implementation of the trait instead. The trait object is
still chosen once by `config::resolve`; what changes inside it is
`hlc::PresentedMode`, which names what the vehicle is being told rather than
which implementation runs.

`AcWithSoc` composes an `AcHlc`, because underneath is where the hardware is and
the hardware is AC in both presented modes. Two of that path's answers read the
mode, and both are the same C++ `charge_mode` branch of the
`WaitingForAuthentication` case: whether a five percent offer stands
(`Charger::run_state_machine`'s `WaitingForAuthentication` entry, whose DC arm raises it unconditionally and whose "already
authorized" escape is explicitly AC only) and how the authorization loop leaves
the start. Both presented modes proceed at once and neither runs a pilot detour:
the DC arm "always stay[s] within 5 percent mode anyway", and the AC arm runs with
high level communication disabled for the session, because
`EvseManager::setup_AC_mode` is reached from `switch_AC_mode` and from the
`subscribe_dlink_error` arm and both pass `ac_hlc_enabled` false.

**One writer.** `AcWithSoc::present` is the only place the mode is written, and
it does three things together because a route doing two of the three would leave
the vehicle told one thing and the pilot doing another: the mode is recorded, the
inner path is told, and the stack is owed a re-announcement as a
`SessionDuty::AnnounceMode`. Four routes reach it, and they are the four
`EvseManager` has: the boot (`ready` calling `setup_fake_DC_mode` in place of
`Charger::setup`), the state of charge, the deadline and the unplug, and the data
link error. It is idempotent, which is what lets every route state its
destination without first testing where the port is.

The announcement is a duty rather than an effect the path emits, for the reason
every other `SessionDuty` is one: the identity, the debug flag and the advertised
set live on `hlc::HlcPort` above the trait and none of them is a charging
decision. `HlcPort::announce` deliberately does not store the fake DC set, so the
derivation that port owns is untouched by a flip; that is what keeps `charge_mode`
immutable, and it reproduces the C++ consequence that an AC capability report
arriving while the fake DC mode is presented overwrites the announcement.

The reinitialization that follows the flip to AC is `EvseState::Reinit`, ported as
two reducer inputs plus a wait. `Charger::process_pending_reinit_request` will not
break the control pilot while SLAC is still matched: it asks the vehicle to end
its ISO 15118 session and comes back on a later pass. The port waits on the same
fact, on an event rather than a poll, which is why `HlcEvent::SlacMatched` exists
beside `MatchingStarted`: the C++ keeps both members and the narrower one is this
wait's whole gate. `Reinit` leaves the transaction open, which is the reason the
mode can reintroduce a vehicle mid session without splitting its billing record.

### Where the stop and error signalling lives

What the vehicle is told when a session ends sits on `Core`, not on a power path, because
that is where it sits in the C++: `Charger` is one state machine for both charge modes and
neither of the two blocks branches on the mode.

| C++ | Here |
|---|---|
| `Charger.cpp:1053-1063`, the `StoppingCharging` entry | `Core::ask_vehicle_to_stop` |
| `Charger.cpp:1430-1438`, `Charger::cancel_transaction` | `Core::stop_error_for` |

The edge that drives the first is `SessionDuty::AskVehicleToStop`, raised by
`duties_for_edge` beside the `StoppingCharging` publish it follows in the C++. Both AC
paths and `Dc` hold a `SessionProgress`, so all three raise it from the same mapping and
none of them holds a copy of the decision.

Three facts gate the two blocks, and each is held where its value is actually decided.

- **`hlc_charging_active` stays on `PowerPath`.** Its value is settled by the charge mode
  at the `Idle` entry (`Charger.cpp:226-233`: false for AC, true for DC, "for DC, it is
  always HLC mode"), and on AC alone it is then raised by `v2g_setup_finished`
  (`:2278-2281`). Each implementation is therefore the whole of its own answer: `Dc` is
  the constant `true`, `AcBasic` the constant `false`, and only `AcHlc` needs a field.
  Moving it onto `Session` was considered and rejected: `AcHlc::emit`, which every effect
  that path produces passes through, reads it, and `emit` is reached from
  `PowerPath::on_startup` and `to_safe_state`, neither of which takes a `&Session`.
  Putting the fact on the session would mean widening those two trait methods, which is a
  change to the `PowerPath` boundary and a larger decision than this fact.
- **`flag_paused_by_evse` moved onto `Session`.** It is `shared_context` state written
  from outside the state machine by `Charger::pause_charging` and `resume_charging`
  (`:1406-1420`) under a `flag_transaction_active` guard the core owns, and read by arms
  both charge modes reach. `Session::paused_by_evse` is that flag; `Core` writes it and
  `Core::end_session` clears it, matching `Charger::stop_session` (`:1472`).
- **`hlc_d20_active` moved onto `Session` too.** Its only writer is
  `Charger::set_hlc_d20_active` (`:2295-2296`) from the ISO 15118-20 service selection
  (`EvseManager.cpp:979-983`), which reaches this port as `HlcEvent::SelectedService` and
  is mode independent. It is `Session::iso15118_20_active`. The sibling fact the same
  handler records, the selected service, stays on `HlcPort`: its consumers are the AC
  limit emissions and it is forgotten again on a `D-LINK_TERMINATE`, which is a different
  lifetime.

The `else` branch of `Charger.cpp:1053-1063`, the drop to X1, is not on `Core`. Withdrawing
the pilot offer has no DC meaning, so it stays on the path that owns a pilot: `AcHlc::emit`
filters the withdrawal out of the commands the reducer produced while high level
communication carries the session, and `AcBasic` never filters at all.

One consequence is a read that has to be taken early. `Core::apply` samples
`hlc_charging_active` at the pass boundary rather than when it discharges the duty, because
one pass here is several C++ state machine iterations: an unplug during a charge crosses
the `StoppingCharging` entry that reads the flag and the `Idle` entry that clears it in the
same call, and the C++ order puts the read first.

### The power meter's measurable floors narrow what the vehicle is offered

`min_import_current_A` is the floor below which a car side power meter cannot measure
within its accuracy class, which is a legal constraint where German Calibration Law
applies rather than an electrical one. A vehicle offered a current below it would draw
power that cannot be billed.

`EvseManager` subscribes the report (`EvseManager.cpp:249-250`) and
`EvseManager::update_powermeter_capabilities` does three things with it: it stores the
report unless it is the one already held, it writes one session log line, and, on a DC
port with high level communication, it pushes to the stack, where
`EvseManager::apply_powermeter_limits` merges the report's two floors into what the
vehicle is told it may draw. All three are ported, and they are split the way the C++ splits
them: only the third is inside `if (hlc_enabled and charge_mode == "DC")`.

So the report itself is **not** a high level communication fact and is not kept
on that port. `core::powermeter_limits::CarSideMeter` holds it, on `Core`, which
is where the C++ has it (`EvseManager::powermeter_capabilities`,
`EvseManager.hpp:310`) and where it has to be: a basic AC port with a car side
meter records the floors and writes the same transcript line, and a basic AC port
has no high level communication port to keep them in. `DcLimits` holds no copy;
it takes the floors as an argument at each of its three merge seams, so one
holder answers both the transcript and the wire and the two cannot disagree about
what the meter said. What is left on the port is the charge mode gate on the
push, which is the whole of `HlcPort::note_floors`, pinned by
`an_ac_port_with_a_stack_records_the_report_and_still_tells_the_vehicle_nothing`:
dropping it sends an AC port's vehicle a DC capability message.

`core::powermeter_limits` owns the arithmetic, `hlc::dc_limits` owns the seam,
and `Core::handle`'s arm owns the order.

Four properties are worth stating, because each is a way the merge could be got wrong
and still compile.

- **It is a floor, not a clamp in both directions.** Every statement in
  `apply_powermeter_limits` reads a minimum and writes the same minimum, so the band the
  vehicle is offered can only shrink from below. A meter that can measure down to one
  ampere does not widen a four ampere offer to one. Pinned by
  `a_meter_floor_above_the_supply_minimum_narrows_the_offer` and
  `a_meter_floor_below_the_supply_minimum_widens_nothing`, which are the same fixture read
  from the two sides: a clamp passes the first and fails the second.

- **The two directions cross.** The meter names its floors from its own point of view, so
  the meter's `min_import_current_A` raises the supply's **export** minimum and the
  meter's `min_export_current_A` raises the supply's **import** minimum. Filled by name at
  every statement, in the boundary handler included, because both halves have the same
  shape and a swapped positional literal would pass every arithmetic test that drove one
  direction. Pinned by `the_meters_charging_floor_raises_the_supplys_export_minimum` and
  `the_meters_discharging_floor_raises_the_supplys_import_minimum` together.

- **A floor above the available maximum is clamped down to the maximum.** The one
  statement that lowers a figure, and it lowers the minimum, so the offer still never
  widens. What it is clamped against differs between the two seams; see the derating entry
  in the divergence list.

- **An absent import minimum is adopted, an absent nominal minimum is not.** The supply's
  `min_import_current_A` is optional, and `max_optional` gives the meter's figure outright
  when the supply named none, so a car side meter can introduce an import minimum the
  supply never had. A nominal minimum the supply never reported stays absent, deliberately
  and for the reason the C++ states at the same statement: a fabricated one would bypass
  the consumers' fallback to the regular minimum. Pinned by
  `an_unnamed_import_minimum_is_adopted_from_the_meter` and
  `an_unreported_nominal_minimum_is_not_fabricated`.

Two gates ride with it, both C++ ones. The store and the session log line are **not**
gated on the charge mode, so an AC port records a floor it does not apply and holds it;
the push is gated on `hlc_enabled and config.charge_mode == "DC"`
(`an_ac_port_records_the_report_and_offers_the_vehicle_nothing`). And a report identical
to the one already held returns before the log line, so a meter republishing on a timer
costs the transcript nothing (`a_report_the_port_already_holds_writes_nothing`).

**What this replaced.** The merge arrived upstream during the rebase and was left a
recorded no-op rather than ported mid-rebase: the transcript line carried a
`NOT applied to any limit` clause so an operator comparing two transcripts could tell the
ported half from the unported one, and four tests asserted the drop. Two of them,
`both_floors_reach_the_transcript_and_the_line_says_they_are_unapplied` and
`an_absent_floor_is_written_as_not_available`, asserted that clause and now assert the
C++ line. The other two,
`the_record_is_the_whole_effect_of_the_report` and
`a_floor_above_the_standing_limit_still_changes_nothing`, asserted that exactly one effect
came out; both drove an AC core, where the C++ push is gated off, so **neither could have
failed when the merge landed** despite being written to. They are kept, renamed to
`an_ac_port_records_the_report_and_offers_the_vehicle_nothing` and
`an_ac_port_offers_nothing_however_high_the_floor`, and now say that they pin the charge
mode gate. The DC behaviour they were meant to cover is driven in
`core::tests::what_a_car_side_meter_floor_narrows`.

## Identity

Three distinct names are used in code so the "connector id" ambiguity above
cannot recur: `evse_index` for the framework three-tier mapping,
`legacy_topic_id` for the configuration label used by Node-RED topics and the
reported EVSE id, and an `EnableScope` enum with `Evse` and `Connector`
variants replacing the magic zero on `enable_disable` and `force_unlock`.

Wire behavior stays identical to the C++, including its asymmetry, which is
narrower than it first looks. Connector enabled state is assigned only for
`EnableScope::Connector`, and that gate applies to enable and disable alike
(`Charger.cpp:1853-1855`). What is not scope gated is the disable's effect on
the state machine: a winning disable sets the disable request and routes any
active session to stopping regardless of scope (`Charger.cpp:1887-1900`).
Re-entry to idle on enable is gated on connector enabled state
(`Charger.cpp:1857`), which is why an EVSE scoped enable cannot restore a
disabled connector.

The module asserts at startup that it maps to exactly one connector and fails
loudly otherwise, which matches what the C++ actually does since its session
events hardcode connector 1.

### What the billing meter is told, and the two fields that decide it

`Charger::start_transaction` fills seven fields of `types::powermeter::TransactionReq`
and this module fills the same seven. Two of them are the ones a signed OCMF
record is read back for, and neither has a source inside this module:

`identification_type` is the credential kind, and it is a total mapping of
`id_token.id_token.type` through `utils::convert_to_ocmf_identification_type`
in `EvseManager/utils.hpp`: eight `types::authorization::IdTokenType` values
onto eight of `OCMFIdentificationType`'s eighteen. `MacAddress` is the only one
that maps to `UNDEFINED`, because OCMF has no autocharge type. The mapping's far
side is a wire enum `core` cannot name, so it lives at the boundary as
`ocmf_identification_type` and what `core` carries is the credential kind:
`core::token::IdTokenType`, derived from the record in `IdTag`'s single
constructor beside `value` and `plug_and_charge`. It is deliberately not derived
from `plug_and_charge`: that reads `authorization_type`, which is a different
question, and a contract presented over external identification and a card
negotiated over ISO 15118 are the pair the two answers disagree about.

`tariff_text` is what the driver was quoted, and its only source is the
authorization verdict: `validation_result.tariff_messages.at(0).content`, behind
an `empty()` guard that leaves the field unset. `Command::AuthorizeResponse`
therefore carries the verdict's messages as `core::auth::TariffMessages`
alongside the status and the certificate status, `Auth::authorize` records them
in the same statement pair `Charger::authorize` assigns
`shared_context.id_token` and `shared_context.validation_result` in, and
`Core::mirror_authorization` is the one writer of the session read model the
metering start bills from. `TariffMessages::text` is the one reader of the list,
which is what keeps "the first message" a single decision;
`scripts/unconstructable.py` proves a second reader does not compile.

Three things are deliberately not carried, and each is a narrowing with no
consumer rather than a value replaced by a default:

- The rest of `ValidationResult`. `expiry_time`, `parent_id_token`, `evse_ids`
  and `allowed_energy_transfer_modes` reach no field this module writes;
  `reservation_id` and `certificate_status` are read at the boundary, before the
  verdict reaches `core` (`evse_managerImpl::handle_authorize_response`), which
  is where the C++ reads them too.
- `MessageContent::format` and `MessageContent::language`. `tariff_text` on
  `powermeter.yaml` is a bare string, so there is nowhere for either to go. The
  C++ carries a standing `TODO` to select by language when several messages
  arrive; closing it would widen `TariffMessages` rather than find a field
  silently dropped.
- The other ten `OCMFIdentificationType` values - `DENIED`, `EVCCID`, `EVCOID`,
  `ISO7812`, `CARD_TXN_NR`, the two numbered `CENTRAL` variants, the two
  numbered `LOCAL` variants and `PHONE_NUMBER`. No credential this interface can
  present reaches any of them, in this port or in the C++, so this module never
  signs one.

`identification_level` and `identification_flags` stay unset and empty, which is
what the C++ does and for the reason its own two `TODO`s give: neither is known
to EVerest.

## Configuration

Five config keys accepted by the C++ manifest are not accepted here:
`hack_skoda_enyaq`, `hack_present_current_offset`,
`hack_pause_imd_during_precharge`, `hack_fix_hlc_integer_current_requests`,
`hack_simplified_mode_limit_10A`. No shipped config or test sets any of them and all
default to inert. `hack_allow_bpt_with_iso2` is supported; ten shipped configs use
it.

`ac_with_soc` was the sixth and is accepted now, along with the `reinit_method` and
`reinit_duration_ms` keys the mode's reinitialization reads. It is implemented as
a `PowerPath` of its own; see "Charge modes".

Supplying an excluded key is a startup error naming it, never a silent no-op,
because a deployment that sets a hack key and gets a module which quietly does
not do it is the failure this exclusion exists to prevent. The guarantee holds on
every boot path and in every config group this module owns.

It holds under `ConfigBootMode::Database` as well. This document said it did not,
on the grounds that `SqliteStorage` builds `ModuleConfig` member by member and does
not carry `undeclared_configuration_parameters`. That much is true and it does not
matter, so the claim was wrong when it was written: nothing reads the set out of
storage, because the manager recomputes it. `ManagerConfig` calls `parse` for every
boot mode: `ManagerConfig::init_from_yaml` for the YAML route and the pre-loaded
database route beside it, both in `lib/everest/framework/lib/config.cpp`. Then
`load_and_validate_manifest` rebuilds the set from whatever
`configuration_parameters` it was handed, whether those came from the YAML or from
`storage->get_module_configs`, which is now read once during boot in
`lib/everest/framework/lib/runtime.cpp` rather than inside `config.cpp`. So a stored config naming a key this manifest does not declare, which is
what a module upgrade that drops a key leaves behind, refuses startup on the next
`--db` boot exactly as a config file does.

The keys a database never holds are the ones parsing already dropped: a `--db-init`
boot refuses on the excluded key and writes the config without it, since the value
is discarded before the write. A later boot then has nothing to refuse and nothing
silently unapplied, because the stored config no longer asks for it.

Having the guarantee at all needed a framework change, because the key never
reached Rust.
`core::config::reject_unsupported_keys` was correct and covered but could not
observe an excluded key: the manager resolves module config in `parse_config_map`
(`lib/everest/framework/lib/config.cpp`), which builds its result by iterating the
manifest schema. A key the config file supplies but the manifest does not declare
lands in a separate `unknown_config_entries` set, is logged as `Unknown config
entry '{}' of module '{}' ignored, please fix your config file!`, and used to be
dropped there. `Runtime::get_raw_config` reports the parsed `!module` group, so it
carries every declared key with its manifest default applied and never an
undeclared one.

The framework now retains that set on the module config and reports it through
`Runtime::get_undeclared_config_keys`, which the generated `Module` wrapper
mirrors. Startup feeds it to `reject_unsupported_keys` alongside the raw map. The
rejected alternative was declaring the six keys in `manifest.yaml` purely to refuse
them: self contained, but it makes the manifest advertise keys that exist only to
be refused, and it hardcodes six refusals into one module. Surfacing the set
exposes a value the framework already computes and generalizes to every module and
every undeclared key.

Every group the module owns is reported: its own config group and the config group
of each of the five interfaces it provides. `Runtime::get_undeclared_config_keys`
returns one entry per key, carrying the group that supplied it, and
`reject_unsupported_keys` refuses an excluded key in any of them, naming the group
when it is not the module's own. Checking only the module's own group would leave
`config_implementation: evse: hack_skoda_enyaq: true` quietly doing nothing, which is
the same failure one level down. The C++ drops that key too, so this is the
exclusion being loud where the C++ is silent rather than a new behavior. The group
travels with the key instead of being flattened away, because reporting an
interface's key as one of the module's own would tell a module it was handed
something it was not.

Both properties are pinned by tests: `Config reports undeclared config keys per
group` and `Config reports undeclared config keys on the database boot path` in
`lib/everest/framework/tests/test_config.cpp`. The second asserts the stored key
and not the one the config file supplies, so it also proves which source the
config was read from.

`ac_with_soc` was excluded on architectural grounds as well as usage: it configures
AC hardware as `ChargeMode::DC` and flips between them on a one hour timer, which is
the only thing that would make the power path runtime mutable. It is accepted now
because that flip is a power path of its own rather than a setting the other paths
read, so nothing about the trait object changes at runtime.

## Diagnostics

Structured logging at state transitions, effect failures and rejected inputs, plus
the queue depth and latency warnings.

A bounded per-session trace records event kinds, effect kinds, state transitions and
timestamps, and is discarded when the session ends. It never records payloads. No id
token, EMAID or meter value enters the trace, so no redaction step is required.

Two kinds are excluded: `Effect::SessionLog` and `Event::V2gMessage`. Both belong to the
session transcript, which is a record of the same session in more detail and with wall
clock stamps, and both arrive once per protocol message. Left in, one logged ISO 15118
session would spend the whole `TRACE_CAPACITY` on transcript entries and evict the events
the buffer exists to keep, so enabling logging would degrade post mortem on exactly the
sessions most likely to need it.

The per session transcript itself is the other diagnostic and is described under
divergences 5 and 5b: one directory per session under `session_logging_path`, holding a
CSV and an HTML transcript, written only while `session_logging` is set.

There is no metrics registry, no correlation id scheme and no persistent ring
buffer.

## Testing

`core` is driven directly with no threads, no runtime and no sleeps, because time is
a parameter. This is the primary suite and it is registered with CTest so it runs in
the documented CMake workflow.

Behavior parity is gated two ways:

1. The `core_tests` integration suite is parametrized over both modules by config,
   so every existing scenario runs against C++ and Rust alike, and a new suite
   covers the DC hazards that have no C++ test today: cable check step ordering,
   precharge dual gating, continuous isolation monitoring, over voltage threshold
   derivation, current ramp limiting, import export switch ordering with cache
   invalidation, and shutdown ordering.
2. Recorded outbound traffic from the C++ module is replayed against the Rust module
   and diffed, for the DC sequences where the hazards concentrate and where no
   existing test asserts ordering.

The OCPP suite must pass with this module substituted before replacing the C++
module is proposed.

### The optional collaborators are driven present and absent, not sampled

Four collaborators are absent on some deployments and present on others, and the
cells of that cross product are cheap to leave undriven because absence usually
looks like silence. Three are pinned by name:

- `a_deployment_with_no_stack_never_addresses_one` drives all twenty eight
  `HlcEvent` variants against a basic AC core and requires that no `HlcUpdate`,
  `SlacUpdate`, token publication or session failure escapes. The boundary
  subscribes to those callbacks whether a stack is wired or not, so every one of
  them can reach such a core.
- `an_over_voltage_monitor_works_on_a_port_with_no_isolation_monitor` is the
  fourth corner of the two DC monitors' cross product, and the one where the
  cable check short circuits while the over voltage monitor's commands hang off
  the stages either side of it.
- `an_ac_port_with_a_stack_records_the_report_and_still_tells_the_vehicle_nothing`
  is the charge mode gate on the car side meter push, which the two tests beside
  it could not reach: they drive a basic AC port, which has no stack, so what
  they pin is the absence and not the gate.

The cells that stay undriven are the ones `config::resolve` cannot produce: a
basic AC port beside a stack, an ISO 15118 path without one, a DC path with a
soft overcurrent detector, and the test recorder with a stack. Each is reachable
only by building `CoreParts` by hand, and
`the_only_path_without_a_port_is_the_basic_one` pins the pairing production gets.

## Intentional divergences

The port replicates the C++ module's observable behavior except where that behavior is
a defect. Each divergence below is deliberate, is pinned by a test that would fail
against the C++ behavior, and is whitelisted by the golden trace comparison.

The numbers are stable identifiers, not a count: source comments cite them
(`core/faults.rs` cites 1, `core/path/dc.rs` and `core/mod.rs` cite 4), so a withdrawn
entry keeps its number rather than renumbering the rest. There is no entry 3; it was
withdrawn. Entries 2 and 10 are withdrawn divergences kept as records of why the port
does *not* differ, so the live count is lower than the highest number.

1. **The all errors cleared signal accounts for the over voltage monitor.**
   `modules/EVSE/EvseManager/ErrorHandling.cpp:185-189` sums the active errors of every
   error reporting requirement except `r_over_voltage_monitor`, then fires
   `signal_all_errors_cleared` when that sum reaches zero. The omission is inconsistent
   with `errors_prevent_charging` at `ErrorHandling.cpp:232-235`, which does consult the
   over voltage monitor. The consequence is that a still active over voltage monitor
   error, such as the non blocking `over_voltage_monitor/VendorWarning`, cannot hold the
   signal back, so an OCPP 1.6 backend is told every error is gone while one is not. The
   Rust count includes the over voltage monitor. Pinned by
   `the_all_errors_cleared_signal_waits_for_the_over_voltage_monitor` in
   `src/core/faults.rs`, which raises an isolation monitor error and an over voltage
   monitor error and asserts the cleared signal fires only once both have cleared. Only
   a non blocking over voltage monitor error exposes the omission: a blocking one keeps
   `Inoperative` raised, which the count does include.

2. **A repeated withdraw is reported as an authorization timeout, as in the C++.**
   This was a divergence and has been withdrawn. `Charger::deauthorize_internal` tests
   only `flag_authorized`, so every
   `withdraw_authorization` that finds no authorization takes the same branch and
   reports a plug in timeout, raising `evse_manager/MREC9AuthorizationTimeout` when
   `raise_mrec9` is set. That branch returns at `:1806` without calling `stop_session`,
   so the session stays active and the state stays `WaitingForAuthentication`. A second
   withdraw therefore reaches `:1799` again and reports a second timeout that nothing
   timed out. Two withdrawals in a row are ordinary: the `Auth` module sends one when its
   connection timeout expires and again when a reservation it was holding expires. The
   port previously tracked whether an authorization was still expected in the session and
   reported the timeout at most once, which suppressed announcements the C++ makes. Under
   "match EvseManager, not improve on it" that suppression was removed, so a second
   withdraw now reports a second timeout and raises `MREC9AuthorizationTimeout` again.
   Pinned by `two_consecutive_withdrawals_report_two_timeouts`,
   `two_consecutive_withdrawals_raise_mrec9_twice`,
   `a_withdraw_after_a_granted_and_dropped_authorization_still_reports_one` and
   `a_withdraw_after_a_revoked_authorization_still_reports_a_timeout` in
   `src/core/auth.rs`. The mislabel is
   only observable while a session is active and the state is one of `Disabled`, `Idle`
   or `WaitingForAuthentication`, which is the guard at `Charger.cpp:1796`, and it is only
   observable as an error rather than as a session event in a deployment with
   `raise_mrec9` set, which is the gate at `:1801`. Both tests create those conditions:
   the fixture holds a live session with no authorization, and the error test asserts the
   setting is on before it counts raises.

4. **Losing bidirectional capability mid session ramps the discharge down and refuses
   the next one.** ADR-0018. The C++ never decides what happens at this transition. It
   recomputes a disjunction over the three bidirectional sources at three call sites,
   `modules/EVSE/EvseManager/EvseManager.cpp:2366`, `:2405` and `:2555`, and none of the
   three consults `powersupply_capabilities.bidirectional`, which the supply can change
   while a session runs; the subscription comment at `:219` names derating as a cause.
   So a withdrawal reaches the supply as whichever direction the next
   `powersupply_DC_set` happens to resolve to, and the direction switch at `:2405-2411`
   issues `setMode(Export)` with the previous discharge current still applied, turning
   the cable round under load. The transition is untested in the C++ because it is not a
   decision anyone made.

   The port takes an in flight discharge to zero **in the import direction** first and
   leaves the direction alone, so the turn round the next target performs happens at zero
   current, and the session continues unidirectionally. No new discharge is permitted
   while the supply is reporting no capability: the withdrawal sets a refusal that vetoes
   all three sources, so a vehicle that selects a bidirectional service again is refused.

   **Revised.** The refusal was originally a latch released only when the session ended,
   so a supply that recovered its capability mid session was refused for the rest of that
   session too. That half is withdrawn. It was a behaviour the C++ does not have -- the
   C++ recomputes its disjunction and resumes, because the supply's capability is not a
   term in it -- and "match EvseManager, not improve on it" governs. The refusal now
   tracks the capability the supply reports **now**: a report that carries it again lifts
   the refusal, refreshes the resolution and lets the same session discharge once more,
   and a later withdrawal is its own edge that earns its own ramp down. The release on
   session end is kept beside it, for the supply that withdraws and then stops reporting
   at all.

   What is **not** withdrawn is the ramp, in either direction. The zero first ramp down is
   the whole reason the decision exists, and a resumed discharge rises from that zero
   rather than stepping to the magnitude it was running at. The rise costs no new
   machinery and is not a second policy beside the C++'s: `apply_new_target_voltage_current`
   (`modules/EVSE/EvseManager/EvseManager.cpp:2681-2703`) rate limits every rise in the
   target magnitude at `dc_ramp_ampere_per_second`, whichever direction the supply is in,
   and the port's `Dc::advance_ramp` is that low pass. What the withdrawal contributes is
   the zero the rise starts from.

   Pinned by the `bidirectional_withdrawal` module in `src/core/path/dc.rs`, whose tests
   assert the ramp down effect, its ordering against the turn round, the refusal of a
   later export to grid, that a withdrawal on a charging session actuates nothing, that a
   repeat emits nothing further, and that the discharge resumes **ramped** when the
   capability returns; and by `a_withdrawn_capability_clears_the_fact_and_tells_the_path`,
   `only_the_capability_returning_restores_the_fact_within_the_session`,
   `a_second_withdrawal_after_a_recovery_ramps_the_discharge_down_again`,
   `the_refusal_does_not_outlive_the_session_it_happened_in`,
   `a_unidirectional_supply_on_an_ordinary_session_withdraws_nothing` and
   `a_repeated_unidirectional_report_announces_the_withdrawal_once` in the
   `bidirectional_resolution` module of `src/core/mod.rs`. The withdrawal is a DC fact, so
   it lies outside nothing the golden trace comparison covers and must be whitelisted there
   by identifier.

   The zero first ordering is deliberately **narrow**: it applies to the withdrawal and not
   to the ordinary import to export switch of a healthy bidirectional charge loop, which
   still writes mode then setpoint as the C++ does. The two are inconsistent and that is
   the decision: ADR-0018 is scoped to capability loss, and widening the ordering would
   change the C++ behavior of the normal charge loop, which is a separate call that has not
   been made. `a_direction_switch_on_a_live_supply_precedes_the_setpoint_that_follows_it`
   in `src/core/path/dc.rs` pins the unchanged half.

   **The bidirectional fact is computed once**, into `SessionProfile::bidirectional`, by
   `Core::refresh_bidirectional`, which every source change calls. The single resolution
   carries the AC_BPT term, which is the four term form at `:2555` and not the three term
   form at the two DC sites; see "The single bidirectional resolution carries AC_BPT"
   below for why that is safe here.

5. **Session log fields are escaped correctly.**
   `modules/EVSE/EvseManager/SessionLog.cpp:232` writes a CSV record as
   `fmt::format("\"{}\",\"{}\",\"{}\",\"{}\"\n", ts, origin, msg, xml_pretty)`, quoting
   each field but never doubling a quote inside it, so one quote in a message or in a
   JSON payload shifts every following field of that record and the transcript stops
   parsing as CSV. On the HTML side `html_encode` at `SessionLog.cpp:261-266` replaces
   `<` and `>` and nothing else, so an ampersand in a payload reaches the file bare and
   the document is not well formed. The Rust version emits RFC 4180 fields with embedded
   quotes doubled, and escapes `&`, `<`, `>`, `"` and `'` character by character, which
   also makes the ordering hazard of a replacement chain unrepresentable: an ampersand
   introduced by escaping can never be escaped again. The already encoded direction
   marker `EVSE&gt;CAR` is composed from fixed strings rather than passed through the
   encoder, so it is not double escaped either. Pinned by
   `a_message_carrying_a_quote_an_ampersand_and_an_angle_bracket_parses_as_csv` and
   `a_message_carrying_a_quote_an_ampersand_and_an_angle_bracket_is_well_formed_html` in
   `src/core/session_log.rs`.

5b. **ISO 15118 payloads are recorded unformatted.**
   The C++ round trips every payload through `v2g_message` to pretty print it
   (`SessionLog.cpp:200-208`), which costs an XML library dependency. The Rust version
   writes the payload as received. The content is identical and only indentation
   differs, which no consumer of the transcript depends on. Pinned by
   `an_iso_payload_is_written_unformatted` in `src/core/session_log.rs`.

**Divergences 5 and 5b are reachable.** `SessionLogger` is constructed in `main`, from
`session_logging_path` and `session_logging_xml`, and enabled by `session_logging`. The
core emits `Effect::SessionLog` and `EverestEffects::session_log` performs it. The ISO
payload feed is wired too: `on_v_2_g_messages` sends `Event::V2gMessage`, which is the
one payload bearing record in the C++. The golden trace comparison may whitelist both.

The text of the two entries above previously ended with a paragraph saying the logger was
never constructed and that all three config keys were read by nothing. That paragraph was
half wrong while it stood: `session_logging` was read, at `src/core/hlc/setup.rs:163` and
`:241`, and reached the ISO 15118 stack as its `debug_mode`. See "One config key, three
subsystems" below, which is the trap that claim helped hide.

**One config key, three subsystems.** `session_logging` drives three separate things, in
the C++ and here, and none of them satisfies another:

| What it drives | C++ | Here |
|---|---|---|
| This module's own per session transcript | `session_log.enable()`, `EvseManager.cpp:144` | `SessionLogger::enable`, called in `main` |
| The ISO 15118 stack's `debug_mode`, a different module | third argument of `call_setup`, `EvseManager.cpp:992` and `:1611` | `HlcUpdate::Setup { debug_mode }`, `src/core/hlc/setup.rs:241` |
| Whether the ISO message feed is logged at all | gates `subscribe_v2g_messages`, `EvseManager.cpp:1066`, and re-checked at `:1906` | the logger's own `enabled` gate, since an everestrs subscription is static |

So `session_logging: true` turns on a transcript AND puts another module into debug mode.
`session_logging_path`, `session_logging_xml` and `logfile_suffix` drive only the
transcript. An operator who wants one of the two behaviors gets both; that is the C++
behavior and it is preserved rather than split, because splitting it would silently change
the meaning of every shipped configuration that sets the key.

**When logging cannot start, it says so and abandons the transcript.** Every filesystem
failure is logged at error level naming the path and the OS reason, the transcript for
that session is abandoned, and the next session tries again, so a transient fault is not
a permanent loss of logging. Nothing propagates: `EverestEffects::session_log` always
answers `EffectOutcome::Ok`, so no route exists by which a logging fault reaches the
charge. Two things an operator can check without reading the log:
`session_started.logging_path` on the `session_event` wire is filled only when the
transcript actually opened, so an absent path on a port with logging on means it failed;
and a single INFO line at boot names the root, says that nothing is written until a
session starts, and says that the same key drives the ISO 15118 debug mode. That line
exists because the failure it replaces was silence: a correctly configured port that has
not charged yet writes nothing, which from the filesystem is indistinguishable from a
broken feature.

6. **The relays open isolation self test verdict is awaited before energizing further.**
   The C++ starts the self test and collects its verdict later, concurrently with the
   contactor wait and the ramp up: `modules/EVSE/EvseManager/EvseManager.cpp:2113`
   skips the wait when `cable_check_enable_imd_self_test` is also set, and `:2228`
   defers to the wait at `:2233`. So the cable can be taken from the relays open
   voltage up to the full isolation voltage while the verdict on the isolation monitor
   is still outstanding. The Rust stage machine awaits the verdict in the stage that
   requested it, so the cable is never energized above the relays open voltage on an
   unverified isolation monitor. This is an ordering divergence inside the cable check
   sequence and is therefore visible to the golden trace comparison, which whitelists
   it by identifier. Pinned by `the_full_cable_check_sequence_runs_in_the_ported_order`
   and `a_self_test_verdict_that_never_arrives_fails_the_cable_check` in
   `src/core/path/dc.rs`.

7. **The bounded wait for an energy budget applies to AC as well as DC.**
   `modules/EVSE/EvseManager/Charger.cpp:354` gates
   `WAIT_FOR_ENERGY_IN_AUTHLOOP_TIMEOUT_MS` on `charge_mode == DC`, so an AC session
   waits unbounded for an energy budget later, in `PrepareCharging` at
   `Charger.cpp:759-763`. A deployment whose energy manager never answers therefore
   hangs an AC session with no timeout and no diagnostic. The port applies the same
   five second bound to both modes. It can only ever shorten a wait, never extend
   one. This divergence is AC only, so it lies outside the DC sequences the golden
   trace comparison covers. Pinned by
   `a_second_look_while_waiting_for_energy_neither_proceeds_nor_re_arms` in
   `src/core/path/ac.rs`.

8. **The external energy transfer mode narrowing actually reaches the stack.**
   `modules/EVSE/EvseManager/evse/evse_managerImpl.cpp:542-580` handles
   `update_allowed_energy_transfer_modes` by relabelling the requested modes into
   `filtered_energy_transfer_modes` with `std::transform`. That vector is only
   `reserve`d, never `resize`d, so the transform writes through
   `begin()` past `end()` of an empty vector and `size()` stays zero. The
   emptiness check at `:574` therefore always holds, so the command answers
   `IncompatibleEnergyTransfer` for every request, never reaches
   `call_update_energy_transfer_modes` at `:578`, and writes outside the vector's
   size on the way. The port relabels into a fresh vector, answers `Accepted` and
   makes the call. Pinned by `a_request_passes_through_untouched_on_a_non_mcs_connector`,
   `an_mcs_connector_relabels_the_dc_modes_of_a_request` and
   `a_request_narrowed_to_nothing_is_refused_as_incompatible` in
   `src/core/hlc/mod.rs`, and by `an_accepted_transfer_mode_narrowing_reaches_the_stack`
   in `src/core/mod.rs`. The C++ answer is not reachable as a deliberate outcome:
   `interfaces/evse_manager.yaml` declares `minItems: 1` on the argument, so no
   conforming caller can ask for an empty set and there is no request the refusal
   is the right answer to. Two behaviors the C++ has here are *not* divergences and
   are preserved: the relabelling names `DC` and `DC_BPT`, the ISO 15118-20
   spellings, and not `DC_extended`, which is what this module's own DC derivation
   advertises; and the command tells the stack directly without changing the
   advertised set, so the published `supported_energy_transfer_modes` variable
   keeps reporting the derivation. Both are pinned, the first by
   `an_mcs_connector_leaves_the_iso_2_spelling_of_dc_alone`.

9. **The cable check completion verdict is sent exactly once per attempt.**
   `call_cable_check_finished` has five call sites in
   `modules/EVSE/EvseManager/EvseManager.cpp`: the no isolation monitor skip at `:2058`,
   the success at `:2319`, the two wait helpers at `:2465`
   (`wait_powersupply_DC_voltage_reached`) and `:2494`
   (`wait_powersupply_DC_below_voltage`), and `fail_cable_check` at `:2571`. Nothing is
   missing; the defect is a **double send** on one route. When `cable_check_should_exit()`
   goes true mid wait, the helper emits `CableCheckFinished(false)` itself and returns
   false, on which its caller runs `fail_cable_check`, which emits a second
   `CableCheckFinished(false)` at `:2571` for the same abort. The vehicle is told the
   check failed twice for one attempt. The other branch of the same helpers, the
   measurement timeout at `:2474-2479` and `:2503-2508`, does not emit, so only
   `fail_cable_check`'s single emission fires there: the count depends on **why** the wait
   ended, which no consumer can see. The port sends the verdict once per attempt. The once
   only guard is `Dc::report_failure` in `src/core/path/dc.rs`, which holds the `reported`
   flag of the `Abort` stage, and both routes into a reported abort go through it: the
   de-energized voltage reading and the abort's own bound. Pinned by the
   `cable_check_reporting` module in `src/core/path/dc.rs`, whose six tests each assert the
   whole recorded effect sequence of one attempt rather than one call's return, so a second
   emission anywhere in the attempt is visible; `a_measured_isolation_fault_is_a_fault_and_fails_once`,
   `a_stage_timeout_fails_once_however_the_cable_de_energizes`,
   `an_abort_whose_cable_never_de_energizes_still_fails_once` and
   `an_abort_mid_wait_followed_by_that_waits_bound_still_fails_once` are the four that
   would fail against the C++ behavior.

   A second, smaller divergence rides on the same guard and is the reason it needed a
   bound. The C++ always sends the verdict, `:2568-2571` logging that the voltage did not
   drop in time and sending it anyway. Before this change the port reported a failed
   attempt only from a voltage reading below the safe threshold, so an attempt whose supply
   also stopped reporting reported nothing at all and the vehicle waited out its own timer.
   `Dc::enter_abort` is now the single entry into `Abort` and arms the same ten second
   bound every other stage wait gets; `to_safe_state` and `fail` both go through it, so
   neither route can reach the stage without the bound.

10. **The vehicle to home schedule is held for the life of the process, as in the C++.**
    This was a divergence and has been withdrawn: the port now reproduces the C++
    lifetime exactly, including its consequence. `setup_v2h_mode` limits
    grid import to zero watts and house export to the supply's maximum import power, and
    installs the pair through `update_local_energy_limit` (`:1670-1675`), the only writer
    of `external_local_energy_limits`. Nothing clears it. `get_local_energy_limits`
    prefers the external schedule to the derived one on the sole test that either half of
    it is non-empty (`:2541`), so from the first V2H activation the import limit is zero
    watts for as long as the process runs. The other half of the mode does not last:
    `sae_bidi_active` is cleared at `:612`, inside the current demand finished callback,
    which brings the zero discharge rewrite at `:2554-2557` back on the moment the
    discharge ends. What the two leave behind is a node asking for zero import and zero
    export, so the next ordinary session is granted no budget at all and cannot charge
    until the process is restarted. The port previously released the schedule on the same
    callback that clears `sae_bidi_active`, which removed that consequence but was a
    behaviour the C++ module does not have. Under "match EvseManager, not improve on it"
    the release was removed, so a V2H activation now strands the node exactly as the C++
    does. Pinned by `the_v2h_schedule_outlives_the_discharge_that_installed_it`,
    `active_v2h_forbids_grid_import_and_allows_house_export` and
    `a_v2g_port_keeps_its_ordinary_import_budget` in `src/core/energy/enforce.rs`; the
    first installs the schedule and then feeds the tree every later input it still
    accepts, so a reintroduced release path on any of them fails it. Only reachable on a
    `sae_j2847_2_bpt_mode` of `V2H`, which is the branch at `:952-953`.

11. **A random delay maximum of zero or less is a zero length delay rather than
    undefined behavior.** `energy_grid/energyImpl.cpp:474` draws
    `std::rand() % mod->random_delay_max_duration.load().count()`. The maximum is a module
    member the bus writes: `uk_random_delay`'s `set_duration_s` takes a plain integer
    (`interfaces/uk_random_delay.yaml`) and `uk_random_delayImpl.cpp:29-31` stores it
    unchecked, so a caller can set zero and the next trigger divides by zero. A negative
    maximum is accepted too, and because a non negative left operand modulo a negative
    divisor is non negative, `set_duration_s(-5)` silently becomes a delay of nought to
    four seconds rather than an error. Here both become a delay of zero seconds, which is
    the outcome a maximum of one already produces in the C++: the delay starts, withholds
    the request for exactly the call that drew it, and reports itself elapsed. The command
    is still accepted as sent, because refusing it would leave the caller believing a
    maximum the port does not have; the guard is at the draw
    (`RandomDelay::draw` in `src/core/energy/random_delay.rs`). Pinned by
    `an_out_of_range_maximum_gives_a_zero_length_delay_and_no_panic`, which drives 0, 1,
    -5 and `i64::MIN`.

12. **The random delay seed carries the node id.** `energyImpl.cpp:35` calls
    `std::srand((unsigned)time(0))` in `init`. That seed has one second of resolution and
    nothing per station in it, so two ports whose processes come up inside the same wall
    clock second draw the same sequence of delays. Synchronized load steps across a fleet
    are the exact failure the UK regulation exists to prevent, so a shared seed defeats
    the feature at the moment it matters most, which is a station wide or fleet wide
    restart. `boundary::random::seed` draws the seed from `/dev/urandom` and, if the device
    cannot be read, mixes the boot clock, the process id and the node id, so the fallback
    is still no worse than the C++ happy path. `core` keeps its own splitmix64 generator so
    a fixed seed makes the drawn lengths testable; see
    `the_seed_fixes_the_sequence_and_two_seeds_differ` and
    `a_drawn_delay_is_bounded_by_the_maximum` in `src/core/energy/random_delay.rs`.

13. **An autocharge token with no vehicle identity is not offered.** The external
    identification handler publishes `autocharge_token` unguarded when
    `config.enable_autocharge` is set (`EvseManager.cpp:1026-1028`), and that member is
    filled only from the stack's own `evcc_id` report (`:1034-1036`). With
    `autocharge_use_slac_instead_of_hlc` set, the C++ installs the SLAC subscription
    (`:180-184`) instead and never installs the `evcc_id` one, so the member stays value
    initialized and the handler offers the authorization framework a token with an empty
    identity string, a `MacAddress` token type nobody chose, and the `Autocharge`
    authorization type. A validator is then asked to decide on an identity that names no
    vehicle. This port offers nothing and logs the gap instead, in
    `Authz::on_require_eim` (`src/core/hlc/authz.rs`). Pinned by
    `the_slac_identity_is_not_remembered_for_the_eim_handler` in `src/core/hlc/mod.rs`,
    which drives the SLAC identity in and asserts the external identification handler
    still offers nothing. The SLAC arm itself is unaffected: it publishes a fully formed
    token at the moment the MAC address arrives, which is the C++ behavior.

14. **Clearing the persisted session removes the key instead of writing an empty
    string.** `PersistentStore::clear_session` (`PersistentStore.cpp:24-28`) stores the
    empty string. Its only reader, `get_session` (`:30-42`), answers `{}` for a missing
    key and for an empty value alike, and both call sites test emptiness
    (`EvseManager.cpp:1505`, `Charger.cpp:1556`), so the two are indistinguishable to
    every consumer. `SessionStore::clear` in `src/core/persist.rs` emits
    `Effect::PersistDelete` and leaves no record behind that means "no record".

    Checked against both `kvs` implementations in tree rather than assumed, because a
    clear now runs on a key that may never have been written (a metering start refused
    with `fail_on_powermeter_errors` set stores nothing, and the session end that follows
    it still clears). Deleting an absent key is a no-op in both: the sqlite backing does
    `DELETE FROM KVS WHERE KEY = @key`, which reports `SQLITE_DONE` with no row
    (`modules/Misc/PersistentStore/main/kvsImpl.cpp:225-237`), and the in-memory one is
    `kvs.erase(key)` (`modules/Misc/Store/main/kvsImpl.cpp:26-28`). Both also answer a
    missing key with the null variant, which is what the startup read maps to "no
    record". Pinned by
    `an_empty_recovered_uuid_is_no_record` in `src/core/persist.rs`, which holds the
    empty string and `None` to the same answer, and by
    `an_unreadable_record_boots_as_though_there_were_none` in `src/core/mod.rs`.

15. **Switching the power supply off reports `ChargingPhase::Other` rather than the phase
    that was running.** `powersupply_DC_off` issues
    `call_setMode(Off, power_supply_DC_charging_phase)` and only then returns that member
    to `Other`, so an off issued during a cable check reports `CableCheck` and one issued
    during a charge reports `Charging`. This port reports `Other` on every off.
    `types/power_supply_DC.yaml` defines `Other` as "switching it off or any other
    internal testing not related to real charging", so `Other` is the value the interface
    names for exactly this call and the C++ is reporting a phase its own type definition
    assigns to something else. The deviation is real and deliberate: a driver that keys on
    the phase during an off sees a different value here than from the C++. It is taken
    because the written contract and the C++ disagree and the contract is what a driver
    author reads. The three openings that begin a phase do carry it, which is the parity
    fix this sits beside rather than a divergence. Pinned by `switching_off_reports_no_phase`
    in `src/main.rs`, with `every_phase_maps_to_its_own_counterpart` beside it for the
    phases that are carried.

16. **A pilot detour that returns to the state it left does not re-enter it, so that
    state's session event is published once rather than twice.** `Charger`'s `T_step_EF`
    exit assigns `t_step_EF_return_state`, and where that is the state the detour started
    from, which is what `Charger::request_error_sequence` sets it to, the assignment is
    still a state change: the state runs its `initialize_state` block again on the next
    pass and signals `AuthRequired` or `PrepareCharging` a second time. This port models
    the detour as a sub state of the state it detours from, for the reason *The pilot
    detour and phase switching transitions* gives, and `core::path::duties_for_edge`
    returns early on a self edge, so no second announcement is reachable.

    What that entry does besides announcing is ported at the return rather than dropped.
    `AcHlc::returned_pilot_level` signals the level the returning state owes and not only
    the level the `T_step_EF` exit restores, which is what puts the nominal duty back on a
    session that is not on the five percent offer; and the authorization loop runs behind
    it, which re-arms the nominal fallback deadline the detour cancelled and acts on an
    authorization that arrived while the pilot was down. Pinned by
    `the_slac_error_routine_returns_prepare_charging_to_its_nominal_duty` and
    `the_slac_error_routine_leaves_the_state_it_returns_to_running_again` in
    `src/core/path/ac.rs`, both of which also assert that the session publishes nothing
    across the detour.

17. **A metering start the billing meter refuses still announces the transaction.**
    `Charger::start_transaction` returns false from inside its meter loop when a billing
    meter answers `UNEXPECTED_ERROR` and `fail_on_powermeter_errors` is set, so the C++
    reaches neither `store_session` nor `signal_transaction_started_event`: nothing is
    persisted and nothing is announced. This port allocates the metering start, announces
    `TransactionStarted` behind it on the same publish lane, and answers the verdict
    afterwards in `Core::answer_transaction_start`, which is where the error is raised and
    the port taken out of service.

    Said plainly: on a port with `fail_on_powermeter_errors` set, a refused meter means
    the CSMS is told a transaction started that the C++ would never have opened, and the
    port then goes inoperative behind that announcement. The divergence stands because
    neither way out is available. Announcing after the verdict inverts the order 73
    existing assertions were written against, which makes every one of them a judgement
    call rather than an edit. Deferring the announcement until the verdict arrives invents
    a requested but not open transaction the C++ cannot have, because its meter call
    blocks inside `start_transaction`, and that state would then owe its own answers about
    an unplug, a stop and a shutdown reaching it. Recorded here and at the announcement
    itself in `Core::start_transaction`, where the ordering it sits behind is explained.

18. **A pause for want of energy alone does not resume by itself.**
    `Charger`'s `ChargingPausedEVSE` body rebuilds the reason set on every pass and
    treats an empty one as a resume: it wakes SLAC and moves to `PrepareCharging`
    (`Charger.cpp:1019-1024`). This port's reducer resumes on the request that empties
    the set rather than on a pass that notices it is empty, so a session held only for
    want of a budget waits for a resume request after the budget returns.

    The reason set itself is ported, with the change detection the C++ has: the
    announcement is raised again whenever the set changes while the state is resident
    (`Core::reassess_pause_reasons`, run at the pass boundary because that is where the
    C++'s next loop pass is). So a consumer is told when the set narrows to the
    operator's pause alone; nothing acts on it narrowing to nothing. Pinned by
    `a_pause_is_announced_again_only_when_the_set_it_named_changes` in `src/core/mod.rs`.

    The set's third value is a separate matter and not a divergence at all.
    `Charger.cpp:1007` pushes `Error` when `stop_charging_on_fatal_error_internal`
    answers true, and that predicate polls `shared_context.shutdown_type`, which this
    module does not hold: a charging preventing fault reaches `FaultSignal` here and
    the path leaves the paused state in the same pass, so no set this port can build
    has a fatal error standing behind it. `PauseReason` has no variant for it and
    `a_fatal_fault_leaves_the_paused_state_rather_than_naming_a_reason` is what says
    the reason is unreachable rather than unported.

### The startup wait for the billing meter's first reading

`EvseManager::ready` blocks on `powermeter_cv.wait_for` for
`initial_meter_value_timeout_ms` and everything below that point waits for it: the resume
announcement, `cleanup_transactions_on_startup`, the waiting report and the ready report.
The setting's own description says so, in both manifests: it "defines for how long the
EvseManager waits for an initial meter value from a powermeter before it becomes ready to
start charging".

The port cannot block a thread, so the wait is a deadline and the remainder of the
sequence is a function, `Core::finish_startup`, with three callers: the boot that found a
reading already in hand, the reading that ends the wait, and
`TIMER_INITIAL_METER_VALUE` expiring instead. `Core::startup_held` is the parked thread
as a fact, and it is what keeps the three from running the sequence twice.

One departure from the C++ order, deliberate. `Core::on_startup` asks the energy manager
for a budget *before* the wait rather than behind it. The C++ ask is in
`energyImpl::ready`, a different implementation's `ready`, so nothing orders it against
`EvseManager::ready`'s wait and `ld-ev` decides which runs first; asking first keeps a
port whose meter never reports from being absent from the energy tree for the whole
timeout. Pinned by `the_energy_tree_is_asked_before_the_wait`.

What the wait buys is worth stating, because the port shipped without it: the
`TransactionFinished` that closes a record recovered from a power loss is rendered from
the boundary's latest meter reading, so a restart that announced the recovery before the
meter had said anything told the CSMS the interrupted session drew zero energy.

### The startup recovery order is an emission order, not an execution order

Not a behavioral divergence, and recorded because the C++ ordering is load-bearing and
the mechanism that preserves it here is different.

`Charger::cleanup_transactions_on_startup` clears the persisted record at
`Charger.cpp:1558` before asking the meter to close it in the loop at `:1561`, and it is
synchronous, so the clear has landed before the meter call starts. That order is
deliberate: a recovery that died on the meter call would otherwise find the same record
on the next boot and retry it forever.

`Core::recover_interrupted_transaction` emits the two in that order, and
`the_record_is_removed_before_the_meter_is_asked_to_close_it` pins it, but the two run on
different lanes: `Effect::PersistDelete` is `ExecContext::Store` and
`Effect::StopTransaction` has been `ExecContext::Publish` since the metering commands
moved there. Two lanes keep no emission order between them, so the clear is emitted
first and may still execute second.

This is safe rather than merely tolerated, and for a stronger reason than the pool being
usually fast. Recovery is idempotent: the record names a transaction, closing a
transaction the meter no longer holds is a warning the C++ already ignores
(`Charger.cpp:1564-1566`), and a second boot that finds the record again repeats exactly
the same four steps. The failure the C++ ordering prevents is an unbounded retry, and the
port cannot reach it for a different reason: `take_recovered` consumes the record from the
core's own state, so no single boot can recover twice whatever the pool does with the
write. Moving these two onto the publish thread to buy an ordering nothing needs would
put a blocking store call in front of the session event stream.

### The random delay trigger is a function, not a predicate with a hidden write

Not a behavioral divergence, and recorded here because a reader comparing the two files
will notice the shape changed and needs to know the behavior did not.

`energyImpl::random_delay_needed(float last_limit, float limit)`
(`energy_grid/energyImpl.cpp:369-393`) reads as a query and is not one. Its third branch,
the one that detects the module coming up with a vehicle already attached, assigns the
member `last_enforced_limit = 0.` before returning true, while the value it compares
against is the by-value parameter `last_limit`. The write has exactly one reader: four
lines after the call, `:477` sets `limit_when_random_delay_started = last_enforced_limit`,
which is the limit the delay will hold. `:513` then overwrites the member unconditionally
and there is no early return between the two, so the write is otherwise dead.

Its whole meaning is therefore: *a delay that fires because of the startup detector holds
zero, and every other trigger holds the limit the port last enforced.* Three orderings
make that easy to get wrong, and all three are load bearing: the branch is unreachable
when either limit change branch already returned true (short circuit, not an else), when
a delay is already running (`:471` guards the call on `not running`), and when the
feature is disabled (`:460` wraps the block).

The port returns the decision instead. `Trigger` in `src/core/energy/random_delay.rs`
names which of the two limits a due delay holds and `hold_for` turns the name into the
figure, so nothing writes through a query and the choice is stated where it is made.
Pinned from both sides:
`a_restart_with_a_vehicle_attached_holds_zero_and_not_the_remembered_limit` fails if the
predicate is treated as pure, and
`a_limit_change_inside_the_startup_window_still_holds_the_previous_limit` fails if the
startup branch is allowed to overtake the short circuit.

### The phase switching break is a state, and one request it can lose

`Charger::switch_three_phases_while_charging` (`Charger.cpp:1599-1620`) has four outcomes
and all four are ported. Two refuse and change nothing: a high level communication session
(`:1603-1605`) and a board whose capabilities say it cannot switch
(`energyImpl.cpp:434-439`). The other two accept, and `energyImpl.cpp:425-428` records the
new count for both, so what this node republishes follows the request from that pass on
whether or not the relays have moved yet. Which of the two applies is
`energy::enforce::PhaseSwitch`, decided where the charger state is already in hand.

`PhaseSwitch::Direct` is the direct board call every state but two takes (`:1617`).
`PhaseSwitch::ThroughBreak` is the pilot break, and it is `AcState::SwitchPhases` here
rather than a sub state, unlike the pilot detour: the entry announces
`SessionEvent::SwitchingPhases`, drops the pilot to the configured `switch_3ph1ph_cp_state`
so the vehicle stops drawing, holds `switch_3ph1ph_delay_s`, and only then moves the relays
and hands the session back to `PrepareCharging`, which re-derives the offer. Both
configuration keys are read there and nowhere else.

Two things the state buys that a sub state would not. Every consumer that reads the charger
state answers for the break by its own exhaustive match rather than by inheriting the
answer of the state it interrupts: it reports as `Charging` to the energy manager
(`energyImpl.cpp:185-187`), asks that manager for no energy, permits no random delay, is
not PWM eligible, and runs no soft overcurrent check, which matters because the offer is
withdrawn for the whole break and a check that ran would measure every residual ampere
against the noise floor. And the relay switch cannot be lost: `Iec::handle` moves the
pending count to the board on any route that leaves the break, which is
`Charger.cpp:200-207`, so an unplug or a fault mid break leaves the relays where the energy
manager has already been told they are.

Two things the break leaves on the board, both of them inherited. The relay switch at the
end of it is unconditional in the C++ too (`:612`), so a vehicle that ignored the withdrawn
offer entirely and never opened S2 has its relays moved with the contactor still closed;
`manifest.yaml` assigns that responsibility to the `evse_board_support` implementation in
as many words, and warns that some vehicles can be destroyed by it. And the enforced
current limit is derived from the new phase count from the accepting pass onward, before
the relays have moved, because `energyImpl.cpp:425-428` records the count whenever the call
returns true. So for the length of the break the board's hardware overcurrent limit is the
one that belongs to the count it is about to be on rather than the one it is on. Neither is
reachable as a hazard while the pilot is down, and both are exactly what the C++ does.

Both vehicle behaviours through the break reach `Charging`, by different routes. A vehicle
that answers the withdrawn offer opens S2, the board reports the relays open, and neither
fact ends the break early: `car_requested_stop_power` is gated on `charging_session_live`,
which deliberately excludes `SwitchPhases`, exactly as the C++ `SwitchPhases` case reads no
control pilot event at all. That vehicle then asks for power itself once the offer is back.

A vehicle that ignores the offer and holds state C never opens S2, so it produces no state
C edge afterwards and its contactor never opened. `Charger.cpp:765-768` charges such a
vehicle again from the `PrepareCharging` resident pass without any vehicle edge, by reading
`iec_allow_close_contactor`; the break's exit here does the same by reading the closed
contactor, which is the same reading `Iec::resume_requested` already uses for the identical
case after a pause. Without it the session settled in `PrepareCharging` with the contactor
closed, the offer restored and the vehicle still drawing, which is a session physically
charging under a state that runs no soft overcurrent check and announced no
`ChargingStarted`. `Iec::grant_power` is the single writer of `AcState::Charging` and both
routes go through it, so the two cannot drift apart.

The residual is a race rather than a strand. The C++ flag is control pilot derived and the
contactor here is board reported, so the two disagree for as long as the board takes to
report a contactor it has just been asked to open. A vehicle that opens S2 in the last
moments of a break can therefore be read as still drawing and charged again while it sits
in state B. No current flows in that window, the next control pilot event corrects it, and
closing it means carrying `iec_allow_close_contactor` itself. Recorded at
`Iec::switch_phases_delay_expired`.

One request the port loses. `Charger::pause_charging` (`Charger.cpp:1405-1411`) sets
`flag_paused_by_evse` and tests no state, so a pause arriving during a break is remembered
and acted on once the session is charging again. This port turns that command into a state
change at once and drops it from any state it does not consider a live charging session,
which the break now joins. That is the port's existing flag to state collapse rather than
anything the break introduces: a pause from `Idle` or `WaitingForAuthentication` is already
dropped the same way. Closing it means carrying the pause as a flag again, which is its own
change.

### Soft overcurrent detection is driven by the meter record, not by a 100 ms tick

`Charger::check_soft_over_current` (`Charger.cpp:2076-2110`) runs from the state machine
tick, every `MAINLOOP_UPDATE_RATE` of 100 ms (`Charger.hpp:498`), in `EvseState::Charging`
and `EvseState::ChargingPausedEV` on AC only (`:892`, `:928`). `core::soft_oc` runs the
same evaluation from the meter record that feeds it, with one timer per crossing standing
in for the tick so that the deadline still trips when the meter falls silent. Three
consequences, all named here because none of them is visible from the Rust alone.

- **The crossing latch and its timestamp are one `Option<Instant>`.** The C++ keeps
  `internal_context.over_current` and `last_over_current_event` apart and deliberately
  does not reset the timestamp when the flag clears. That is equivalent: the timestamp is
  written only on the edge into a crossing and read only under the flag. One field cannot
  hold a stamp belonging to a crossing already over.
- **The signalled current has no five second lag here.**
  `Charger::get_max_current_signalled_to_ev_internal` (`:2059-2066`) reads
  `internal_context.pwm_set_last_ampere` for basic charging because
  `update_pwm_max_every_5seconds_ampere` (`:1256-1266`) may defer a duty cycle change for
  up to five seconds per IEC 61851-1, so the figure signalled can lag the stored limit.
  This port applies a limit change to the pilot immediately, so there is no second figure
  to track and `Iec::signalled_current_a` answers with the limit while a nominal offer
  stands. What is modelled is the zero: `cp_state_X1` (`:1302`) and `cp_state_F` (`:1311`)
  both zero the field, and a five percent duty is set through the overload that takes a
  duty rather than an ampere and never writes it, so an offer that is not a nominal
  current signals no current at all. `AcHlc` takes the second branch once the vehicle has
  taken the session over, where the negotiated limit is what it was told regardless of the
  duty on the pilot.
- **One wake-up is armed per crossing, and one that expires outside the two states is not
  re-armed.** The gate is re-read when the deadline arrives, so a deadline delivered while
  the port is in `ChargingPausedEVSE` discharges nothing, exactly as the C++ simply does
  not call the function there; the crossing itself survives, so the first sample after the
  offer returns trips at once rather than restarting the window. The residual gap is a
  crossing that begins, leaves the two states, and returns to them with the meter
  permanently silent: it has no event left to trip it where the C++ tick would. Recorded
  at `Core::check_soft_over_current`.

Soft overcurrent detection is built for AC alone and a DC core holds no detector at all,
which is the structural form of the C++ gate: the DC branch of the charging state takes
the enforce target route (`Charger.cpp:879-890`) and never calls the check. The three
`soft_over_current_*` configuration keys are read here and nowhere else.

Nothing in this connects to `Effect::SetOvercurrentLimit`. That is the board's hardware
limit, travels on the current limit rather than on a measurement, and
`a_soft_crossing_does_not_touch_the_hardware_overcurrent_limit` in `src/core/mod.rs` pins
that a soft crossing leaves it alone.

### The `ac_with_soc` refresh deadline is a real hour, not a counter

`Charger::setup` seeds `shared_context.ac_with_soc_timer` with `3600000`, which reads
as one hour of milliseconds and is plainly what was meant. What the C++ then does with
it is subtract `50` once per pass of `Charger::main_thread`, whose
`MAINLOOP_UPDATE_RATE` is **100 ms** and whose queue wait returns early whenever a
board support event arrives. So the realized period is about two hours on an idle
port and shrinks towards one hour and below as event traffic rises: it is a function
of how busy the port is, not of time. This is a C++ defect the port does not
reproduce; see `core/path/ac_with_soc.rs` for what it uses instead.

`ac_with_soc::DC_REFRESH_AFTER` is one real hour. It is the value the constant names,
it is the value the manifest description implies, and it cannot drift with load. This
is a divergence rather than a preserved defect because the fix is local and nothing
observable depends on the C++ figure: the deadline exists so a long AC session
eventually collects a fresh state of charge, and neither an hour nor two is a
correctness boundary.

The deadline is armed on the flip to AC and cancelled on the flip to DC, which is
what `Charger::setup`'s `_ac_with_soc_timeout` argument does: false from
`setup_fake_DC_mode`, true from `setup_AC_mode`, so the countdown exists exactly
while AC is presented.

### A transaction start with the record already open moves the state

`Charger::run_state_machine`'s `WaitingForAuthentication` external authorization arm and the plug and charge arm beside it guard only the record
opening on `not flag_transaction_active`, and then assign `current_state` regardless.
This port refused the whole `IecInput::TransactionStarted` on an open record, which
left every route that re-enters `WaitingForAuthentication` mid transaction stuck in
it: the data link error restart, which `session_restart` reaches with the record
open, and the reinitialization, which exists precisely to keep it open. Corrected
here, with the two guards separated the way the C++ has them.

Found by the `ac_with_soc` flip, which cannot work without it, and it was a live
defect on the data link error route before that:
`a_data_link_error_on_a_running_five_percent_offer_restarts_the_session` asserted the
session sitting in `WaitingForAuthentication`, and it now asserts the
`PrepareCharging` the C++ reaches on the same pass.

### Five boolean commands answer before the core has decided

`evse_manager` declares seven commands that return a boolean. Five of them are
answered by the boundary at the moment the command arrives, before the core has
seen it, because the intake is non-blocking: the handler pushes an `Event` onto
the writer queue and returns. `reserve`, `pause_charging`, `resume_charging` and
`stop_transaction` answer `Ok(true)` unconditionally (`src/main.rs`), and
`enable_disable` answers the *requested* state rather than the arbitrated one,
under a comment that says so.

The two that answer honestly do it without consulting the core: `force_unlock`
reports whether a connector lock is wired, and `external_ready_to_start_charging`
reports a configuration value.

The C++ answers from the decision. `evse_managerImpl::handle_reserve` returns
what `Charger::reserve` decided, and `handle_stop_transaction` returns what
`cancel_transaction` decided. So a reservation this module declined and a remote
stop on an idle connector are both reported to the caller as having succeeded,
and an `enable_disable` that loses arbitration reports the state it asked for.
This is a port defect, not a preserved C++ one, and it is one of the two things
recorded in the commit message as blocking a real charger.

Closing it needs a reply channel from the core back to the waiting command, which
is a boundary change rather than a core one: the effect that carries the decision
would have to name the caller to answer. No such channel exists today.

## Preserved C++ defects

Behavior the C++ gets wrong that this port reproduces anyway, because fixing it would
change what a vehicle is offered and that decision belongs in the C++ first. The
difference from Intentional divergences above is only that: each entry there is a defect
whose fix is safe and local, each entry here is one whose fix is not.

1. **Two of the three session setup derivations can offer the vehicle no payment option
   at all.** `session_setup` in `interfaces/ISO15118_charger.yaml` declares `minItems: 1`
   on the argument, so an empty list is not a representable request, and two arms produce
   one:

   - A session start whose reason is not `Authorized` (`EvseManager.cpp:1362-1367`) pushes
     `ExternalPayment` if `config.payment_enable_eim` and `Contract` if `pnc_enabled`, and
     has no fallback for the case where both are off. The other two trigger points do have
     one (`:369-372` and `:1329-1332`, which warn and push `ExternalPayment` anyway), so a
     deployment with both disabled is offered external payment at boot and at every session
     finish, and nothing at each session start.
   - An `Authorized` session event with `payment_enable_eim` off and `pnc_enabled` on
     (`:1317-1332`). The `Authorized` branch offers no contract by design, and the fallback
     is guarded on *both* settings being off, so it does not fire when plug and charge is
     the one that is on. This one is the more surprising of the two: the deployment enabled
     a payment option and the vehicle is offered none.

   Ported unchanged and pinned by
   `every_combination_of_the_two_settings_is_pinned_at_every_trigger_point` in
   `src/core/hlc/session.rs`, a full five-by-four table that names both empty rows
   explicitly, plus
   `a_start_from_a_plug_in_can_offer_nothing_where_both_options_are_disabled` and
   `an_authorized_event_with_external_payment_disabled_offers_nothing`. All three assert
   the emptiness rather than a fallback, so the day the C++ grows one the tests are what
   fail.

2. **External derating can introduce an import limit the power supply never named.**
   `EvseManager::apply_external_derating`, which `get_powersupply_capabilities` calls,
   narrows four capability fields through `min_optional`, but the export pair is required on
   `types::power_supply_DC::Capabilities` and the import pair is optional, so the four
   calls reach two different overloads. The optional one treats an absent capability as
   "no cap" and lets the present operand win (`:57-68`), so a supply that reported no
   import ceiling at all comes back with the derate's value **as** its ceiling, where the
   export direction can only ever be lowered (`:70-77`). An external source can therefore
   hand a unidirectional supply a discharge limit, and the figure reaches the vehicle
   through the EVSE maximum limit set. Reproduced rather than corrected, because the
   alternative reading, clamping an absent capability to itself, would silently drop the
   only import cap a bidirectional site had, and choosing between the two is a decision
   for the C++. Pinned by
   `a_derate_introduces_an_import_limit_the_supply_never_named` in `src/core/derate.rs`
   and `a_derate_introduces_a_discharge_limit_the_supply_never_named` in
   `src/core/hlc/dc_limits.rs`.

   A mechanism note rides on the same code. The C++ stores the derate request untouched
   and applies it on every **read** of `get_powersupply_capabilities()`, so a derate
   emits nothing at all and reaches its readers whenever they next ask. This port emits
   nothing either. `Core::refresh_derated_capabilities` moves the one copy that is not
   re-read, the energy tree's, when the derated report actually moves, which a derate
   arriving, a derate being relaxed, and a present voltage that completes a half named
   request can each do.

   The power path is deliberately not told there. `energy::enforce` is the sole producer
   of the path's limit set and derives it from that same report
   (`energyImpl::handle_enforce_limits`, which now reads
   `EvseManager::get_powersupply_capabilities_for_hlc`),
   so the path learns the narrowed ceiling on the next enforced limits pass, exactly as
   the C++ charger clamp learns it on its next read. Pushing a set from the derate
   instead would have to name the supply's ceiling **without** the energy allowance
   applied, widening the DC clamp back to the supply maximum until the next pass, so the
   deferral is a correctness requirement and not a timing preference. Pinned by
   `a_derate_does_not_tell_the_path_by_itself`, which asserts both halves: the derate
   tells the path nothing, and the very next pass carries the narrowed figure.

   **The push model is ported, and this divergence is closed.** The paragraph that stood
   here described it and named what closing it would take, so here is the closure against
   that description. The forward to the stack is
   `apply_powermeter_limits(apply_external_derating(raw))`, which is
   `capabilities_for_hlc`; `last_hlc_capabilities` replaces the raw report as the
   forward's change gate, so a raw change the derate or a meter floor flattens is not
   re-sent and a derate change that moves nothing else is; a derate that changes the
   request pushes, which is `EvseManager::set_external_derating`
   (`EvseManager.cpp:2826-2838`); an unchanged report pushes nothing at all, which is the
   early return at `:2764-2767`; and the DC boot forwards the seed report, because the
   last sent report is absent there, which is the direct push at `:561-564`. One function
   emits all three messages, as one C++ function does, and its four call sites are the
   C++'s four. Pinned by the `DcLimits` tests named on each of those and by
   `a_derate_tells_the_vehicle_the_report_it_narrowed` in `core`.

   The **car side power meter's floors are in that forward** too, and they are applied to
   the same derated report. The two narrowings remain disjoint by field: derating touches
   only maxima and the meter floors only minima
   (`nothing_but_the_four_minimum_currents_moves`), which is why one helper can serve both
   the forward and the minimum limit set without telling the stack two different minima.
   A floor above a derated ceiling is therefore clamped at that ceiling in the announced
   report exactly as it is in the limit set the path clamps against, where before the
   announced minimum could stand above the announced maximum. Both halves are pinned, by
   `a_floor_above_a_derated_ceiling_is_clamped_at_that_ceiling` and
   `the_announced_floor_is_clamped_to_the_derated_maximum_too`.

   And the refresh is gated on the report having moved, so the present voltage
   measurement, which arrives several times a second, costs nothing while no derate
   stands. That gate is now a cost guard only: with nothing emitted, refreshing
   unconditionally would rewrite the energy tree with the value it already held and is not
   observable from the core's effects.

3. **An AC capability report while the fake DC mode is presented overwrites the DC
   announcement.** `EvseManager` keeps one `supported_energy_transfers` for both
   presented modes, `setup_fake_DC_mode` replaces it with the DC pair, and
   `recompute_and_publish_supported_ac_energy_transfers` replaces it again with the
   derived AC set under no mode guard. A board that publishes its capabilities during a
   fake DC session therefore tells the vehicle the port is AC after all. Reproduced,
   because the alternative is storing the fake set on `hlc::HlcPort` and that is what
   makes `charge_mode` mutable again; a fix belongs in the C++, where the mode is the
   thing that should gate the recomputation.

4. **A second state of charge while AC is presented reinitializes again.**
   `EvseManager::switch_AC_mode` calls `Charger::start_reinit` unconditionally and the
   only guard on the far side is `reinit_running`, which a finished sequence has
   cleared. So a vehicle that somehow keeps an ISO 15118 session alive past the flip has
   its control pilot broken again on every `DcEvStatus`. It costs nothing in practice,
   because the reinitialization is what tore that session down and a torn down session
   reports no further state of charge. Pinned rather than corrected: a port that
   swallowed the second report would leave such a vehicle charging as DC on AC hardware.

5. **`hlc_charging_active` survives the flip to AC.** `Charger::set_hlc_charging_active`
   only ever sets the flag and the `Idle` entry is the only thing that clears it, so
   nothing in the mode switch or the reinitialization lowers it. An AC presented session
   therefore still reports high level charging until the vehicle ends the ISO session or
   the port goes idle, which changes what `Core::ask_vehicle_to_stop` sends and makes the
   enforced limits handler refuse a phase change. In practice the vehicle clears it: the
   reinitialization asks it to stop, and the `stop_from_ev` that follows is a writer of
   the false. Reproduced, because clearing it on the flip would be a second writer of a
   flag whose single-writer discipline is what keeps the DC and AC answers apart.

6. **The high level communication failure latch reaches the duty cycle but not the
   contactor gate.** `PrepareCharging` reads `hlc_failed` beside
   `hlc_use_5percent_current_session` when it picks the duty cycle, and does not read it
   at all in the test just below that decides whether to enter `Charging`:
   `(iec_allow_close_contactor and not hlc_use_5percent_current_session) or (iec_allow and
   hlc_allow and hlc_use_5percent)`. So on the one route where the latch is set without an
   entry into `WaitingForAuthentication` to re-derive the offer - a `D-LINK_ERROR` arriving
   with the offer already off the pilot, which under `[V2G3-M07-04]` runs no pilot detour -
   the vehicle is offered the nominal duty cycle while the gate still waits for the high
   level communication half of a permission that the failed link will not grant. The
   session sits in `PrepareCharging` until the cable comes out. Reproduced, because the fix
   is to read the latch in the gate as well and that changes when relays close, which
   belongs in the C++ first. The offered duty cycle half is pinned by
   `a_link_error_with_the_offer_already_down_keeps_the_resume_on_nominal` in
   `src/core/path/ac.rs`, which asserts the resume comes back on nominal signalling and the
   gate is nonetheless still shut.

### The random delay restrains the AC pilot current and nothing else

The most consequential thing about the C++ implementation, and it is invisible from the
random delay code itself.

`handle_enforce_limits` withholds exactly one figure: the local `limit`, which is the
ampere limit after the watt to current conversion (`energy_grid/energyImpl.cpp:457-483`).
Three consumers read it: the cable rating cap at `:520`, `Charger::set_max_current` at
`:531-539`, and nothing else. Every DC leave side limit the vehicle is told is derived at
`:578-642` from `watt_leave_side`, which is `total_power_W` straight off the wire and is
never touched by the delay, and from the supply's capabilities. `ac_max_current_A` reaches
the DC block only through the change detector at `:563`, which decides whether to
recompute and takes part in no derivation.

So on a DC port a running random delay holds the control pilot current, which is not what
carries DC power, and the vehicle is offered the full new power immediately. The UK
obligation is met on AC and not on DC. Ported unchanged, because the fix is to withhold
`total_power_W` as well and that changes what the vehicle is told, which belongs in the
C++ first. Pinned by `a_dc_session_is_not_restrained_by_a_running_random_delay` in
`src/core/energy/enforce.rs`, which asserts the delay is running, the pilot current is
held at zero, and the vehicle's power limit is nonetheless the full requested figure.

### A held limit reaches the republished limits only through the cable rating

`energyImpl.cpp:516-523` rewrites `value.limits_root_side.ac_max_current_A` with
`std::min(limit, pp_rating)` and does so only inside `if (pp_rating)`. On a port whose
board reports no rating, which is what `IECStateMachine.cpp:482-487` returns for a zero
reading, the struct is republished carrying the figure the energy manager sent while the
charger is given the held one. Every consumer of `enforced_limits`, OCPP included, is
therefore told the port is already at the new limit for the whole delay. Preserved for the
same reason as the entry above: agreement here is a change to what an external consumer
is told. Pinned by `without_a_cable_rating_the_republished_limit_is_the_one_being_withheld`
in `src/core/energy/enforce.rs`.

### Two random delay oddities that follow from statement order

Both are consequences of where `energyImpl.cpp:463-467` sits, and neither is safe to
tidy: each one changes when a load step actually reaches the grid, which is the thing the
regulation constrains.

1. **An unsuitable charger state drops a running delay and can draw a new one in the same
   call.** The reset at `:463-467` clears `random_delay_running` whenever the state is not
   `PrepareCharging`, `Charging` or `WaitingForAuthentication`. It runs *before* the start
   decision at `:471`, and the two limit change branches of the trigger do not look at
   state at all, so an enforced limit that arrives in, say, `Idle` and also moves the limit
   clears the delay and immediately draws a fresh one. Pinned by
   `an_unsuitable_state_can_drop_a_delay_and_start_another_at_once` in
   `src/core/energy/random_delay.rs`.

2. **A cancel inside the startup window is redrawn at once.** `uk_random_delay`'s `cancel`
   documents its effect as "the same as if the time expired just now", and outside the
   startup window it is: the requested limit was already recorded at `:513` on the call
   that started the delay, so the next enforced limit finds no change and draws nothing.
   Inside the five second startup window the startup branch does not look at whether the
   limit moved, so it fires again and the port is held for a second interval. Time
   expiring would not have done that. Pinned by
   `a_cancel_inside_the_startup_window_is_redrawn_at_once` in `src/core/mod.rs`.

### The single bidirectional resolution carries AC_BPT

The C++ disjunction is not one expression repeated three times. Two of the three sites
name three terms and one names four:

```
:2366      (powersupply_DC_set)     hack_allow_bpt_with_iso2 or sae_bidi_active or session_is_iso_d20_dc_bpt()
:2405-2407 (powersupply_DC_set)     the same three, and last_is_actually_exporting_to_grid
:2555-2556 (get_local_energy_limits) the same three, or session_is_iso_d20_ac_bpt()
```

`session_is_iso_d20_ac_bpt()` (`:2710-2713`) appears at `:2555` and nowhere else, and
`get_local_energy_limits` branches on `config.charge_mode` internally at `:2542`, so that
site is shared by both charge modes and the fourth term is what lets an AC BPT session
export.

The port has **one** resolution and it carries the AC_BPT term, because `:2555` is the
site whose expression is complete. Collapsing to the three term form would be the lossy
direction. Two facts make the choice safe rather than a bet:

- **There is one resolution and both readers take it.** Two of the three sites are
  ported: the `powersupply_DC_set` direction choice, as `Dc::direction_mode` in
  `src/core/path/dc.rs`, which covers both `:2366` and `:2405` and runs on the DC path
  only, and `:2555`, as the `bidirectional` field of `core::energy::Publish`, which
  decides whether the energy flow request asks for an export budget at all. Both read
  `HlcPort::bidirectional`, so the four term form is the only form in the port and the
  two readers cannot disagree. The AC_BPT term is therefore live rather than dormant: an
  AC BPT session keeps its export budget, which is what `:2555` is for.
- **A DC port never advertises an AC service.** `supported_dc_transfer_modes` derives
  `DC_extended` and `DC_BPT`, or their MCS spellings, and never an AC mode, so an AC_BPT
  selection on a DC port is not a selection this EVSE offered. Pinned indirectly by
  `a_dc_port_advertises_nothing_bidirectional_until_the_supply_says_it_can` in
  `src/core/hlc/mod.rs`.

If the shared site is ever ported, it reads the same field and needs no second
resolution; and the day a fourth read site wants the three term form instead, it needs a
stated reason rather than a second expression.

### The three session setup derivations are kept apart, not collapsed

`EvseManager.cpp` derives the `call_session_setup` arguments at three sites, `:357-374`
at boot, `:1311-1334` on an `Authorized` or `SessionFinished` session event and
`:1348-1370` on a session start. The three read the same inputs and look like copies,
and they are not:

- The contract payment option is offered at boot whenever plug and charge is enabled,
  and on a session event only for `SessionFinished`. An `Authorized` event takes the
  `else` branch at `:1323-1327`, which also withdraws the certificate installation
  service, because the stack must not offer either to a session that is already
  authorized.
- That same `else` branch leaves `central_contract_validation_allowed` standing. The
  session start arm, for the `Authorized` start reason, clears it (`:1359`). So the two
  arms that both mean "already authorized" disagree about that one flag.
- The session start arm for an `Authorized` reason pushes `ExternalPayment`
  unconditionally, ignoring `config.payment_enable_eim`.
- Only boot and the session event arm have the both-options-disabled fallback, per the
  preserved defect above.

`src/core/hlc/session.rs` therefore keeps three match arms over a `Trigger` enum and
shares only the one line all three do agree on, the gating of the two contract flags on
`pnc_enabled`. The differences are pinned individually, most directly by
`an_authorized_start_clears_central_validation_where_an_authorized_event_does_not` and
`an_authorized_start_offers_external_payment_even_where_it_is_disabled`, so a later
collapse fails a test rather than passing quietly.

### Energy setpoints and the SAE V2H schedule

`EnergyTree` produces a setpoint after the first matching enforced limit in a
bidirectional session. AC uses the enforced amperes per phase and DC uses the enforced
total power; both retain their sign, so positive requests charge and negative requests
discharge as `types/energy.yaml:159-170` specifies. The C++ EvseManager has no writer of
`schedule_setpoints` (a repository-wide symbol search finds only readers and writers in
other modules), so this producer is a Rust addition, not a behavior divergence being
copied from C++.

The SAE V2H activation also installs the C++ `setup_v2h_mode` schedule
(`EvseManager.cpp:1651-1668`): grid import is limited to zero and house export is limited
to the supply's maximum import power. The rewrite lives directly on `EnergyTree` because
that C++ helper is the only remaining writer of EvseManager's external limit member; the
removed `set_external_limits` command leaves no general external input to preserve.

## Deliberately unported surface

Evidenced statements that something is not ported, with what the evidence is. This is
not a to-do list: each entry is a decision, and the reason it is safe is the point.

### The eight pass-through variables

`interfaces/evse_manager.yaml` declares eight variables that are not the session
event stream, the limits or the readiness flags. The C++ publishes all eight and this
port published **none** of them until now, which nothing in the module noticed: they
have no reader inside it, so no test, no reachability census and no compiler check had
anything to say. The OCPP suite found the group by measuring one of its consequences,
`MeterValues` on the wire falling from 478 to 12 and `TriggerMessage(meter_values)`
answered `Rejected`. That is why this entry exists even though all eight are now
carried: the group needs to be named somewhere, so that the next variable added to
that interface is a decision rather than an omission.

All eight are carried, and where each comes from:

| variable | carried from | route |
|---|---|---|
| `powermeter` | the billing meter's reading, whole | `Intake::on_powermeter` |
| `powermeter_public_key_ocmf` | the billing meter's key | `Intake::on_public_key_ocmf` |
| `hw_capabilities` | the board support capability record, whole | `Intake::on_capabilities` |
| `telemetry` | the board support telemetry record, whole | `Intake::on_telemetry` |
| `evse_id` | configuration, once | `Intake::on_ready` |
| `car_manufacturer` | the vehicle's own MAC address | `core::hlc::manufacturer`, `Effect::PublishCarManufacturer` |
| `ev_info` | what the vehicle said about itself, accumulated per session | `core::hlc::EvInfo`, `Effect::PublishEvInfo` |
| `selected_protocol` | five writers on one value | `core::protocol`, `Effect::PublishSelectedProtocol` |

The first five are republications and happen at the boundary, in the subscriber that
receives the value, because that is what they are: the module is handed a record and
puts it on its own interface unchanged, with no decision in between. Routing them
through `core` is not possible in any case - `core` is compiled without `everestrs`
(`src/lib.rs`) and cannot name a wire type - and inventing a transition to carry them
would make that transition the thing under test rather than the publish. The last three
are decisions and are in `core`: `car_manufacturer` is a total function of the address
the vehicle named itself with, `ev_info` is per session state the stack writes, and
`selected_protocol` is state with five writers.

None of the eight is observable to any test in this crate, because publishing needs a
`ModulePublisher` and that needs a live framework runtime. `main.rs`'s
`mod pass_through_variables` is the source ledger that closes the hole, in the same
shape and for the same reason as `mod session_event_payloads` and
`mod session_log_wiring`: nine rows pinning the exact expressions, every one of them a
place where reverting to the dropped body compiles and passes every other gate.

**`ev_info` is carried half filled, and that is the right shape.** It is the one of
the eight that is not a record this module was handed: `EvseManager` accumulates it
across twenty call sites from the vehicle's own reported figures, and this port has a
source for two of them, `evcc_id` (the stack's vehicle identity) and `soc`
(`HlcEvent::StateOfCharge`, on a DC port only, as in the C++).

This entry used to argue the opposite, and the argument was wrong in one specific
inference. It read: every one of the twenty six fields is optional on the wire, so a
record carrying two filled fields and twenty four absent ones tells a consumer exactly
what a vehicle that reported nothing tells it. It does not. Both consumers test
`has_value()` **per field** - `OCPP201::init_evse_subscriptions` reads `soc` and
`evcc_id` under separate guards - so a record with `evcc_id` present says strictly more
than no record at all. And no record at all is not "a vehicle that reported nothing": it
is a consumer whose `if` never runs. The C++ contract is per field optionality, and
replacing it with an all or nothing one left the port no way to say "this vehicle did
name itself".

It cost a measured test. `OCPP201` caches `ev_info.evcc_id` and stamps it onto the id
token of `TransactionEvent(Ended)` as `additionalInfo[{type: "EVCCID"}]`; with nothing
published the cache stayed empty and `ocpp21 bidirectional::test_q01` failed on that
stamp, in both parameterizations, deterministically. Supplying only this publication
flipped it four times out of four, and supplying a same shaped record without `evcc_id`
did not.

The reasoning above is kept rather than deleted because it will be tempting again for
the next half filled record on this list. The test to apply is whether the consumer
reads the record or its fields.

What a deployment still does not get: the AC seven and DC six vehicle figure callbacks
remain unported, so twenty four of the twenty six fields stay absent. Those are
features. The record is reset and republished empty at session start and session finish,
as the C++ does, so one vehicle's identity cannot outlive its session on the wire.

Three things make that classification checkable rather than a shrug, and they were
established rather than assumed.

**The record is not a control input here, although it is one in the C++.**
`EvseManager::process_dc_ev_target_voltage_current` reads `ev_info.maximum_current_limit`
and `ev_info.maximum_voltage_limit` back out to clamp the vehicle's target, so in the C++
`ev_info` is where two control facts live. This port keeps them where they clamp instead,
on the DC path, and `Dc::clamped_target` applies both with the same citations. So the
absent publication costs a consumer a reading and costs the charge nothing.

**Seven of the twenty four could be filled from facts this port already carries** - the
two present values, the two clamped targets, and the vehicle's maximum current, voltage
and power - and they are not, because every one of them would need a second home. The
facts live on `Dc` and on the enforced limit set; `EvInfo` lives on `HlcPort`. Copying
them across is the shape this port refuses elsewhere: one fact written in two places that
can disagree. The C++ has no such problem because its one `ev_info` is both the store and
the publication.

**And two of the seven would still not reach the wire.** `EvseManager.cpp:736-743` stores
`present_voltage` and `present_current` with the publish commented out and a comment
saying why: "dont publish ev_info here, it will be published when other values change.
otherwise we will create too much traffic on mqtt". A port that filled them would show
them only on whatever published next.

`selected_protocol` is now published after all thirteen announcements the C++ publishes
it after. The thirteenth was `ChargingFinished` (`Charger.cpp:1548`), which had no
`SessionEvent` variant here at all; it was recorded on this list as a missing protocol
publication point that was really a missing session event, and porting the event is what
closed it. `core::protocol::published_after` and `mod pass_through_variables` carry the
assertions.

### `plug_temperature_C` on the `dc_external_derate` interface

`interfaces/dc_external_derate.yaml` declares one variable beside the
`set_external_derating` command, `plug_temperature_C`, described there as the plug
temperature "exposed here for convenience" so that a derating module can read it without
wiring up the board support driver itself. `EvseManager` never publishes it: the string
`plug_temperature` does not appear anywhere under `modules/EVSE/EvseManager/`, so no
consumer of the C++ module has ever received a value on it. This port does not publish it
either.

Publishing it would mean forwarding the board support temperature report, which this
module receives but does not otherwise read, and inventing the mapping from that report's
sensor list to a single plug figure. That mapping is the decision the interface leaves to
whoever implements it, and making it here would put a number on the bus that the C++
never put there. The command half of the interface is fully ported; see
`Command::SetExternalDerating` and `src/core/derate.rs`.

### The transcript's external MQTT publication

`SessionLog::output` ends by handing every record to an MQTT functor
(`SessionLog.cpp:244-249`) that `EvseManager.cpp:139-142` wires to publish
`{origin, target, iso15118, msg}` on `everest_api/<module id>/var/hlc_log`. Not ported,
and not portable today: `lib/everest/framework/everestrs/everestrs/src/lib.rs` exposes
`publish_variable` on declared interfaces and nothing else, and external MQTT appears
there only as runtime command line arguments. A Rust module has no binding that can reach
an external topic, whatever the manifest's `enable_external_mqtt: true` says.

The port produces the bodies anyway. `SessionLogger::record` fills
`LogOutput::publications` with exactly the four fields, pinned by
`the_publication_carries_the_origin_target_protocol_and_message`, and
`EverestEffects::session_log` binds them to `_unpublished`. Wiring is one call at that
site once everestrs gains the binding. Consequence for a deployment: a consumer watching
that topic on the C++ module sees nothing from this one, and the transcript files and the
EVerest log are the only outputs.

### Most of the C++'s transcript call sites

The transcript subsystem is fully ported and four of the C++'s roughly fifty
`session_log` call sites are wired to it: the session bracket
(`evse/evse_managerImpl.cpp:182` and `:336`, which are also the two that start and stop
the log), the state transition line (`Charger.cpp:175`), and the ISO 15118 message feed
(`EvseManager.cpp:1919`, the only payload bearing site in the module). Those four are the
spine of a transcript: when the session opened, how the state machine progressed, every
protocol message with its payload, and when it closed.

The rest are not wired, and they are annotations rather than structure: about forty
single-purpose lines inside `Charger.cpp` (`EIM Authorization received`,
`PnC Authorization received`, the `Enter`/`Exit T_step_EF` and `T_step_X1` pairs,
`Start`/`Exit switching phases`, the `Stop in <state>: <reasons>` family, `Set PWM Off`,
`Set PWM F`, the BCB toggle), plus `EvseManager.cpp`'s D-LINK relays (`:384`, `:398`,
`:405`), the AC HLC contactor pair (`:413`, `:418`), the isolation monitor self test
result (`:701`), the SAE mode lines (`:954`, `:956`), the payment options (`:984`), the
SLAC state (`:1237`), `D-LINK_READY` (`:1259`), and `energy_grid/energyImpl.cpp:676`.
A transcript here is therefore sparser than a C++ one but not differently shaped: every
line it does write is byte comparable.

One of those groups is structurally blocked rather than merely unwired. `Charger.cpp:1144`
and `:1147` write `Event <cpevent>` for the derived `CPEvent` vocabulary
(`IECStateMachine.hpp:41-51`), which this port does not hold. The port maps one raw pilot
reading to one `IecInput` (`path::ac::cp_to_input`) and derives four of that vocabulary in
`CpTracker` (`event::CpEdges`): the arrival, the departure and the two `BCD` edges, which
are the four `EvseManager.cpp:1094-1119` forwards to SLAC. The C++ derives a QUEUE of
`CPEvent`s from one reading inside `IECStateMachine::state_machine`, so a single B reading
arriving from F emits `CarRequestedStopPower`, `CarPluggedIn` and `EFtoBCD` as three
separate lines. Writing those lines faithfully means porting the rest of that queue, with
its `ev_simplified_mode`, `pwm_running` and `relais_on` inputs, which is a state machine
port and not a logging change. Recorded here rather than approximated, because a line spelled from the port's own
vocabulary would look like the C++'s and not be it.

### The pilot detour and phase switching transitions

`Charger::evse_state_to_string` spells twelve states and `session_log::charger_state_name`
spells eleven. `T_step_EF` and `T_step_X1` have no `AcState`: the port models the pilot
detour as a sub state of the state it detours from (`path::ac::Detour`). So a transcript
here carries no `Charger state: Charging->T_step_EF` line, and the transitions the C++
writes around one collapse into a single edge. `SwitchPhases` is not in that group: it is a
real `AcState`, spelled as the C++ spells it, because it is a state a session rests in for
a configured number of seconds rather than a detour a timer runs, and because it owes a
session event of its own. `AcState::Startup` is the reverse case, a port state the C++ has
no name for; it is spelled `Startup` so that a transition cannot silently disappear, and it
is unreachable from a transcript because the port leaves it long before any session opens
one.

### The externally set energy limits, and with them the SAE V2H rewrite

`EvseManager::get_local_energy_limits` (`EvseManager.cpp:2535-2563`) is ported, as
`EnergyTree::local_energy_limits` in `src/core/energy/mod.rs`, and the SAE V2H rewrite
is ported with it. It arrives by a different route than the C++ takes: rather than
`setup_v2h_mode` (`:1651-1668`) writing `external_local_energy_limits` through
`update_local_energy_limit` (`:1670-1675`), the port holds the mode on `EnergyTree`
itself and applies the rewrite in `local_energy_limits` (`src/core/energy/mod.rs`, the
`config.sae_v2h` branch). The observable lifetime is the C++'s, including the strand it
leaves behind; see the withdrawn divergence 10 above, which is pinned by three tests in
`src/core/energy/enforce.rs`. What is genuinely absent is any *other* writer of external
limits, and there is none left to port: `set_external_limits` is no longer a command on
`interfaces/evse_manager.yaml`.

That gap is narrower than it looks: `set_external_limits` is no longer a command on
`interfaces/evse_manager.yaml`, so `setup_v2h_mode` is the only writer of external limits
left in the C++ module. The zero discharge step that follows the branch (`:2554-2558`) is
ported and runs on both.

The consequence for the SAE bidirectional handler is specific. `EvseManager.cpp:949-960`
does three things and only the first is ported: it raises `sae_bidi_active`, which is a
bidirectional source and has a reader here; it writes a session log line; and for the
V2H mode alone (`:952-953`) it calls `setup_v2h_mode`, which sets the export schedule's
`total_power_W` to `powersupply_capabilities.max_import_power_W` and forces the import
schedule to zero, so grid charging stops and discharge to the house load is allowed up
to the supply's import capability. That rewrite is not ported. A V2H deployment
therefore gets the direction half of the mode, because `sae_bidi_active` resolves the
session bidirectional, and not the limits half, so nothing stops it importing from the
grid at the same time.

This is a stated gap rather than a decision to behave differently. The `sae_j2847_2_bpt_mode`
setting is already parsed and already reaches the stack as `call_setup`'s `sae_mode`
(`HlcConfig::sae_mode`), so the V2H arm has its input in hand, and the builder it would
feed now exists. What it still lacks is an external limit input on `EnergyTree` whose
schedules replace the derived ones; `core::energy::flow_request::Schedule` carries
entries beyond the first for exactly that.

### `update_supported_app_protocols`

The command exists on `interfaces/ISO15118_charger.yaml:143` and no module originates a
call to it. Repository wide, `call_update_supported_app_protocols` appears at exactly four
sites, all inside `modules/EVSE/IsoMux/charger/ISO15118_chargerImpl.cpp` (`:660`, `:661`,
`:695`, `:698`), and all four are IsoMux forwarding a call it received on its own
implementation of the same interface to the two stacks behind it. So the whole chain is a
multiplexer relaying a request that nothing in tree makes. `EvseManager`, the other
requirer of the interface, never calls it. The port therefore emits no such command and
has no effect variant for one. Adding it would mean choosing a payload with no caller to
say what belongs in it.

### The no energy pause request

`signal_hlc_no_energy_available` reaches the vehicle as `no_energy_pause_charging`
(`EvseManager.cpp:444-456`). It has exactly two producers, and both nest inside
`if (config_context.charge_mode == ChargeMode::DC)`: the bounded wait in the
authorization loop (`Charger.cpp:356-372`, the block opens at `:356`) and the
`PrepareCharging` entry (`:719-725`, the block opens at `:719`). There is no AC producer.

Neither is ported, for two reasons that are separate:

- The gate is `Charger::power_available` (`:2115-2131`), which is two different
  predicates. On AC it is `get_max_current_internal() > 5.9`, which this port has as
  `path::ac::power_available`. On DC it is
  `evse_limit.evse_maximum_current_limit > 0 and evse_limit.evse_maximum_power_limit > 0`
  over `shared_context.current_evse_max_limits`, and that structure is not ported at all:
  the boundary reads only `ac_max_current_A` out of `EnforcedLimits`. So the DC gate has
  no input here, and a producer built on the AC budget instead would be a different
  decision wearing the same name.
- `Dc` holds no authorization loop and no `WaitingForAuthentication` dwell, so the first
  producer has nowhere to sit even with the gate in hand. Its once per session guard
  (`no_energy_warning_printed`, `:367-372`) and its bounded wait are `AcHlc` machinery.

An earlier revision of this module attached the request to `AcHlc::on_timer` for the
wait for energy expiry. That put a pause request meant for the moment before a DC cable
check onto AC sessions, which is a wrong mode signal a real vehicle sees, so it was
removed rather than left standing. `HlcUpdate` therefore has no `NoEnergyPause` variant
and `Settings` derives no mode: both would be surface with no producer. The two config
keys `zero_power_ignore_pause` and `zero_power_allow_ev_to_ignore_pause` are still read
into `MiscSettings`, and `EvseManager.cpp:444-456` is the whole of what they mean, so
porting the DC producer needs the DC limits channel first and then one derivation.

The bounded energy wait itself stays on `AcHlc`, because it is not a DC fact: it only
decides how long the AC authorization loop holds for a budget before proceeding. See
Intentional divergences.

### The pause hop, and what the stop and pause signalling still cannot reach

Three limits used to be recorded here, all of them the same shape: a state transition the
port did not make, with the signalling correct given the transitions it was offered. Two
are closed and the third is narrowed. What closed them is one transition on each path.

**The mid-charge pause is reachable.** In `Charger::run_state_machine` the `Charging` arm
leaves for `StoppingCharging` on `flag_paused_by_evse` among its seven reasons, that
state's entry asks an ISO 15118-20 vehicle to pause, and its exit settles into
`ChargingPausedEVSE`, which can resume. Both paths now make that hop.
`Iec::pause_requested` takes a pause out of `Charging` through the stopping entry instead
of entering `ChargingPausedEvse` directly, and `Dc`'s `PathEvent::PauseRequested` arm does
the same on the DC side. So the pause request no longer waits for something else to end
the session: it goes out at the pause. Driven by
`core::tests::stop_signalling::the_mid_charge_pause`.

**A DC pause withdraws power.** The same arm switches the supply off and withdraws the
relay permission, in that order, and cancels the re-apply watchdog last, which is the
ordering `path::dc::tests::hazards` pins for every other DC energy removal. The monitors
are left running, because the session is still open and the cable is still connected; the
C++ stops them from `subscribe_current_demand_finished`, which is the vehicle's answer to
the request rather than part of it.

**A DC unplug crosses the stopping entry.** `Dc::on_bsp`'s unplug arm enters
`StoppingCharging` before the resting edge, so the vehicle is asked to end the session as
an AC one already was, and every DC unplug now announces `StoppingCharging`. Pinned by
`a_dc_unplug_asks_the_vehicle_to_stop`. The record still closes on the resting edge, which
is what the resting state arm of `duties_for_edge` exists for.

Where a stop is headed is the one piece of state this added. `path::StoppingOutcome` is
stated by the single function on each path that enters `StoppingCharging`, `begin_stopping`,
and read by the single function that leaves it. Neither path mirrors
`Session::paused_by_evse`, which `Core` remains the only writer of: the C++ re-reads that
flag at the `StoppingCharging` exit and the port carries the answer with the transition
instead. Every entry restates it, so a stop cannot begin without saying where it ends.

Both paths also settle a stop that begins with the relays already reported open in the
same pass, rather than parking behind a board fact that has been and gone. That is
`Charger::run_state_machine`'s own settle loop, which re-runs its switch until the state
stops changing.

What is still not reached:

- **A pause arriving outside `Charging` is dropped rather than deferred.** Only the
  `Charging` arm tests `flag_paused_by_evse`. The `PrepareCharging` and
  `ChargingPausedEVSE` arms do not, so the C++ keeps the flag standing and acts on it on
  the pass after the charge starts. Neither path here has a second read site to do that, so
  a pause during a DC preparation is lost, and on AC it enters `ChargingPausedEvse`
  directly without crossing the entry. `Core` still records the flag, so a session paused
  that way is asked to pause at whatever stopping entry it does reach. Pinned by
  `a_pause_during_an_ac_preparation_crosses_no_entry`, and by
  `preparing_and_paused_iso15118_20_dc`, which is what the terminating-route tests drive.

- **A paused DC session resumes.** This was a divergence and has been withdrawn. The
  argument for it was that the `ChargingPausedEVSE` arm's resume leaves for
  `PrepareCharging` and that the rest of that arm, the DC charge loop it hands to, was
  unported, so moving the progress would announce a restart nothing carried out. What that
  missed is that the restart is the **vehicle's**: the C++ entry actuates two things, the
  five percent duty cycle (`Charger.cpp:747-750`) and the relay permission for a session
  still holding both gates (`:739-742`), and the charge then resumes on the next
  `CurrentDemandStarted`, which is an arm this path has had all along. `Dc` now enters
  `PrepareCharging` through `enter_prepare_charging`, the same entry its authorization
  loop runs, so the two routes into that state actuate identically. The SLAC wake up that
  travels with the C++ transition is `Core`'s, owed to whichever path leaves the paused
  state, so it follows without a second producer. Pinned by
  `a_paused_dc_session_resumes_on_the_request`,
  `a_resumed_dc_session_charges_again_on_the_next_current_demand` and
  `a_dc_resume_leaves_the_paused_state_and_wakes_slac`.

- **A resume arriving inside the pause hop has to be repeated.** It lowers
  `Session::paused_by_evse`, and the `StoppingCharging` exit then reads `ChargingPausedEV`,
  which no route here reaches. The session settles into `ChargingPausedEvse` anyway, which
  is what keeps the second resume available. Pinned by
  `a_resume_inside_the_pause_hop_has_to_be_repeated`.

- **A resume reads the vehicle's standing request.** This was a divergence and has been
  withdrawn. `Iec` now holds `iec_allow_close_contactor` as the C++ does: state C or D out
  of B sets it (`IECStateMachine.cpp:241-243`), state B out of either clears it, and the
  `Idle` entry clears it with the session (`Charger.cpp:225`). `Iec::resume_requested`
  reads that latch where it read the relays, which is what lets a vehicle that never left
  state C be allowed power again with no fresh pilot edge to wait for -- and none is
  coming, which is why the C++ `PrepareCharging` arm reads the latch too (`:763-767`).
  The relay proxy read wrong in both directions, and the second was the worse: a vehicle
  that had opened S2 was allowed power for as long as the board had not yet reported the
  relays open. Pinned by `a_resume_allows_power_again_to_a_vehicle_still_asking_for_it`,
  `a_resume_does_not_allow_power_to_a_vehicle_that_withdrew_its_request` and
  `the_next_vehicle_does_not_inherit_the_last_one_s_request`. The `PrepareCharging` arm's
  own use of the latch is not ported: `Iec` still enters `Charging` from the state C
  reading, and the resume is the one route that needed the latch to reach it.

- **A stopped session is still not announced `Finished` on DC, and a DC fault still crosses
  no entry.** `Dc` has no `Finished` entry: a stop parks in `StoppingCharging` until the
  unplug, and `Dc::to_safe_state` deliberately leaves the progress alone, so a DC charge
  stopped by a fault announces nothing and is told nothing. Pinned by
  `a_fault_tells_both_modes_why_and_only_ac_also_asks_them_to_stop`.

- **A faulted session finishes rather than pausing.** The `StoppingCharging` exit reads
  `stop_charging_on_fatal_error_internal` beside the pause and settles a faulted session
  into `ChargingPausedEVSE`; both paths here finish it. Pre-existing, and pinned from both
  ends now: `a_fault_on_a_paused_session_finishes_it` for the state and the test above for
  the signalling.

### Both paused states resume through the preparation

The two paused states used to resume asymmetrically, and the difference was visible on the
wire. `ChargingPausedEv` keeps the duty cycle up and is PWM eligible, so a fresh control
pilot state C reached `Charging` in one edge and the session published `ChargingStarted`
alone. `Charger::process_cp_events_state`'s `ChargingPausedEV` arm answers the same event
by assigning `PrepareCharging`, and `Charger::run_state_machine`'s `PrepareCharging` arm is
the single entry into `EvseState::Charging`, so a C++ deployment publishes the preparation
and then the start on every EV side resume. Anything counting session events, OCPP
included, saw a different sequence from the two.

`ChargingPausedEvse` was never affected: `Iec::resume_requested` restores the offer and
leaves the state in `PrepareCharging` for a state C to finish, which is the same route.
`Iec::car_requested_power` now prepares the session too whenever it is neither preparing
already nor re-reading the state C of a live charge, which under the PWM eligible set is
exactly the vehicle's own pause. So `Charging` has one origin on both routes and both
paused states emit the same ordered pair.

`grant_power` stays the only writer of the state, and being the only writer is not the
whole invariant: where its callers arrive from is what the session events depend on.
`charging_is_only_ever_entered_from_the_preparation` drives every `AcState` and `IecInput`
pair, with the contactor reported either way, and checks that no route enters `Charging`
from anything but the preparation or a charge already running. The ordered sequence itself
is driven from both paused states by
`a_resume_by_the_car_announces_the_preparation_before_the_charge` and
`a_resume_by_the_charger_announces_the_preparation_before_the_charge`, and through `Core` by
`an_ev_pause_and_resume_announce_the_pause_and_then_charging_again`.

This was never in the divergence list. It was found by reading the two paused states beside
each other and asking why one of them was PWM eligible, not by any gate, and a test at the
time pinned the wrong sequence: `an_ev_pause_and_resume_announce_the_pause_and_then_charging_again`
expected `ChargingStarted` alone, reasoning that a single entry into the charging state
makes a resume look like a first start. The single entry is real; what the reasoning missed
is that the C++ arrives at it through the preparation.

Three cells this route does not reach, all of them pre-existing:

- **On AC HLC the C++ resume is driven by a BCB toggle, not by the state C itself.** The
  `ChargingPausedEV` arm of `Charger::process_cp_events_state` assigns `PrepareCharging`
  only for basic AC; with `hlc_charging_active` set it starts a toggle pulse instead, and
  the `ChargingPausedEV` arm of `Charger::run_state_machine` leaves for `PrepareCharging`
  on `bcb_toggle_detected`. Nothing in this port models the toggle, so an HLC session here
  resumes on the bare state C. The route is the same and so is the pair of events it emits;
  what differs is the trigger, which was already true before the preparation was inserted.

- **Neither party can resume the other's pause.** `Charger::process_cp_events_state` has no
  `ChargingPausedEVSE` arm, so a state C under an EVSE pause moves nothing there; here the
  state fails `Iec::pwm_eligible` and the offer is down, which reaches the same answer
  twice. `Charger::resume_charging` only lowers `flag_paused_by_evse`, which the
  `ChargingPausedEV` arm never reads, so a resume command under the vehicle's own pause is
  dropped, as `Iec::resume_requested` drops it. Pinned by
  `neither_party_can_resume_the_other_s_pause`.

- **There is no EV side pause on DC at all.** `ChargingPausedEv` is a control pilot fact and
  `Dc` has no control pilot reducer: a state B arriving mid charge on DC leaves the
  progress in `Charging` and emits nothing. So the state is unreachable there and so is
  this route.

### The ten ISO 15118 charger variables `EvseManager` never subscribes

`interfaces/ISO15118_charger.yaml` declares 46 variables. Ten have no
`subscribe_<name>` anywhere in `modules/EVSE/EvseManager/`, so the C++ receives them and
drops them at the framework: `supported_app_protocols_secc`, `selected_payment_option`,
`requested_energy_transfer_mode`, `dc_bulk_charging_complete`, `dc_charging_complete`,
`ev_app_protocol`, `display_parameters`, `dc_ev_present_voltage`, `meter_info_requested`
and `ev_termination`. Not consuming them is parity, not a gap.

Two corrections to how that set is usually described, both worth keeping because the
obvious reading of the C++ is wrong:

- **The comment block at `EvseManager.cpp:962-973` is not this list.** It says `// unused
  vars of HLC for now:` and names seven things, and two of them,
  `ac_close_contactor` and `ac_open_contactor`, *are* subscribed, at `:412` and `:417`,
  with bodies that do work. A third, `EV_ChargingSession`, is not a variable of the
  interface at all. So the comment is stale in the C++ and only four of its seven entries
  belong to the never subscribed ten.
- **`ev_termination` is in the ten and not in the comment.** The port does better than
  parity on that one: `on_ev_termination` logs the code and the explanation and raises
  `HlcEvent::StopFromEv`, so a vehicle ending the session is acted on rather than
  dropped.

Eight of the ten are the eight `WITHOUT_COUNTERPART` rows of
`tests::dropped_hlc_facts` in `src/main.rs`, which asserts they stay silent. The other
two are consumed rather than dropped: `requested_energy_transfer_mode` into
`HlcEvent::ModeSelected` and `ev_termination` into `HlcEvent::StopFromEv`.

### The three things `Charger::set_max_current` does besides storing the limit

`energyImpl.cpp:529-539` hands the enforced current to `Charger::set_max_current`
(`Charger.cpp:1381-1402`), and this port routes the same figure to `Limits::max_current_a`.
Three of that function's behaviors had no counterpart here. All three belong to the
charger's own limit accounting rather than to the enforced limits handler, which is why
they are one entry, and **all three are now ported**; the entry is kept because what they
are and where they live is what a reader needs to follow `Iec`'s two accessors.

- **The stored limit is an absolute value.** `set_max_current` writes `std::fabs(c)`
  (`:1388`) and keeps the sign of its argument only to orient the signal it emits
  (`:1397`). `Iec::set_current_limit_a` stores the magnitude for that reason, so the
  board's overcurrent threshold and the duty cycle on the pilot are both positive
  currents, and availability is asked of the magnitude as `power_available()` asks it of
  `get_max_current_internal()` (`:2126`). The announcement keeps the sign, because that
  is what the signal carries. Pinned by
  `a_discharge_allowance_is_stored_and_signalled_as_a_magnitude` in `src/core/path/iec.rs`
  and, for the interaction with the no budget test, by
  `a_discharge_allowance_is_not_a_budget_that_has_gone` in `src/core/path/ac.rs`.
- **The cable rating narrows the figure on its way to the board.** `:1393` reads the
  stored limit back through `get_max_current_internal()` (`:2047-2057`), which substitutes
  `max_current_cable` when the connector is an `IEC62196Type2Socket`, the rating is the
  smaller of the two, and the state is not `Idle`. That member is `read_pp_ampacity()`
  cached at `:47` and `:344`, and reset on the `Idle` entry at `:235`.
  `Iec::max_current_internal` is that accessor, and every figure the reducer hands the
  board or the pilot comes through it, which is what keeps the rating from applying to
  some of them and not others. A socket that has reported no rating offers **nothing**,
  which is `value_or(0.0)` in the C++ comparison rather than an omission. The energy node
  still applies the rating to the figure it republishes, where `energyImpl` applies it,
  and `the_cable_cap_changes_only_the_republished_current` in
  `src/core/energy/enforce.rs` pins that half.
- **The budget expires.** `EnforcedLimits::valid_for` becomes `max_current_valid_until`
  (`:1389`), an already expired limit is refused outright (`:1384`), and
  `power_available()` drops the stored limit to zero and warns once the deadline has
  passed (`:2116-2122`). Both halves are ported. The C++ notices the overrun lazily, on
  the next read of availability in a loop that polls; here the grant arms
  `TIMER_BUDGET_VALIDITY` for its own `valid_for_s` and the deadline is the notice, which
  is the same answer at every instant either could be asked. The `mod the_budget_expires`
  tests in `src/core/mod.rs` pin the arming, the fallback, the silence of an expiry with
  nothing to drop, and the refusal of a grant that is already invalid.

The consequences each of them had, for the record: a socket deployment with a thin cable
was offered the full enforced current rather than the cable's rating; an AC bidirectional
session was told no power was available while it discharged, and its board was given a
negative overcurrent threshold; and an energy manager that stopped sending updates left
the last budget standing instead of falling back to zero.

### Outbound commands with no variant

`interfaces/ISO15118_charger.yaml` declares 28 commands. The `Effect::HlcUpdate` dispatch
in `src/main.rs` sends 25 of them, and `tests::dispatched_hlc_updates` pins each arm to the
command it must reach. Of the three it does not send, two are unported for reasons given
above: `no_energy_pause_charging` has no reachable producer here, and
`update_supported_app_protocols` has no originator anywhere in tree.

`pause_charging` was the third until the stop signalling moved onto `Core`. It is now
dispatched as `HlcUpdate::PauseCharging` from the `StoppingCharging` entry, whose
ISO 15118-20 branch has a reachable producer; see "Where the stop and error signalling
lives" above.

`update_dc_maximum_limits` was the fourth until the enforced limits handler gave it a
producer. It is dispatched as `HlcUpdate::DcMaximumLimits` from the live limits update
(`energy_grid/energyImpl.cpp:681`) and pinned in the table like the other 23. The other
call site, the DC mode boot announcement at `EvseManager.cpp:1597-1607`, which sends a
hardcoded 400 A / 200 kW / 1000 V ceiling beside the floor, still has no counterpart here,
so a DC vehicle now learns the maximum limits from the first enforced pass rather than at
boot.

The remaining one was a gap rather than a decision, and it is now closed.

- **`update_meter_info`.** Called at `EvseManager.cpp:1183`, inside `if (hlc_enabled)`,
  immediately before `call_update_ac_present_power` at `:1186`. The two guards are not the
  same: the meter info goes out whenever HLC is enabled, while the present power
  additionally needs `p.power_W` and a selected ISO 15118-20 energy service. So the port
  kept the narrower of an adjacent pair and dropped the broader one, and a vehicle asking
  for a metered reading was told nothing: the stack renders this into `MeterInfo` on
  `ChargingStatusRes` and `CurrentDemandRes`.

  `HlcUpdate::MeterInfo` now goes out on every reading of the billing meter to any port
  with a stack, one effect ahead of the present power as the C++ has it one line ahead.
  The variant carries **no payload**: the record is eight nested wire types deep and the
  core models the four figures it decides on, so the boundary fills it from the same
  cache the three session event payloads read, which the arrival that produced the effect
  wrote before it. The variant says when and the record says what. Pinned by
  `a_meter_reading_without_a_power_figure_announces_no_power`, which drives the weaker
  guard against the stronger one, and by the dispatch ledger row that keeps the record off
  the adjacent command.

### The three signed meter values on the transaction payloads

Carried. `TransactionStarted.signed_meter_value`, and `start_signed_meter_value` and
`signed_meter_value` on `TransactionFinished`, are the billing meter's own answers to
`start_transaction` and `stop_transaction`, which `EverestEffects` keeps in a
`SignedMeterValues` cell exactly as `Charger::shared_context` keeps them
(`Charger.hpp:373-374`). The two payloads read them back where
`evse/evse_managerImpl.cpp:207` and `:279-280` read `get_start_signed_meter_value` and
`get_stop_signed_meter_value`. `SessionStarted.signed_meter_value` stays absent, because
the C++ session started connection assigns every other field and never that one.

What made this more than a widening of the effect completion was the order.
`Charger::start_transaction` calls the meter and **then** signals
`signal_transaction_started_event` (`Charger.cpp:1497-1513`), so its answer is in hand
when the payload is built. This port announced first and pushed `Effect::StartTransaction`
behind the announcement, so the field could only ever be `None`. `Core::start_transaction`
now pushes the request first and announces after it.

Push order alone is not execution order here, and this is the part worth knowing.
Effects run on serial lanes (`Effect::context`): safety, publishes on this module's own
interface, and one per device and for the store. The metering commands were on a shared
pool when this was found and `Effect::PublishSessionEvent` is on the publish lane, so
reversing the push order would have left the request running on one thread while the
payload was rendered on another. All three metering transaction commands - `StartTransaction`, `StopTransaction`
and `CancelAllTransactions` - are therefore `ExecContext::Publish` now, which is the same
reason `Effect::SessionLog` is: what they produce is read back by the publish behind them.
`CancelAllTransactions` joins them although it feeds no payload, because `Core::on_startup`
pushes it directly behind the named close of a recovered record and off that lane it could
overtake that close.

The cost is that a slow billing meter now delays this module's publishes. The C++ pays
more for the same guarantee - its meter call is inline on the charger's own state machine
thread, so a slow meter there delays safety actuation too - and this lane does not.

One divergence is unchanged rather than closed. `Charger.cpp:1427-1432` returns before the
signal on a refused start with `fail_on_powermeter_errors` on, so the C++ publishes **no**
`TransactionStarted` at all there; this port announces unconditionally and raises
`POWERMETER_TRANSACTION_START_FAILED` afterwards from `answer_transaction_start`. That is a
transaction lifecycle change rather than a payload one, it is older than this ordering, and
it wants its own measurement.

The four boundary sites that carry the values are rows of
`session_event_payloads::WIRED` in `src/main.rs`, for the reason that whole ledger exists:
`EverestEffects` holds a `ModulePublisher` and no test in this crate can construct one, so
reverting any of them to the `None` it replaced compiles and passes every gate. The two
payload rows pin the pairing rather than each field alone, because swapping the finish
payload's two fields publishes a well formed record of the wrong energy.

### `session_setup` erased the `authorization_response` in front of it

The same ordering fault as the metering one above, found the same way and fixed the
same way, and worth its own entry because it is the case that made the pool's
membership rule explicit.

`ISO15118_chargerImpl::handle_session_setup` ends by resetting
`v2g_ctx->evse_v2g_data.evse_processing[PHASE_AUTH]` to `Ongoing`
(`modules/EVSE/EvseV2G/charger/ISO15118_chargerImpl.cpp:255-257`), deliberately, so that
a grant cannot leak into the next session. `handle_authorization_response` sets the same
field to `Finished` (`:295`). The two do not commute. The C++ never has to care: it
issues both as direct calls on the thread that decided them.

The core's emission order is right - the `Authorized` session event derives the
`session_setup`, and the `AuthorizationResponse` is emitted after it - but both were
`Effect::HlcUpdate` and therefore on the ordinary pool of three, since retired. A broker
capture caught the inversion, and the whole consequence with it. In the inverted
capture the stack sends `AuthorizationRes` 18 times against the C++ arm's 5, because it
keeps answering `Ongoing` and the vehicle keeps asking; `PowerDeliveryReq` appears 0
times against 4; and `v2g_setup_finished` is never published at all, so
`hlc_charging_active` stays false, `decide_plug_and_charge_start` takes its
`AwaitFallback` branch (`core/path/ac.rs:272-292`, `:1513-1516`) and the port waits out
the ten second five percent fallback. The test's window has closed by then: the same
single test takes 59 s and fails on the port where it takes 25 s and passes on the C++.
Four `ocpp16/plug_and_charge_tests` runs failed on it, and it read as a flake twice
before the capture, because a race that inverts about half the time does.

Every `Effect::HlcUpdate` is `ExecContext::Publish` now, not just that pair. The
twenty five `ISO15118_charger` commands are one conversation with one stateful peer and
they all write `evse_v2g_data` in place; `EvseV2G` says so itself at
`ISO15118_chargerImpl.cpp:343` ("we need to use locks on v2g-ctx in all commands as they
are running in different threads"). Reproducing the C++'s total order for a chosen
subset only reproduces it where the choosing was right, and the sentence this replaced -
"Nothing depends on the order these run in" - is what that choosing already cost. A
third serial lane for HLC alone was considered and rejected: the C++ also orders an
`ISO15118_charger` command against an `evse_manager` publish, because both come off one
thread, and only sharing the existing lane keeps that.

The cost is the metering one again: a slow `EvseV2G` now delays this module's publishes.
The C++ pays more for the same guarantee.

### The AC half of the `ac_with_soc` mode switch announces nothing

This entry used to say the mode switching setup was unported. It is ported now, and
porting it corrected two claims the entry made.

`EvseManager::setup_fake_DC_mode` announces `DC_extended` and `DC_core`, in that
order, and not the four value set an actual DC port derives. `hlc::setup::announce`
sends that pair.

`EvseManager::setup_AC_mode` announces **nothing at all** in this mode. Its
`call_setup` and its `update_supported_energy_transfers` sit inside
`if (ac_hlc_enabled)`, and that is the function's own argument rather than
`config.ac_hlc_enabled`; both call sites that reach it here,
`EvseManager::switch_AC_mode` and the `subscribe_dlink_error` arm, pass `false`. So
the AC transfer mode list the C++ builds two statements earlier is computed and
discarded, and the whole of the AC announcement is `selected_protocol =
"IEC61851-1"`. `announce(PresentedMode::Ac)` returns an empty effect list and says
so, rather than inventing an announcement no deployment has ever sent.

That one field is carried now. `SelectedProtocol::note_mode_announced` is the write,
called from the `SessionDuty::AnnounceMode` arm rather than from inside
`HlcPort::announce`, because the field is not the stack's: it is reported on this
module's own interface and its other four writers are session lifecycle events. The
asymmetry is kept - the AC half writes `BasicAc` and the DC half writes nothing at
all - because a flip that reported basic AC while the vehicle was being shown a fake
DC port would be a wrong answer no consumer could detect. The entry's earlier claim
that the field was not carried is superseded; see *The eight pass-through
variables*.

That is coherent with what the flip does: the ISO 15118 session is being torn down
and the vehicle is about to be reintroduced to a basic AC port.

## Non-goals

Effect execution lanes beyond safety, publish and ordinary. Intake priority classes.
Event coalescing. A macrostep fact FIFO or invariant latching layer. A metrics
registry. An effect port abstraction with a single no-op implementation. Any state
machine that is not reachable from `main`.

That last one is a ratchet rather than a claim, because a port lands bottom up and is
unreachable in places while it is in flight. `scripts/reachability.py` enumerates every
enum variant with no production producer, every `pub fn` whose only callers are tests,
and every subscriber callback that consumes nothing, and fails if the count rises
against `scripts/reachability-baseline.txt`. The number may fall freely and rises only
with a stated reason. It has been wrong once in the direction that matters: it counted
only empty callback bodies, so a callback that gained a log line left the ledger while
still dropping its fact. A body that merely announces a drop now counts as dropped.

A census is the weaker half of the answer, because it reports after the fact and in a
line nobody is required to read. `scripts/unconstructable.py` is the other half, for
the classes where absence became a type: it writes each mistake back into a scratch
copy of `src/` and requires the compiler to refuse it, asserting the diagnostic and
not merely the failure. It is not a fifth gate; it is the evidence that a specific
mistake became impossible rather than merely unlikely. Note the census can legitimately
fall when this happens, and did: `set_cable_check_options` and
`SessionLogger::is_enabled` left the ledger because the surface they named no longer
exists.

The publish lane was added on measured evidence, not on anticipation: three workers
sharing one queue reordered the same four session events three ways across five SIL
runs. A fourth lane needs evidence of the same kind.

That bar is about ordering, throughput and performance, and for those it stays
exactly where it is. An addition that defends against the silent loss of a safety
deadline is exempt, but only when both of these hold. First, the failure is silent,
so there is nothing to measure until the hazard has already happened, and asking for
evidence first is asking for the hazard. Second, the addition costs no thread and no
polling. The timer thread's death notice is the case in point: a guard on that
thread's own stack, reporting on its way out, so it adds neither. An addition meeting
only one of the two is held to the measured evidence bar like everything else.
