# Manager lifecycle state machine

State machine diagram: [`ManagerLifecycleStateMachine.mmd`](ManagerLifecycleStateMachine.mmd)

`ManagerState` (defined in `src/manager.hpp`) is the **phase** of the main-loop (what the manager
is doing right now). `ShutdownCause` records **why** a shutdown or drain was started; it is kept
across transient states (for example through `ForceTerminating` / `ShutdownFinalizing`) so the next
step can distinguish normal stop, admin-driven restart, and crash recovery.

Timeouts and limits that drive transitions are defined in `src/manager.cpp` (for example graceful
shutdown duration before `ForceTerminating`, and the cap on automatic crash restarts).

## Startup (happy path)

- `Idle` (initial C++ object state) → `Initializing` when `run()` begins.
- `Initializing` → `StartingModules` when module processes are spawned and ready-handlers are
  registered (`handle_start_modules()`).
- `StartingModules` → `Running` when every non-ignored module has published **ready** on MQTT
  (and standalone handling rules are satisfied). If SIGINT/shutdown is already in progress when
  the last ready arrives, the transition to `Running` is **skipped** on purpose.

## Normal shutdown (SIGINT / SIGTERM)

- First signal (no modules): cleanup and → `Exiting` with success.
- First signal (modules running): `shutdown_cause` = `Normal`, → `ShutdownRequested`, MQTT
  shutdown is published; modules run their shutdown handlers and return so the process can exit;
  child exits are collected while draining (see **Module process exit** below).
- When **all** module children have exited while still in a shutdown-flow state, the manager
  → `ShutdownFinalizing`, then typically `handle_finish_normal_shutdown()`:
  - If this shutdown was due to the first SIGINT/SIGTERM (`sigint_received_`): → `Exiting` with
    success after cleanup.
  - Otherwise the manager can return to **`Idle`** (modules down, manager loop still running;
    another SIGINT/SIGTERM is required to exit the process in that mode).
- **Second** SIGINT/SIGTERM after the first was already handled: treated as "terminate now" →
  `Exiting` with failure (user abort).

## Unexpected module exit ("crash" path)

- While in `StartingModules` or `Running`, if a **module** child exits unexpectedly:
  `shutdown_cause` = `Crash`; graceful shutdown is initiated (MQTT shutdown publish) → first
  `ShutdownRequested`, then immediately → `CrashShutdownInProgress` for that path.
- The same drain / timeout / force-terminate machinery as normal shutdown applies while children
  remain.
- When all children are gone: → `ShutdownFinalizing`. If `--recover-module-crashes` was passed
  on the command line and crash recovery is still allowed (see `MAX_UNEXPECTED_MODULE_RESTARTS`
  in `src/manager.cpp`), the manager reloads config and goes back to **`StartingModules`** via
  `handle_restart_modules_after_shutdown()`. If the restart cap is exceeded with recovery enabled,
  it finishes crash cleanup and → **`Idle`**. Without `--recover-module-crashes` (default), the
  manager shuts down remaining modules gracefully and then **exits** the process.

## Admin "restart modules"

- Controller IPC can request restart while modules are still running. The manager then →
  `RestartRequested` and sets `shutdown_cause` = `Restart`. MQTT shutdown is used to drain
  modules; when all module children have exited, either `advance_lifecycle_state_if_ready()` or the
  `ShutdownFinalizing` path reloads config and returns to **`StartingModules`**.

## Shutdown timeout and forced kill

- From `ShutdownRequested`, `CrashShutdownInProgress`, or `RestartRequested`, if shutdown lasts
  longer than the configured graceful timeout, the manager → `ForceTerminating` and sends
  SIGTERM to remaining module children, then after a further grace period SIGKILL if needed.
- When all tracked children are gone (including after `ECHILD` bookkeeping recovery), the flow
  continues to `ShutdownFinalizing` and the same `ShutdownCause`-driven finalization as above.

## `ForceTerminating` and `ShutdownFinalizing`

- `ForceTerminating`: in-flight forced teardown of stubborn module processes.
- `ShutdownFinalizing`: all module PIDs are gone; the manager decides exit vs idle vs restart
  based on `ShutdownCause` and `sigint_received_` as described above.

## Expected vs exceptional transitions

**Expected:** linear startup; clean idle shutdown after SIGINT; controlled restart after admin
request; optional bounded crash recovery restart loop when `--recover-module-crashes` is set;
manager exit after unexpected module exit when that flag is omitted; timeout escalation only
when modules miss their shutdown deadline.

**Worth noting:** transitions are applied from the main loop (`waitpid`, lifecycle advance,
controller IPC, signal polling, shutdown timer). Re-entrancy is avoided by keeping shared state
on `Manager` and using explicit gates (for example "do not go to `Running` if shutdown already
started").

## Module process exit

When the manager publishes the global MQTT shutdown signal, each module's `Everest::handle_shutdown()`
runs the registered shutdown callback (generated `LdEverest::shutdown()` for C++ modules), then
disconnects MQTT. That stops the module's main loop; `main()` returns and the child process exits
normally. Module authors should tear down threads and resources in `shutdown()` and **return**
promptly — the framework does not call `exit()` from module base classes.

If a module does not implement `shutdown()` on an interface implementation, the default logs a
warning and returns; the process still exits once MQTT disconnect completes.

Legacy module headers that predate the shutdown template may omit a module-level `shutdown()`
entirely; in that case `ModuleBase::shutdown()` logs a warning and returns (no impl hooks run
until the module is regenerated).

If a module blocks in `shutdown()` or keeps other threads running, the manager escalates after the
graceful shutdown timeout (`SHUTDOWN_TIMEOUT_MS`) to SIGTERM and, if needed, SIGKILL (see
**Shutdown timeout and forced kill** above).

## Status fifo (`--status-fifo`)

When a path is passed to `--status-fifo`, the manager writes single-line messages (each terminated
with `\n`) for lifecycle state transitions and selected semantic events. Tests and tooling can
wait on these lines instead of parsing manager logs.

**State notifications** (one per `ManagerState` transition, constants in `utils/status_fifo.hpp`):

- `MANAGER_INITIALIZING`, `MANAGER_STARTING_MODULES`, `MANAGER_RUNNING`, `MANAGER_RESTART_REQUESTED`,
  `MANAGER_CRASH_SHUTDOWN_IN_PROGRESS`, `MANAGER_SHUTDOWN_REQUESTED`, `MANAGER_FORCE_TERMINATING`,
  `MANAGER_SHUTDOWN_FINALIZING`, `MANAGER_IDLE`, `MANAGER_EXITING`

**Startup / readiness** (unchanged):

- `ALL_MODULES_STARTED` — all non-ignored modules reported ready (also implies `MANAGER_RUNNING`
  when the transition was not skipped).
- `WAITING_FOR_STANDALONE_MODULES` — manager-spawned modules are ready; standalone modules still
  pending.

**Semantic events** (not always paired 1:1 with a state name):

- `SIGINT_RECEIVED` — first SIGINT/SIGTERM handled by the manager.
- `ALL_MODULES_STOPPED_CLEAN` — normal shutdown after SIGINT with no unclean module exits.
- `FORCE_SHUTDOWN_TIMEOUT` — graceful shutdown deadline exceeded; force-terminate path started.
- `CRASH_RECOVERY_ATTEMPT:n/max` — crash recovery reload/restart (`--recover-module-crashes`).
- `CRASH_RECOVERY_EXHAUSTED` — recovery cap exceeded; manager will stay idle after shutdown.

Python helpers live in `everest.testing.core_utils.everest_core` (`ManagerStatusFifo`,
`EverestCore.wait_for_manager_status`, `EverestCore.assert_no_manager_status`).
