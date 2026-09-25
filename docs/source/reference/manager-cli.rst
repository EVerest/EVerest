.. _reference-manager-cli:

#############################
Manager Command Line Options
#############################

Reference for the command line options of the EVerest ``manager`` binary, the
process that spawns and supervises all module processes. See
:ref:`exp-manager-lifecycle` for what the manager does with them, and
:ref:`exp-configuration-service` for how configuration is stored and loaded.

.. note::

   Option abbreviation is disabled: every option must be spelled in full.
   ``--conf`` is therefore not accepted as a prefix of ``--config`` — it is a
   separate, deprecated option (see below).

Some options are marked **experimental**. Those are part of the public surface
but exempt from the EVerest stability guarantees and the deprecation period:
they may change incompatibly or be removed in any release. See
:ref:`project-experimental-components` for what that means, and
:ref:`project-experimental-index` for the current list. The manager emits a
warning at startup naming any experimental option it was given.

*******
General
*******

``--version``
  Print version and exit.

``--help``, ``-h``
  Produce the help message.

``--check``
  Validate the YAML config given via ``--config`` and exit. Exit code ``0``
  means success. Requires ``--config``: without it the manager exits with an
  error instead of validating the default config. The check needs no MQTT
  broker and neither reads nor seeds the configuration database.

``--prefix <path>``
  Prefix path of the EVerest installation.

**************************
Configuration and Storage
**************************

``--config <path>``
  Full path to a config file. If the file does not exist and has no extension,
  it is looked up in the default config directory. Optional: defaults to the
  default config file in the default config directory. Without ``--db``, the
  config is loaded from YAML on every start and runtime configuration changes
  are persisted to ``user-config/<config-name>.yaml``. Only YAML is accepted;
  configuration files in JSON format are not loaded.

``--conf <path>``
  **Deprecated.** Same as ``--config``. Do not use both — passing both is
  rejected as ambiguous, and using ``--conf`` logs a deprecation warning.

``--db <path>``
  Full path to the configuration database file. Optional: without ``--db`` an
  in-memory database is used and the YAML config is authoritative on every
  start. With ``--db`` and ``--config``, the database wins when it holds a valid
  configuration; otherwise it is seeded from the YAML config.

  The database holds module configurations only, never the manager
  ``settings:`` block. With ``--db`` alone, the settings (installation paths,
  MQTT broker and prefixes, controller port, ``run_as_user``, telemetry, schema
  validation) are the compiled-in defaults. Pass ``--config`` alongside
  ``--db`` whenever the deployment relies on non-default settings; the YAML
  ``settings:`` block is then applied on every start.

``--reset-from-yaml``
  **Experimental.** Discard the existing database slot and re-seed from the YAML
  config file. Intended for development use when you want to reset to a known
  YAML state. Requires ``--config``.

``--db-init``
  **Deprecated, no effect.** Seeding the database from YAML when it holds no
  valid configuration is now the default. Ignored unless both ``--config`` and
  ``--db`` are given. Use ``--reset-from-yaml`` to force re-seeding.

***************
Management APIs
***************

Both options start a :ref:`management API <exp_management_apis>` inside the
manager process, so it stays reachable while no modules are running. Each takes
an optional value: ``ro`` (the default when the option is given without a
value) or ``rw``, as in ``--configuration-api=rw``. Any other value is rejected
and the manager exits with an error.

``--configuration-api[=ro|rw]``
  **Experimental.** Start the configuration_API. In ``ro`` mode only queries are
  served; ``rw`` also enables managing configuration slots, loading YAML and
  changing configuration parameters.

``--lifecycle-api[=ro|rw]``
  **Experimental.** Start the lifecycle_API. In ``ro`` mode only the module
  status and version queries are served; ``rw`` also enables requests to start
  and stop the modules.

****************
Module Lifecycle
****************

``--standalone <module-id>...``, ``-s <module-id>...``
  Module ID(s) to not automatically start child processes for. Those must be
  started manually to make the framework start. Accepts multiple values.

``--ignore <module-id>...``
  Module ID(s) to ignore: do not automatically start child processes and do not
  require that they are started. Accepts multiple values.

``--into-idle``
  **Experimental.** Boot into idle state — no modules are started. Also enters
  Idle instead of exiting when the configuration is invalid, missing or contains
  no modules.

``--idle-on-failure``
  **Experimental.** When there is nothing startable — the boot configuration is
  invalid or contains no modules, crash recovery is exhausted with
  ``--recover-module-crashes``, or a configuration reload fails during a module
  restart — keep the manager alive in Idle so the configuration API stays
  available. Default: exit with an error.

``--recover-module-crashes``
  **Experimental.** After an unexpected module exit, reload the config and
  restart the modules, bounded by an internal retry limit. Default: shut down
  all modules and exit the manager.

``--graceful-shutdown``
  **Experimental.** On shutdown, restart or crash, publish the shutdown signal
  via MQTT so modules can run their shutdown handlers, and force-terminate
  stragglers only after a timeout. Default: terminate module processes
  immediately with ``SIGTERM``, escalating to ``SIGKILL``.

*************************
Diagnostics and Debugging
*************************

``--dump <dir>``
  Dump the validated and augmented main config and all used module manifests
  into the given directory.

``--dumpmanifests <dir>``
  Dump the manifests of all modules into the given directory — including modules
  not used in the config — and exit.

``--dontvalidateschema``
  Do not validate the JSON schema on every message.

``--status-fifo <path>``
  Path to a named pipe that shall be used for status updates from the manager.
  Defaults to empty, which disables it. See :ref:`exp-manager-lifecycle` for the
  messages written to it.

``--retain-topics``
  Retain the configuration MQTT topics set up by the manager, for inspection. By
  default these are cleared after startup.

``--mqtt_everest_prefix <prefix>``
  Override the MQTT everest prefix. Useful for running multiple instances in
  parallel.
