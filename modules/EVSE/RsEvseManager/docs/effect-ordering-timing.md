# Effect ordering and the deferred shutdown decision

Measured 2026-09-10 against the port tree that carried
`fix(RsEvseManager): read state B against the previous pilot level`. Finding 1 is
separately committed as `fix(rs-evse): preserve supply direction through zero
setpoints`. This report does **not** declare the port ready or close the shutdown
race.

**The unbounded stale-actuation window measured here is a property of the EVerest
framework, not a defect of this port.** The C++ `EvseManager` this module replaces
sits behind the same RPC path, the same disconnected publish queue and the same
per-topic receiver serialization, and carries the same window. Three of the four
causes in [Why the stale window has no finite bound](#why-the-stale-window-has-no-finite-bound)
are framework properties both modules share. The fourth, a port worker suspended
between its generation check and its call, is the only port-shaped one, and it is
closed by `fix(rs-evse): serialize device and session store effects`, which needs
no new interface.

**It is deliberately not fixed here.** A receiver-enforced fence is an EVerest-wide
change, is proposed separately, and is not part of this module: see the
[bounded cancellation/fence options](shutdown-contract-scope.md). No option is
implemented in this port, and none should be, because a fence only refuses
anything where the receiver enforces it. No production driver does, so on a real
charger a module-side fence resolves to absent and refuses nothing.

## Decision figures

- **One unanswered RPC took 300.023 seconds to return `CmdTimeout`.** If off
  waits behind that invocation, it can wait approximately five minutes before
  its own invocation starts. This is a measured caller wait, not a physical
  shutdown deadline. Off's own response can then take another 300 seconds.
- **There is no finite guaranteed stale-actuation window after a pre-call
  generation check.** A controlled production-executor probe observed an older
  enable after off by 0.027 ms, 20.076 ms and 1000.125 ms when the checked
  invocation was held for respectively 0, 20 and 1000 ms after off. These are
  injected-delay demonstrations, not estimates of field frequency or maxima.

A simple module-side check plus concurrent off cannot provide a no-late-enable
contract. Neither does waiting for a caller timeout prove that the peer has
finished executing the earlier command. No cancellation, fence, timeout change
or new off policy has been implemented.

## What the 300 seconds covers

The actual call chain is the generated client publisher, Rust
`Runtime::call_command` in `lib/everest/framework/everestrs/everestrs/src/lib.rs`,
C++ `Module::call_command` in `everestrs_sys.cpp`, and
`Everest::call_cmd` in `lib/everest/framework/lib/everest.cpp`.

`everest.cpp` defines `remote_cmd_res_timeout_seconds = 300` and a one-second
polling step at lines 38-39; its constructor initializes the timeout at line 53.
There is no per-command or RsEvseManager configuration override for this value.
The unrelated controller RPC timeout and Auth `connection_timeout` are not used
by these device commands.

At lines 443-460 the framework registers a response handler, publishes the
request, then starts the deadline and waits for the result. Under a normally
scheduled process and a returning publish operation, the remaining response wait
can approach 300 seconds, with up to one polling interval of deadline overshoot
plus scheduling overhead. Schema validation, locks and publication precede that
clock. Without scheduling and transport progress assumptions, there is no strict
finite bound on the entire invocation.

A local EVSE fault or `SupplyOff` does not set the framework's
`shutdown_processed` flag. Its early-exit path is for completed framework process
shutdown, not an electrical safety timeout. Likewise the port's five-second
`EFFECT_DRAIN_TIMEOUT` bounds how long it joins workers; it neither interrupts an
RPC nor cancels a command at the peer.

The existing global safety lane can itself wait behind an earlier safety RPC
(e.g. BSP power-off before supply-off). Its bypass avoids ordinary/device
backlogs; it is not a guaranteed physical shutdown latency.

## Calls that can be ahead of off

`EverestEffects::run` in `src/main.rs` is the authoritative mapping. Each listed
RPC uses the same framework response timeout. There is no new ordering between
these devices, the store, or the publish lane.

| Domain | Mutating calls whose local invocation is serialized |
| --- | --- |
| DC supply | `set_mode`, `set_export_voltage_current`, `set_import_voltage_current` |
| BSP | `enable`, `allow_power_on`, `cp_state_x_1/f/e`, `pwm_on` (also PwmOff), `ac_set_overcurrent_limit_a`, `ac_switch_three_phases_while_charging` |
| IMD | `start`, `stop`, `start_self_test` |
| Overvoltage monitor | `set_limits`, `start`, `stop` |
| Connector lock | `lock`, `unlock` |
| Store | `store`, `delete` |

In the current policy, `SupplyOff`, `AllowPowerOn(false)`, `BspEnable(false)`,
`SetCpState`, and `UnlockConnector` bypass their normal device domain to retain
the existing off/release behavior. They are explicit FIFO exceptions at the one
`safety_context` function in `src/boundary/executor.rs`. Enable/power-on calls
join the BSP queue. Ordinary `SetSupplyMode`, including its Off payload if used,
retains its normal supply domain; the urgent core command is `SupplyOff`.

For the specific supply-off decision, the new supply queue has at most one
locally active mode or setpoint RPC. Other devices and the store do not add to
that queue's delay. A policy that merely appends off to its tail would also wait
for its backlog; the single-in-flight figure assumes a future priority policy
that skips that backlog. Such a policy is deferred.

## Healthy-path measurements and their limits

A standalone C++ probe compiled the **unchanged production `everest.cpp`** and
called `Everest::call_cmd` over a controlled `MQTTAbstraction` double. It accepted
commands and returned results after a chosen delay, then withheld one result for
the full, unmodified timeout. It did not start a broker, simulator, hardware
module, Herdr process, or end-to-end suite.

| Injected response delay | Samples | Min / median / max caller duration |
| --- | ---: | --- |
| 0 ms | 20 | 0.099 / 0.120 / 0.491 ms |
| 20 ms | 20 | 20.276 / 20.353 / 20.424 ms |
| 1000 ms | 3 | 1000.35 / 1000.41 / 1000.44 ms |
| No response | 1 | 300.023 seconds, `CmdTimeout`, exit 0 |

These are framework overhead and controlled-response measurements, **not measured
realistic hardware latency**. Schema validation was disabled in this probe.
No deployed hardware/configuration was supplied for such a distribution.

The available DC example `config/config-sil-dc-rs-evse-manager.yaml` selects
DCSupplySimulator, YetiSimulator, IMDSimulator and OVMSimulator. Their command
handlers normally set local state and return, rather than deliberately waiting
for a physical transition. DCSupplySimulator's mode/setpoint handlers take a
mutex also held by its publishing worker. IMD's three-second self-test countdown
and OVM's configured error-injection delay run outside the command response path;
neither is a synchronous RPC duration. These source facts support a short
healthy simulation path, but do not establish a numerical worst case for mutex
contention, MQTT delivery, a missing reply or an arbitrary hardware driver.

## Why the stale window has no finite bound

1. A worker checks generation G, is suspended before its actual command call,
   and a concurrent off advances to G+1 and completes. On resumption the old
   worker can still send its command. Nothing in a one-time check bounds that
   suspension or revokes the subsequent call. **This is the one port-shaped cause,
   and per-device serialization closes it: a device's commands now run on that
   device's own FIFO worker, so there is no second worker to resume behind off.**
2. The generated command payload contains an ID, arguments and origin, not an
   execution deadline or generation. `Everest::call_cmd` times out the caller;
   `Everest::provide_cmd` still invokes the peer handler without checking a
   caller deadline (`everest.cpp`, command wrapper beginning at line 954).
3. `MQTTAbstractionImpl::publish` queues messages while disconnected, and
   `on_mqtt_connected` drains them later. There is no command-expiry check in
   that queue (`lib/mqtt_abstraction_impl.cpp`, lines 185-190 and 463-473).
4. Receiver `MessageHandler` serializes **per MQTT topic**, not per device
   (`lib/message_handler.cpp`, `dispatch_operation_message`). Mode and setpoint,
   or store and delete, use different command topics. Two `setMode` requests
   already received in order do retain that order; a stuck first handler can
   instead delay off. This does not protect an old call suspended before send,
   which can arrive after off, or establish ordering across command topics.

The measured generation-check probe used the production Rust `Executor` and a
controlled runner. Its model checked a generation, held the invocation, let
`SupplyOff` advance the generation and complete, then released the old invocation.
Only the runner modeled the proposed check; production has no such check. A real
transport/driver acceptance deadline, cancellation acknowledgement or generation
fence would be needed to claim a finite stale-actuation bound. Causes 2, 3 and 4
are framework-wide and reach the C++ `EvseManager` identically; closing them is an
EVerest change, not a port one.

## Implemented and validated scope

Device workers serialize through **local runner completion**; the store has its
own FIFO worker; existing publish ordering is preserved. There is no new
cross-device ordering. The off/release bypass and its stale-actuation possibility
remain explicit. A timed-out remote store request can still execute late; FIFO
fixes the demonstrated local worker race, not distributed cancellation.

- 54 boundary tests passed, including 19 executor tests and both observable
  same-key store/delete orders. The device test exercises both supply orderings,
  BSP commands, IMD operations, monitor operations and the lock worker.
- 15 effect-classification tests passed.
- Returning device/store routing to a shared pool in a scratch mutation
  made the device ordering, store-then-delete and delete-then-store regression
  tests each fail with exit 101 after successful compilation.
- Generated production-boundary `cargo check --bins` and standalone library/test
  Clippy passed. The generated-boundary check skips native link registration
  using `everestrs/build_bazel`; it is not an installed-runtime test.

Raw probes and logs are retained in `build/effect-ordering-validation/` in the
delivery worktree: `rpc-delay.cpp`, `compile-rpc.py`, `rpc-delay.log`,
`window/window.rs`, `window/window.log`, `core/*fifo*.log`, and `mutant/*.log`.
The native probe uses ABI-matched header copies for this worktree's pre-existing
native dependencies (97a62e24e); `everest.cpp` is byte-identical between that
snapshot and bdad5614f and was freshly compiled from the pinned worktree. Using
current headers with the older native library first exposed a ModuleConfig ABI
mismatch; that failed attempt is not measurement evidence. All builds used
`nice -n 10`; Cargo additionally used `--offline -j2` and the explicit cc linker.

The architecture's zero-before-reversal claim remains unproven. Its persistence
recovery claim does not cover delayed remote writes after RPC timeout. `architecture.md` was not edited for either.

## The command reply channel, and what a caller gets when nobody answers

Five `evse_manager` commands return a bool the C++ derives from the real
decision: `pause_charging`, `resume_charging` and `stop_transaction` answer
`flag_transaction_active` (`Charger.cpp:1330-1345`, `:1367-1394`), `reserve`
answers its accept or refuse verdict (`EvseManager.cpp:1735-1773`), and
`enable_disable` answers the *arbitrated* state from
`parse_enable_disable_source_table`, not the requested one
(`Charger.cpp:1697-1774`). `force_unlock` is not in this set: the C++ answers
`true` whenever a connector lock is wired and `false` otherwise
(`evse/evse_managerImpl.cpp:491-500`), which is a boundary fact, and the port
already answers it at the boundary.

This port posts commands onto the single event queue, so intake used to return
before the core had seen the request and answered `true` unconditionally.
`reserve` was the sharp end: the core refuses a reservation on a port that is
not idle or one held under another id, and the wire still reported success, so
the module contradicted a decision it had made one call earlier. A CSMS reads
that as a durable fact.

Intake now blocks on a reply, with a bound.

### Why blocking is safe here

The writer thread does only three things: `queue.recv`, `reducer.apply` (pure,
no I/O) and `dispatch` (an `mpsc` send). Every effect is handed to one of the
serial lane workers, so no effect ever runs on the writer, the writer calls no
peer, and it cannot re-enter intake. The module requires eleven interfaces and
`evse_manager` is not among them, so no effect of ours can reach our own
command either.

A peer's callback cannot deadlock us from the other side. Incoming commands
arrive on the framework's operation pool, serialized per topic
(`message_handler.cpp:107-108`, `dispatch_operation_message`), while results
for our *outgoing* calls are drained by a separate dedicated
`result_worker_thread`. A blocked intake handler therefore cannot delay a lane
worker's outbound call from completing.

### The no-answer case

Two paths give the caller no answer at all, and both are reachable:

- After the writer thread is gone, `EventSender::send` discards the event and
  logs at debug (`boundary/event_loop.rs`).
- An event enqueued after `drive` has returned on `StepOutcome::Shutdown` is
  never processed.

There is a third, narrow window with the same shape. The event queue is
created before `module.start`, so framework callbacks arriving during startup
queue rather than being lost, but the writer that drains it does not exist
until `boundary::start` a little later. A command landing in between waits out
its bound and is refused. That is a refusal rather than an invention, and it is
the honest answer for a module whose core has not begun reading its queue.

So the wait is bounded by `REPLY_TIMEOUT`, and **a breach answers `false`**.

### A timeout is indistinguishable from a no

This is the part of the contract a caller has to be told, because it cannot be
discovered from the wire: **a `false` from any of these five commands may mean
the module decided `false`, or it may mean the module never answered.** The
interface has one bit and no third value, so the two cases are not separable.
Nothing downstream should read a `false` as a positive statement about module
state.

That distinction is sharper for some commands than others:

- `reserve` is the safe case. `false` means "not reserved", and a module that
  never answered has indeed not reserved anything. The timeout answer and the
  decided answer agree about the world.
- `enable_disable` is nearly as safe. `false` means "not enabled", and shutdown
  is where a non-answering module is heading anyway.
- `pause_charging`, `resume_charging` and `stop_transaction` are the weak case,
  and it is worth naming plainly. Their C++ `false` is not "the request was
  declined", it is specifically **"no transaction was active"**
  (`Charger.cpp:1330-1345`, `:1367-1394`) - a claim about state. On a timeout
  this port asserts that claim without having established it, because there is
  no value with which to say "unknown". It is the same shape as the defect this
  channel exists to fix, one size smaller, and it is kept only because the
  alternatives are worse: holding the caller, or inventing a `true` that claims
  an action nothing performed.

A caller that needs to distinguish the two has to use the log, not the return
value. A breach logs at warn from `boundary::replies`, naming the bound.

### Why five seconds

`REPLY_TIMEOUT` is picked so that a breach means *stopped*, not *busy*, with a
wide margin either side:

- **Far above any healthy latency.** The reducer does microseconds of work per
  event, and the loop already warns at 50ms as a sign that something is doing
  I/O on the writer thread. Five seconds is two orders of magnitude above that
  warning, so a breach is never a merely slow pass. The queue would have to
  hold work no realistic depth explains; it warns at a depth of 100.
- **Far below the caller's own budget.** The framework waits 300 seconds for a
  command result (`everest.cpp:38`) and throws `Shutdown` early if MQTT has
  already stopped. Answering at five seconds returns a real `false` the caller
  can act on, rather than letting it run into a `CmdTimeout` it would report as
  an error instead of a decision.

The cost is paid at shutdown, where a caller that is already waiting can sit
out the full bound before being released. That is bounded and it is the only
place the figure is felt.

### Where the answer is produced

`Event::CommandAwaiting` carries a `ReplyToken`; the token is a plain integer,
so `Event` and `Command` keep deriving `Clone`, `Debug` and `PartialEq` and the
core stays free of boundary types. `Core::apply_command` returns the verdict
beside its effects, which is what keeps the answer and the decision from
drifting apart: they are produced by the same call.

The core emits `Effect::AnswerCommand`, and the **writer** intercepts it in
`perform` and completes the waiter directly, exactly as it already intercepts
`StartTimer` and `CancelTimer`. `Effect::context` answers `None` for all three
for one reason: a blocked peer must not be able to delay them.

### What the answer does and does not promise

It reports the decision, not its completion. The effects the decision produced
are dispatched to their lanes and the answer can reach the wire before they
have run. That is the same promise the C++ makes: `enable_disable` returns once
its synchronous state machine pass is done, while the `bsp->enable` it queued
is still a command call in flight.
