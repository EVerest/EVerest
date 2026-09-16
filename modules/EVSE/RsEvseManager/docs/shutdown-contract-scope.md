# Shutdown contract: bounded options, and why none of them is in this module

Prepared 2026-09-10. The [timing evidence](effect-ordering-timing.md) establishes a
300.023-second caller wait and no finite stale-actuation bound. Costs are rough
engineer-days including focused tests, not delivery commitments.

**Decided: neither option ships in `RsEvseManager`.** The window both options
address is a framework property that the C++ `EvseManager` this module replaces
shares equally, so closing it here would make the port something other than a
replacement: this port adds nothing the C++ module does not already have. Both
options remain live as EVerest proposals, owned outside this module.

## Option A — acknowledged transport cancellation

**Shared framework work, about 5–10 days before hardware integration.** Existing
request IDs are reusable, but there is no command cancellation/acknowledgement
mechanism in this RPC path. A timeout or removal of a response handler only stops
waiting. Build a cancel request and terminal acknowledgement (`cancelled before
actuation` versus `already executing/completed/unknown`), retained cancellation
state for cancel-before-request and reconnect races, and safe late-reply cleanup.
Cancellation must bypass blocked command workers.

Touch `lib/everest/framework/lib/everest.cpp` (`call_cmd`, `provide_cmd`),
`include/framework/everest.hpp`, `include/utils/types.hpp`/`lib/types.cpp`
(message/status definitions), `lib/message_handler.cpp` (dispatch and queued
requests), and `lib/mqtt_abstraction_impl.cpp` (disconnected request queue).
Expose the handle/context through Rust `Runtime::call_command`, the
`everestrs_sys` C++ bridge, generated client/service bindings, and RsEvseManager's
executor/`EverestEffects::run`. Unsupported peers must be reported, never silently treated as cancelled.

**Cost/risk after that:** roughly 2–5 additional days per selected software
driver, plus hardware qualification. An already-entered handler must cooperate
at its real actuation point; generic dispatch cancellation alone is insufficient.
Risks include framework-wide compatibility, cancel/result races, retention across
restarts, and deadlock if cancellation waits on the worker it must interrupt.
It does not retract a bus frame already sent, reverse prior actuation, or provide
a shutdown deadline when the peer is unreachable or cannot acknowledge stopping.

## Option B — receiver-enforced epoch fence (preferred, as an EVerest proposal)

**First authorization: supply + BSP in simulation, about 3–5 days.**
Use a receiver-issued ownership/boot token and monotonic epoch. A stop advances
that device's epoch and installs an off latch; older commands are rejected at
the actual state write. Restart/reconnect requires fresh ownership, and resuming
requires an explicit new authorization. The fence acknowledgement must identify
what is fenced; receiving a message is not evidence of physical off.

This requires peers, but need not change shared framework transport: add opt-in
interface/types beside `interfaces/power_supply_DC.yaml` and
`interfaces/evse_board_support.yaml`, regenerate bindings and change selected
manifests/wiring. RsEvseManager carries the token/epoch through effects,
executor dispatch and `EverestEffects::run`. Do not silently fall back to unfenced
mutations. Legacy setters and other callers must not bypass an active fence.

For the first proof, change DCSupplySimulator's
`main/power_supply_DCImpl.cpp`: `handle_setMode`, both voltage/current setters,
and `power_supply_worker`; and YetiSimulator's
`board_support/evse_board_supportImpl.cpp` enable/power/PWM/CP/phase/limit handlers
and their state-writing methods in `YetiSimulator.cpp`. Fence checks and state
writes must share a short critical section; no lock held across a slow RPC.

**Production is a separate, named-driver authorization:** about 2–5 days of host
software work per backend, plus bench time; firmware/controller changes require
a separate estimate. Supply sites include `PowerSupplies/*/main/power_supply_DCImpl.cpp`,
Huawei_V100R023C10's `connector_base/base.cpp`, and MicroMegaWattBSP's
`dc_supply/power_supply_DCImpl.cpp`. Follow through to InfyPower's
`acdc->switch_on_off/set_voltage_current`, DPM1000's CAN broker/worker, and
MicroMegaWattBSP's `serial.setOutputVoltageCurrent`. The power_supply_DC_API
adapter must carry the fence through its external MQTT receiver, not terminate
it at the adapter. Already queued hardware writes require downstream enforcement
or a confirmed drain/off latch; a handler-entry check does not qualify.

The first proof excludes IMD, OVM, lock and store. Coverage there requires
interface/receiver changes to IMDSimulator `handle_start/stop/start_self_test`,
OVMSimulator `handle_set_limits/start/stop`, YetiSimulator connector-lock
`handle_lock/unlock`, and `Misc/{Store,YamlStore,PersistentStore}/main/kvsImpl.cpp`
`handle_store/delete` commit points, then deployed equivalents.

## Recommendation and acceptance boundary

If EVerest takes this up, start with **B's bounded supply/BSP proof**: one fence
invalidates all older mutations of a device, including commands delayed before
send, without a generic per-request cancellation protocol. Principal risks are
stale ownership after restart, an unfenced alternate writer, or a driver accepting
a fence before its downstream queue is actually fenced. A receiver unable to
service urgent stop independently still offers no prompt-off guarantee.

**Why it cannot be a module-side change.** A fence refuses an actuation only where
the receiver enforces it. No production driver implements one, so a module that
offers a fence against today's drivers resolves to absent everywhere it matters
and refuses nothing. The value is in the receivers, which is why this belongs to
EVerest rather than to one manager module.

Both require held-invocation, reordered-delivery, lost-acknowledgement, restart,
and downstream-queue tests. Neither undoes prior actuation, proves physical zero
current from a software acknowledgement, nor repairs failed/unreachable hardware.
A hard shutdown deadline needs a verified watchdog/interlock or equivalent
independent off path. Stop after the simulator proof for a named-hardware
decision; no implicit framework rewrite or fleet rollout.
