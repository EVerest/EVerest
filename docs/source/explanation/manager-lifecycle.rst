.. _exp-manager-lifecycle:

###############################
Manager Lifecycle State Machine
###############################

The EVerest manager is the process that spawns and supervises all module
processes; see :doc:`detail-module-concept` for the module concept itself. This
page explains the manager's own lifecycle: how it starts modules, how it drains,
restarts or force-kills them, and how it reports what it is doing.

``ManagerState`` (defined in
``lib/everest/framework/src/manager_module_status.hpp``) is the **phase** of the
main loop — what the manager is doing right now. ``ShutdownCause`` (defined in
``lib/everest/framework/src/manager.hpp``) records **why** a shutdown or drain
was started; it is kept across transient states (for example through
``ForceTerminating`` and ``ShutdownFinalizing``) so the next step can
distinguish a normal stop, an admin-driven restart and crash recovery.

Every transition also reports a module status to the
:ref:`Configuration API <exp-configuration-service>`. The mapping is the total
function ``module_status_action_for(ManagerState)`` in
`manager_module_status.hpp <https://github.com/EVerest/EVerest/blob/main/lib/everest/framework/src/manager_module_status.hpp>`_.
It is derived from the **destination** state alone:

- ``StartingModules`` → ``Starting``
- ``Running`` → ``Running``
- ``ShutdownRequested``, ``CrashShutdownInProgress``, ``ForceTerminating`` →
  ``Stopping``
- ``RestartRequested`` → ``RestartTriggered``
- ``ShutdownFinalizing`` → ``Stopped``
- ``Initializing``, ``Idle``, ``Exiting`` → ``AtRest``

Timeouts and limits that drive transitions are defined in
`manager.cpp <https://github.com/EVerest/EVerest/blob/main/lib/everest/framework/src/manager.cpp>`_
— for example the graceful shutdown duration before ``ForceTerminating``, and
the cap on automatic crash restarts.

*********************
State Machine Diagram
*********************

.. mermaid:: images/manager_lifecycle_state_machine.mmd

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
and only escalates to ``ForceTerminating`` after the graceful shutdown timeout
(``SHUTDOWN_TIMEOUT_MS``). The state machine is identical in both modes; without
the flag the drain deadline is simply zero and the ``FORCE_SHUTDOWN_TIMEOUT``
status event is not emitted (immediate termination is expected, not a timeout).

All remaining sections of this page describe the graceful
(``--graceful-shutdown``) flow. In default mode the MQTT shutdown publish is
skipped and the force-terminate escalation happens immediately.

********************
Startup (Happy Path)
********************

- ``Idle`` (initial C++ object state) → ``Initializing`` when ``run()`` begins.
- ``Initializing`` → ``StartingModules`` when ``handle_start_modules()`` begins;
  publishing the startup metadata, registering the MQTT ready handlers and
  spawning the module processes all happen in that state. Calling it with an
  empty module list is a ``std::logic_error`` — the boot and restart paths
  reject that case before it gets here, because zero modules would hang the
  manager in ``StartingModules`` forever (no ready handler would ever fire).
- ``StartingModules`` → ``Running`` when every non-ignored module has published
  **ready** on MQTT (and standalone handling rules are satisfied). If
  ``SIGINT``/shutdown is already in progress when the last ready arrives, the
  transition to ``Running`` is **skipped** on purpose.
- On the transition to ``Running`` the manager clears the retained startup
  topics (unless ``--retain-topics``) and publishes the global ready signal on
  ``<mqtt_everest_prefix>ready`` (``transition_to_running_and_announce()``).

***************
Startup Failure
***************

- A configuration that fails to load or validate, or that contains **no
  modules** (empty or missing ``active_modules``, empty database slot), makes
  the manager go to ``Exiting`` with ``EXIT_FAILURE``.
- With ``--idle-on-failure`` the **no modules** case enters ``Idle`` instead and
  reports ``FailedToStart`` to the Configuration API (matching a failed restart
  reload), so a startable configuration can be loaded and a restart requested.
  Invalid configurations still exit.
- ``--into-idle`` is evaluated **before** the configuration is inspected, so the
  manager enters ``Idle`` unconditionally (valid, invalid or empty
  configuration; no modules are started) so the Configuration API stays
  available for loading a corrected configuration and requesting a restart.
- Boot failures that happen before the configuration and lifecycle bookkeeping
  exists return ``EXIT_FAILURE`` straight out of ``run()`` **without** a
  transition to ``Exiting`` (and therefore without a ``MANAGER_EXITING`` status
  event): a configuration database that cannot be initialized, and a failed MQTT
  broker connection.

***********************************
Normal Shutdown (SIGINT or SIGTERM)
***********************************

- First signal (no module children, for example in ``Idle``): controller
  shutdown, MQTT disconnect and → ``Exiting`` with success — no drain.
- First signal (modules running): ``shutdown_cause`` = ``Normal``, →
  ``ShutdownRequested``, MQTT shutdown is published; modules run their shutdown
  handlers and return so the process can exit; child exits are collected while
  draining (see :ref:`Module Process Exit <exp-manager-lifecycle-module-exit>`).
- When **all** module children have exited while still in a shutdown-flow state,
  the manager goes to ``ShutdownFinalizing``, then typically
  ``handle_finish_normal_shutdown()``:

  - If this shutdown was due to the first ``SIGINT``/``SIGTERM``
    (``sigint_received_``): → ``Exiting`` with success after cleanup.
  - Otherwise the manager returns to ``Idle`` unconditionally (not gated by
    ``--idle-on-failure``: a requested stop is not a failure). Modules are down,
    the manager loop keeps running and another ``SIGINT``/``SIGTERM`` is
    required to exit the process.

- A **second** ``SIGINT``/``SIGTERM`` after the first was already handled is
  treated as "terminate now" → ``Exiting`` with failure (user abort). The
  controller is shut down, but MQTT is not disconnected explicitly on this path.
- A **first** ``SIGINT``/``SIGTERM`` that arrives while a crash or restart drain
  is already running re-enters ``ShutdownRequested`` (re-arming the drain
  deadline). It overrides a pending ``ShutdownCause::Restart`` with ``Normal`` —
  so the restart intent is dropped and the manager stops — but a
  ``ShutdownCause::Crash`` is deliberately **kept**, so the process still exits
  with failure.

.. note::

   The "return to ``Idle``" branch above is currently unreachable, because
   ``ShutdownCause::Normal`` is only ever set together with
   ``sigint_received_``. It exists for a future explicit "stop modules" command.

***********************************
Unexpected Module Exit (Crash Path)
***********************************

- While in ``StartingModules`` or ``Running``, if a **module** child exits
  unexpectedly: ``shutdown_cause`` = ``Crash``; graceful shutdown is initiated
  (MQTT shutdown publish) → first ``ShutdownRequested``, then immediately →
  ``CrashShutdownInProgress`` for that path.
- The same drain, timeout and force-terminate machinery as for a normal shutdown
  applies while children remain.
- When all children are gone: → ``ShutdownFinalizing``. If
  ``--recover-module-crashes`` was passed on the command line, no
  ``SIGINT``/``SIGTERM`` was received in the meantime (a signal during a crash
  drain means the user wants to stop, so no auto-restart happens), and crash
  recovery is still allowed (see ``MAX_UNEXPECTED_MODULE_RESTARTS`` in
  ``lib/everest/framework/src/manager.cpp``), the manager reloads the
  configuration and goes back to ``StartingModules`` via
  ``handle_restart_modules_after_shutdown()``. If the restart cap is exceeded
  with recovery enabled, it finishes crash cleanup and **exits with failure** —
  unless ``--idle-on-failure`` was also passed, in which case it stays alive in
  ``Idle``. Without ``--recover-module-crashes`` (the default), the manager
  shuts down remaining modules gracefully and then **exits** the process.

*****************************
Administrative Module Restart
*****************************

- Controller IPC (the ``restart_modules`` method; only compiled in with
  ``ENABLE_ADMIN_PANEL`` and only answered while the controller process runs)
  can request a restart while modules are still running. The manager then goes
  to ``RestartRequested`` and sets ``shutdown_cause`` = ``Restart``. MQTT
  shutdown is used to drain modules; when all module children have exited,
  ``advance_lifecycle_state_if_ready()`` goes to ``ShutdownFinalizing`` and
  ``handle_restart_modules_after_shutdown()`` reloads the configuration and
  returns to ``StartingModules``. (``advance_lifecycle_state_if_ready()`` has a
  second, defensive restart branch for a ``Restart`` cause outside a
  shutdown-flow state; ``RestartRequested`` is itself a shutdown-flow state, so
  the finalizing path is the one that normally runs.)
- A restart can also be requested while the manager is ``Idle`` (no modules
  running, for example after ``--into-idle`` or a previously failed start).
  There is nothing to drain, so ``Idle`` → ``RestartRequested`` →
  ``ShutdownFinalizing`` → ``StartingModules`` happens without an actual drain.
  This is the :ref:`Configuration API <exp-configuration-service>` workflow:
  load a configuration, then request a restart.
- A reload that fails or yields a configuration with **no modules** is a failed
  restart: the manager goes to ``Exiting`` with ``EXIT_FAILURE``, unless
  ``--idle-on-failure`` was passed, in which case it settles into ``Idle`` and
  reports ``FailedToStart`` (load a corrected configuration and request another
  restart).

.. _exp-manager-lifecycle-force-kill:

********************************
Shutdown Timeout and Forced Kill
********************************

- From ``ShutdownRequested``, ``CrashShutdownInProgress`` or
  ``RestartRequested``, if the shutdown lasts longer than the configured
  graceful timeout, the manager goes to ``ForceTerminating`` and sends
  ``SIGTERM`` to the remaining module children, then ``SIGKILL`` to whatever is
  still alive ``FORCE_KILL_GRACE_TIMEOUT_MS`` later (once;
  ``force_kill_sent_``).
- When all tracked children are gone (including after ``ECHILD`` bookkeeping
  recovery), the flow continues to ``ShutdownFinalizing`` and the same
  ``ShutdownCause``-driven finalization as above.

***********************************************
``ForceTerminating`` and ``ShutdownFinalizing``
***********************************************

- ``ForceTerminating``: in-flight forced teardown of stubborn module processes.
- ``ShutdownFinalizing``: all module PIDs are gone; the manager decides exit vs.
  idle vs. restart based on ``ShutdownCause`` and ``sigint_received_`` as
  described above.

************************************
Expected vs. Exceptional Transitions
************************************

**Expected:** linear startup; clean idle shutdown after ``SIGINT``; controlled
restart after an admin request; optional bounded crash recovery restart loop
when ``--recover-module-crashes`` is set; manager exit after an unexpected
module exit when that flag is omitted; timeout escalation only when modules miss
their shutdown deadline.

.. note::

   Almost all transitions are applied from the main loop (``waitpid``, lifecycle
   advance, controller IPC, signal polling, shutdown timer). The exception is
   the transition to ``Running``, which is applied from the module-ready MQTT
   handler on the message-dispatch thread. ``state_`` is therefore atomic and
   all transitions are serialized with ``state_transition_mutex_``; the "do not
   go to ``Running`` if shutdown already started" check and the transition
   itself are taken under that lock so the gate cannot race with the main loop.

.. _exp-manager-lifecycle-module-exit:

*******************
Module Process Exit
*******************

When the manager publishes the global MQTT shutdown signal
(``<mqtt_everest_prefix>shutdown``), each module's
``Everest::handle_shutdown()`` runs the registered shutdown callback (the
generated ``LdEverest::shutdown()`` for C++ modules), then disconnects MQTT.
Repeated shutdown signals are ignored (``shutdown_received``), and a module
without a registered handler logs a warning ("No shutdown handler registered for
module …") and still disconnects. Disconnecting stops the module's main loop;
``main()`` returns and the child process exits normally. Module authors should
tear down threads and resources in ``shutdown()`` and **return** promptly — the
framework does not call ``exit()`` from module base classes.

If a module does not implement ``shutdown()`` on an interface implementation,
``ImplementationBase::shutdown()`` logs a debug message and returns; the process
still exits once the MQTT disconnect completes.

Legacy module headers that predate the shutdown template may omit a module-level
``shutdown()`` entirely; in that case ``ModuleBase::shutdown()`` logs a debug
message and returns (no implementation hooks run until the module is
regenerated).

If a module blocks in ``shutdown()`` or keeps other threads running, the manager
escalates after the graceful shutdown timeout (``SHUTDOWN_TIMEOUT_MS``) to
``SIGTERM`` and, if needed, ``SIGKILL`` (see
:ref:`Shutdown Timeout and Forced Kill <exp-manager-lifecycle-force-kill>`).

*******************************
Status FIFO (``--status-fifo``)
*******************************

When a path is passed to ``--status-fifo``, the manager writes single-line
messages (each terminated with ``\n``) for lifecycle state transitions and
selected semantic events. Tests and tooling can wait on these lines instead of
parsing manager logs. The FIFO must already exist (``mkfifo``) and be open for
reading, otherwise ``StatusFifo::create_from_path()`` throws at startup; if a
later write fails the FIFO is silently disabled for the rest of the run.
Self-transitions are dropped by ``transition_to_unlocked()``, so no duplicate
line is written for them.

**State notifications** (one per ``ManagerState`` transition, constants in
``lib/everest/framework/include/utils/status_fifo.hpp``):

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

**Semantic events** (not always paired one-to-one with a state name; these are
not all ``StatusFifo`` constants — ``CRASH_RECOVERY_ATTEMPT`` is formatted in
``Manager::notify_crash_recovery_attempt()``):

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

Python helpers live in ``everest.testing.core_utils.everest_core``
(``ManagerStatusFifo``, ``EverestCore.wait_for_manager_status``,
``EverestCore.assert_no_manager_status``).
