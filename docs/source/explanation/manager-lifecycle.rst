.. _exp-manager-lifecycle:

###############################
Manager Lifecycle State Machine
###############################

The EVerest manager is the process that spawns and supervises all module
processes; see :doc:`detail-module-concept` for the module concept itself. This
page explains the manager's own lifecycle: how it starts modules, how it drains,
restarts or force-kills them, and how it reports what it is doing.

Two independent pieces of information drive the lifecycle:

- The **state** — what the manager is doing right now. It is what the state
  machine below transitions between, and what the manager reports to the
  outside world.
- The **shutdown reason** — why a shutdown or drain was started: a normal stop,
  an administrative restart, or crash recovery. It is remembered for the whole
  duration of the drain, so that once all modules are gone the manager knows
  whether to exit, to go back to idle, or to start the modules again.

This page describes observable behavior. Timeouts and limits (the graceful
shutdown duration, the grace period before ``SIGKILL``, the cap on automatic
crash restarts) are compile-time constants of the manager and are not
configurable at runtime.

.. warning::

   The options ``--graceful-shutdown``, ``--into-idle``, ``--idle-on-failure``
   and ``--recover-module-crashes`` used on this page are
   :ref:`experimental <project-experimental-components>`: they are exempt from
   the EVerest stability guarantees and may change or be removed in any release.
   The manager logs a warning at startup when one of them is used. See
   :ref:`reference-manager-cli` for the full option reference.

******
States
******

``Idle``
  The manager is alive but no modules are running — before startup, after
  booting with ``--into-idle``, or after modules were stopped without exiting
  the process.

``Initializing``
  The manager has started and is preparing the run: reading the configuration
  and setting up its infrastructure.

``StartingModules``
  Startup metadata is published, MQTT ready handlers are registered and the
  module processes are spawned. The manager stays here until every module has
  reported ready.

``Running``
  All non-ignored modules are ready and the system is operational.

``ShutdownRequested``
  A normal stop was requested (``SIGINT``/``SIGTERM``) and the modules are
  being drained.

``CrashShutdownInProgress``
  A module exited unexpectedly and the remaining modules are being drained.

``RestartRequested``
  An administrative restart was requested and the modules are being drained.

``ForceTerminating``
  Modules did not exit within the drain deadline and are being terminated by
  signal.

``ShutdownFinalizing``
  No module processes are left. The manager now decides — based on the shutdown
  reason — whether to exit, return to ``Idle``, or start the modules again.

``Exiting``
  Final cleanup before the manager process returns.

Every state transition is reported to the
:ref:`Configuration API <exp-configuration-service>` as a module status, derived
from the destination state alone: ``StartingModules`` reports *Starting*,
``Running`` reports *Running*, the three drain states and ``ForceTerminating``
report *Stopping*, ``RestartRequested`` reports *RestartTriggered*,
``ShutdownFinalizing`` reports *Stopped*, and ``Initializing``, ``Idle`` and
``Exiting`` report *AtRest*.

*********************
State Machine Diagram
*********************

.. mermaid:: images/manager_lifecycle_state_machine.mmd

************************************
Expected vs. Exceptional Transitions
************************************

Three paths through the diagram are by design:
:ref:`startup <exp-manager-lifecycle-startup>` followed by a
:ref:`normal shutdown <exp-manager-lifecycle-normal-shutdown>`, an
:ref:`administrative restart <exp-manager-lifecycle-restart>`, and — only with
``--recover-module-crashes`` — a bounded
:ref:`crash recovery loop <exp-manager-lifecycle-crash>`. Every other transition
is a deviation; this is where each one is described:

- Nothing startable at boot (configuration invalid, missing, or without
  modules): :ref:`Startup Failure <exp-manager-lifecycle-startup-failure>`.
- A module reports **ready** after a shutdown has already started, so
  ``Running`` is skipped:
  :ref:`Startup (Happy Path) <exp-manager-lifecycle-startup>`.
- A second ``SIGINT``/``SIGTERM``, or a signal arriving during a crash or
  restart drain:
  :ref:`Normal Shutdown <exp-manager-lifecycle-normal-shutdown>`.
- A module exits unexpectedly, and crash recovery exhausting its restart cap:
  :ref:`Unexpected Module Exit <exp-manager-lifecycle-crash>`.
- A configuration reload that fails or yields no modules:
  :ref:`Administrative Module Restart <exp-manager-lifecycle-restart>`.
- Modules that miss the drain deadline and are terminated by signal:
  :ref:`Shutdown Timeout and Forced Kill <exp-manager-lifecycle-force-kill>`,
  with :ref:`Module Process Exit <exp-manager-lifecycle-module-exit>` for why a
  module gets stuck in the first place.
- Which status events do and do not appear in these cases:
  :ref:`Status FIFO <exp-manager-lifecycle-status-fifo>`.

*****************************************************
Graceful Shutdown Is Opt-In (``--graceful-shutdown``)
*****************************************************

By default the manager does **not** publish the MQTT shutdown signal and does
not wait for modules to exit on their own: whenever the shutdown flow starts
(``SIGINT``/``SIGTERM``, unexpected module exit, admin restart), remaining
module processes are terminated immediately via ``ForceTerminating``
(``SIGTERM``, escalating to ``SIGKILL`` after a grace period). This matches the
pre-lifecycle manager behavior and keeps teardown fast while most modules do not
yet shut down cleanly.

With ``--graceful-shutdown``, the manager first publishes the MQTT shutdown
signal (``<mqtt_everest_prefix>shutdown``, payload ``true``, QOS2, not retained)
so modules can run their registered shutdown handlers and exit by themselves,
and only escalates to ``ForceTerminating`` after the graceful shutdown timeout.
The state machine is identical in both modes; without the flag the drain
deadline is simply zero and the ``FORCE_SHUTDOWN_TIMEOUT`` status event is not
emitted (immediate termination is expected, not a timeout).

All remaining sections of this page describe the graceful
(``--graceful-shutdown``) flow. In default mode the MQTT shutdown publish is
skipped and the force-terminate escalation happens immediately.

.. _exp-manager-lifecycle-startup:

********************
Startup (Happy Path)
********************

``Idle`` → ``Initializing`` → ``StartingModules`` → ``Running``.

The manager publishes its startup metadata, subscribes to the module ready
topics and spawns the module processes. It reaches ``Running`` once every
non-ignored module has published **ready** on MQTT and the standalone handling
rules are satisfied; it then clears the retained startup topics (unless
``--retain-topics``) and publishes the global ready signal on
``<mqtt_everest_prefix>ready``.

If a shutdown is already in progress when the last ready message arrives, the
transition to ``Running`` is **skipped** on purpose — and with it the global
ready publish. A configuration with no modules never reaches ``StartingModules``
in the first place, because nothing would ever report ready and the manager
would wait forever; see the next section.

.. _exp-manager-lifecycle-startup-failure:

***************
Startup Failure
***************

- A configuration that fails to load or validate, or that contains **no
  modules** (empty or missing ``active_modules``, empty database slot), makes
  the manager go to ``Exiting`` with a failure exit code.
- With ``--idle-on-failure`` both cases enter ``Idle`` instead and report
  *FailedToStart* to the Configuration API (matching a failed restart reload),
  so a startable configuration can be loaded and a restart requested.
- ``--into-idle`` is evaluated **before** the configuration is inspected, so the
  manager enters ``Idle`` unconditionally (valid, invalid or empty
  configuration; no modules are started) so the Configuration API stays
  available for loading a corrected configuration and requesting a restart.
- Failures that happen before the lifecycle exists — a configuration database
  that cannot be initialized, or a failed MQTT broker connection — abort the
  startup directly with a failure exit code, **without** a transition to
  ``Exiting`` and therefore without a ``MANAGER_EXITING`` status event. With
  ``--idle-on-failure`` or ``--into-idle`` a database that holds no usable
  configuration is not one of these cases: instead of aborting, the boot
  continues with no active configuration slot — the database is left untouched —
  so it reaches the lifecycle with no modules and the rules above apply.

.. _exp-manager-lifecycle-normal-shutdown:

***********************************
Normal Shutdown (SIGINT or SIGTERM)
***********************************

- First signal with no modules running (for example in ``Idle``): controller
  shutdown, MQTT disconnect and → ``Exiting`` with success — no drain.
- First signal with modules running: the shutdown reason becomes *normal stop*,
  the manager goes to ``ShutdownRequested`` and publishes the MQTT shutdown
  signal; modules run their shutdown handlers and exit (see
  :ref:`Module Process Exit <exp-manager-lifecycle-module-exit>`).
- Once **all** module processes are gone, the manager goes to
  ``ShutdownFinalizing`` and from there to ``Exiting`` with success.
- A **second** ``SIGINT``/``SIGTERM`` is treated as "terminate now": the drain is
  abandoned, any module process still alive is killed immediately (``SIGKILL``,
  no grace period) so no module process outlives the manager (processes a module
  spawned itself are not tracked and are not signalled), and the manager goes to
  ``Exiting`` with a failure exit code (user abort).
- A **first** ``SIGINT``/``SIGTERM`` that arrives while a crash or restart drain
  is already running re-enters ``ShutdownRequested`` and re-arms the drain
  deadline. It overrides a pending restart — the restart intent is dropped and
  the manager stops — but a pending crash reason is deliberately **kept**, so
  the process still exits with failure.

.. note::

   ``ShutdownFinalizing`` can also settle back into ``Idle`` for a *normal stop*
   that did not come from a signal: modules are down, the manager loop keeps
   running, and another ``SIGINT``/``SIGTERM`` is needed to exit the process.
   No caller triggers this today; it exists for a future explicit "stop modules"
   command.

.. _exp-manager-lifecycle-crash:

***********************************
Unexpected Module Exit (Crash Path)
***********************************

- While in ``StartingModules`` or ``Running``, a module process that exits
  unexpectedly sets the shutdown reason to *crash* and starts the drain via
  ``ShutdownRequested`` → ``CrashShutdownInProgress``.
- The same drain, timeout and force-terminate machinery as for a normal
  shutdown applies while modules remain.
- When all modules are gone the manager goes to ``ShutdownFinalizing`` and, by
  default, **exits** with a failure exit code.
- With ``--recover-module-crashes`` it instead reloads the configuration and
  goes back to ``StartingModules`` — provided no ``SIGINT``/``SIGTERM`` was
  received in the meantime (a signal during a crash drain means the user wants
  to stop) and the internal cap on automatic restarts is not yet exhausted.
  Once the cap is exceeded the manager exits with failure, or stays alive in
  ``Idle`` if ``--idle-on-failure`` was also passed.

.. _exp-manager-lifecycle-restart:

*****************************
Administrative Module Restart
*****************************

- A restart requested over the controller IPC (only available with the admin
  panel enabled and while the controller process runs) while modules are
  running sets the shutdown reason to *restart* and goes to
  ``RestartRequested``. The modules are drained; when they are all gone, the
  manager reloads the configuration in ``ShutdownFinalizing`` and returns to
  ``StartingModules``.
- A restart can also be requested while the manager is ``Idle`` (no modules
  running, for example after ``--into-idle`` or a previously failed start).
  There is nothing to drain, so ``Idle`` → ``RestartRequested`` →
  ``ShutdownFinalizing`` → ``StartingModules`` happens without an actual drain.
  This is the :ref:`Configuration API <exp-configuration-service>` workflow:
  load a configuration, then request a restart.
- A reload that fails or yields a configuration with **no modules** is a failed
  restart: the manager goes to ``Exiting`` with a failure exit code, unless
  ``--idle-on-failure`` was passed, in which case it settles into ``Idle`` and
  reports *FailedToStart* (load a corrected configuration and request another
  restart).
- Exception: a restart requested via the Lifecycle API while the manager is
  already ``Idle`` settles back into ``Idle`` and reports *FailedToStart*
  **regardless of** ``--idle-on-failure`` — nothing was running, and exiting
  would take the API away from the very client that must push a corrected
  configuration.

.. _exp-manager-lifecycle-force-kill:

********************************
Shutdown Timeout and Forced Kill
********************************

If a drain — from ``ShutdownRequested``, ``CrashShutdownInProgress`` or
``RestartRequested`` — lasts longer than the graceful shutdown timeout, the
manager goes to ``ForceTerminating``, sends ``SIGTERM`` to the remaining module
processes and, after a short grace period, ``SIGKILL`` to whatever is still
alive. Once all module processes are gone the flow continues to
``ShutdownFinalizing`` and to the same outcome the shutdown reason would have
produced without the escalation.

.. _exp-manager-lifecycle-module-exit:

*******************
Module Process Exit
*******************

When the manager publishes the global MQTT shutdown signal
(``<mqtt_everest_prefix>shutdown``), each module runs its registered shutdown
callback and then disconnects from MQTT. Repeated shutdown signals are ignored.
Disconnecting stops the module's main loop, so the module's ``main()`` returns
and the child process exits normally.

Module authors should tear down threads and resources in the generated
``shutdown()`` hook and **return** promptly — the framework does not call
``exit()`` from the module base classes. Modules or interface implementations
without a shutdown hook (for example generated code that predates the shutdown
template) simply log a debug message and still exit once the MQTT disconnect
completes.

If a module blocks in its shutdown hook or keeps other threads running, the
manager escalates after the graceful shutdown timeout to ``SIGTERM`` and, if
needed, ``SIGKILL`` (see
:ref:`Shutdown Timeout and Forced Kill <exp-manager-lifecycle-force-kill>`).

.. _exp-manager-lifecycle-status-fifo:

*******************************
Status FIFO (``--status-fifo``)
*******************************

When a path is passed to ``--status-fifo``, the manager writes single-line
messages (each terminated with ``\n``) for lifecycle state transitions and
selected semantic events. Tests and tooling can wait on these lines instead of
parsing manager logs. The FIFO must already exist (``mkfifo``) and be open for
reading, otherwise the manager fails at startup; if a later write fails the FIFO
is silently disabled for the rest of the run. Self-transitions are not reported,
so no duplicate line is written for them.

**State notifications** (one per state transition):

- ``MANAGER_INITIALIZING``, ``MANAGER_STARTING_MODULES``, ``MANAGER_RUNNING``,
  ``MANAGER_RESTART_REQUESTED``, ``MANAGER_CRASH_SHUTDOWN_IN_PROGRESS``,
  ``MANAGER_SHUTDOWN_REQUESTED``, ``MANAGER_FORCE_TERMINATING``,
  ``MANAGER_SHUTDOWN_FINALIZING``, ``MANAGER_IDLE``, ``MANAGER_EXITING``

**Startup and readiness:**

- ``ALL_MODULES_STARTED`` — all non-ignored modules reported ready **and** the
  manager actually entered ``Running``; it is written right after
  ``MANAGER_RUNNING``. It is **not** written when the transition to ``Running``
  was skipped because a shutdown was already in progress.
- ``WAITING_FOR_STANDALONE_MODULES`` — manager-spawned modules are ready;
  standalone modules are still pending.

**Semantic events** (not always paired one-to-one with a state):

- ``SIGINT_RECEIVED`` — first ``SIGINT``/``SIGTERM`` handled by the manager.
- ``ALL_MODULES_STOPPED_CLEAN`` — normal shutdown after ``SIGINT`` with no
  unclean module exits (in practice only reachable with ``--graceful-shutdown``;
  force-terminated modules exit by signal).
- ``FORCE_SHUTDOWN_TIMEOUT`` — graceful shutdown deadline exceeded; the
  force-terminate path started. Only emitted with ``--graceful-shutdown``; in
  default mode immediate termination is expected and not reported as a timeout.
- ``CRASH_RECOVERY_ATTEMPT:n/max`` — crash recovery reload and restart
  (``--recover-module-crashes``).
- ``CRASH_RECOVERY_EXHAUSTED`` — recovery cap exceeded; the manager exits with
  failure after shutdown (or stays idle with ``--idle-on-failure``).

