.. _exp-configuration-service:

#############################
EVerest Configuration Service
#############################

The EVerest Configuration Service manages EVerest module configurations at
runtime. It runs as part of the EVerest manager process, which exposes stable
Async APIs over MQTT and manages the full lifecycle of EVerest modules —
starting, stopping and restarting them as needed. The manager process stays
alive independently of the module lifecycle, so the Configuration API and the
Lifecycle API are served at all times, even while no module is running.

This page explains the architecture and the observable behavior. For the YAML
configuration format the service consumes, see :ref:`exp-yaml-files`. For the
manager states referred to below — ``Idle``, ``StartingModules``, ``Running`` —
see :ref:`exp-manager-lifecycle`.

***********
Terminology
***********

ConfigServiceCore
  The core component within the EVerest manager responsible for managing EVerest
  configurations in memory and persisting them to storage. It is a single-writer
  actor: every operation is serialized, while readers get the active
  configuration as an immutable snapshot without blocking.

Configuration API
  Stable Async API exposed by the manager over MQTT to access configuration data
  and functionality. Used only by external applications, not directly by EVerest
  modules. Part of the set of public EVerest APIs.

Lifecycle API
  Stable Async API exposed by the manager over MQTT to handle module lifecycles
  (for example start, stop, restart).

Configuration API Client
  Any application or component that uses the Configuration API. This includes
  external applications such as a cloud backend or a user interface.

EVerest Manager
  Long-lived process that hosts the ConfigServiceCore, manages the module
  lifecycle and exposes the Configuration API and Lifecycle API. The manager is
  always running — it starts before modules and can remain available after
  modules are stopped or terminated.

EVerest Internal API
  MQTT-based communication mechanism between the manager and EVerest modules.
  Used for configuration handling and distribution at startup as well as runtime
  queries and updates of parameters. Internal EVerest modules always use this
  Internal API, never the Configuration API.

Configuration slot
  A mechanism for managing and switching between different EVerest module
  configurations, that is, specific sets of connected modules and their
  parameters. While configurations are traditionally loaded from a YAML file, a
  configuration slot translates this concept to the internal database, allowing
  it to store and swap multiple alternative configurations seamlessly.

.. warning::

   The options ``--reset-from-yaml``, ``--into-idle`` and ``--idle-on-failure``
   used on this page are :ref:`experimental <project-experimental-components>`:
   they are exempt from the EVerest stability guarantees and may change or be
   removed in any release. The manager logs a warning at startup when one of
   them is used. See :ref:`reference-manager-cli` for the full option reference.

*******************
Configuration Slots
*******************

A configuration slot holds one complete EVerest configuration: the set of active
modules, their parameters and their connections. The database can hold several
slots side by side, and exactly one of them is the **active** slot — the one the
running modules were started from.

Marking a different slot active does not switch anything immediately. It records that
slot as the **next boot slot**; the switch takes effect the next time the
modules are restarted. This is what keeps the running module processes from ever
desynchronizing from the in-memory configuration.

.. note::

   These configuration slots are unrelated to the OCPP **network configuration
   slots** used to describe CSMS connection profiles, despite the similar name.
   For those, see :ref:`tutorial-ocpp2-adding-slots`.

*******************************************
Boot Sources and the Configuration Database
*******************************************

Which configuration the manager boots from is resolved from the command line
options. There are three cases.

**No** ``--db`` **(with** ``--config`` **or the default config lookup)**
  The YAML config is authoritative. It is parsed, including the
  ``user-config/<config-name>.yaml`` merge, and seeded into a process-private
  **in-memory** database on every start. Nothing is persisted to disk except
  runtime configuration writes, which go to the user-config YAML.

``--db`` **only**
  The database file is the only configuration source; manager settings come from
  built-in defaults.

``--config`` **and** ``--db``
  The database wins when it holds a valid boot slot, and the YAML is then
  ignored. Otherwise the database is seeded from the YAML config.

Related options:

- ``--reset-from-yaml`` (experimental) discards the existing database slot and
  re-seeds from the YAML config file. Intended for development use, when you
  want to reset to a known YAML state. Requires ``--config``.
- ``--db-init`` is deprecated and has no effect. Seeding the database from YAML
  when it holds no valid configuration is now the default; use
  ``--reset-from-yaml`` to force re-seeding.
- ``--conf`` is a deprecated alias for ``--config``. Passing both at once is
  rejected as ambiguous.

.. note::

   Without ``--db``, the in-memory database is still a complete multi-slot
   database, so all slot operations of the Configuration API work at runtime:
   list, duplicate, load from YAML, mark active, delete. They are **ephemeral**.
   On the next start a fresh in-memory database is seeded from the YAML plus
   user-config, so slots and the next-boot-slot selection do not survive a
   restart, and only active-slot parameter writes are persisted, through the
   user-config mirror. The ``everest-config-tool`` cannot inspect an in-memory
   database.

*******************
Communication Paths
*******************

There are two communication paths in this architecture, both over MQTT.

Configuration API
  The public Async API exposed by the manager. External applications use this
  interface for all configuration operations: reading and writing configuration,
  managing slots and subscribing to change notifications.

EVerest Internal API
  The interface between the manager and EVerest modules. The manager uses it to
  distribute module configuration at startup. For runtime configuration
  parameter changes targeting the active slot, the ConfigServiceCore uses this
  same interface to deliver changes to the target module and to receive the
  module's verdict. Modules use it to request configuration operations.

The concrete MQTT topics and message payloads of the Internal API are documented
in
`MQTTCommunication.md <https://github.com/EVerest/EVerest/blob/main/lib/everest/framework/docs/MQTTCommunication.md>`_.

On a high level the Configuration API offers the following operations:

- Read module configuration
- Write module configuration
- Write configuration slots (from a YAML config)
- List configuration slots and duplicate them or set their description
- Notify on configuration changes
- Notify on active-slot and module-status changes
- Delete configuration slots
- Select the configuration slot to boot from

Starting, stopping and restarting EVerest modules is handled by the Lifecycle
API, not the Configuration API.

What the Configuration API does **not** cover:

- Adding a module
- Deleting a module
- Renaming a module
- Connecting a module to another module

All of the above can be achieved by uploading a new YAML configuration
containing those changes.

***************
Manager Startup
***************

1. Populate the manager settings, from the YAML file or from built-in defaults
with ``--db`` only. 2. Initialize the database according to the resolved boot
source. 3. Initialize the ConfigServiceCore, which relies on a valid database
being present. It opens the database and loads the configuration slot marked for
the next reboot. Without ``--db``, a persistence mirror routes active-slot
parameter writes to the user-config YAML in addition to the in-memory slot
storage. 4. Connect to the MQTT broker, set up the EVerest Internal API and
register it with the ConfigServiceCore as the runtime-parameter forwarder.
Expose the APIs giving access to configuration and module lifecycle handling. 5.
Start the modules. 6. The EVerest modules use the Internal API to request their
own configuration.

Step 5 has exceptions. With ``--into-idle`` the manager enters ``Idle`` without
starting modules. A configuration that fails to load or validate, or that
contains no modules, makes the manager exit with an error; ``--into-idle`` and
``--idle-on-failure`` both keep it in ``Idle`` instead, which is reported as
``FailedToStart``.
See the startup-failure behavior in :ref:`exp-manager-lifecycle` for the full
set of outcomes.

.. mermaid:: images/config_service_manager_startup.mmd

***********************
Module Stop and Restart
***********************

The manager can stop and restart modules without itself restarting. This enables
runtime configuration changes that require a module restart, and switching to a
different configuration slot, all without losing the Configuration API.

Marking a slot only records it as the next boot slot and answers immediately; it
does not stop any module. The switch takes effect on the next module restart,
which must be requested separately. The manager reloads the marked slot only
while the modules are at rest — a reload is skipped while modules are running or
mid-transition, so the running processes can never desynchronize from the
in-memory configuration.

.. mermaid:: images/config_service_slot_switch_restart.mmd

If the reloaded configuration is invalid or contains no modules, the restart
fails: the manager exits with an error, or, with ``--idle-on-failure``, stays in
``Idle`` and reports ``FailedToStart``.

**********************
Manager Unavailability
**********************

Since the ConfigServiceCore is part of the manager process, a manager crash
makes the Configuration API unavailable, and the modules are down as well. On
restart the manager reinitializes the ConfigServiceCore, loads the boot slot and
starts the modules.

**********
Deployment
**********

The manager is the single long-lived process. It is started by the system init,
for example via systemd, but does not depend on systemd for module lifecycle
management. Systemd only ensures the manager itself starts on boot.

No special tooling is required for production or development deployments:

.. code-block:: bash

    # YAML is authoritative; in-memory database, re-seeded on every start
    ./manager --config my_config.yaml

    # Database-backed: used once it holds a valid configuration,
    # seeded from YAML otherwise
    ./manager --config my_config.yaml --db everest.db

    # Force re-importing the YAML
    ./manager --config my_config.yaml --db everest.db --reset-from-yaml

There is no distinction between the development and production process
architecture — both use the same single-process model as in previous versions.

***************
Read Operations
***************

By a Configuration API Client
=============================

A Configuration API Client sends a read request to the manager via the
Configuration API. The ConfigServiceCore validates the request and returns the
requested configuration data. This works regardless of whether modules are
running.

.. mermaid:: images/config_service_read_api_client.mmd

By an EVerest Module
====================

An EVerest module sends a read request to the manager via the Internal API. The
ConfigServiceCore validates the request and returns the requested configuration
data.

.. mermaid:: images/config_service_read_module.mmd

****************
Write Operations
****************

Two independent mechanisms gate every write:

- The **access rules** embedded in each module's configuration decide whether
  the caller may touch the parameter at all. A request they do not permit is
  answered with ``AccessDenied``.
- The parameter's **mutability** (``ReadOnly``, ``ReadWrite`` or ``WriteOnly``)
  decides whether it can change at runtime, as opposed to only on the next boot.
  A caller granted ``allow_set_read_only`` is the exception connecting the two:
  for that caller ``ReadOnly`` parameters are treated as writable, so the write
  is accepted and persisted, but it typically only takes effect after a reboot.

Values are validated against the parameter's **datatype** before anything is
persisted, so a value that would fail to parse on the next boot is rejected up
front. This is a datatype check only — no range (min/max) validation happens at
this layer, so a badly chosen but well-typed value is accepted here and can
still be refused by the module.

By a Configuration API Client
=============================

Writes to the **active** slot are refused while the modules are mid-transition,
that is starting, stopping or restart-triggered. The whole request then reports
``ModulesInTransientState``, every parameter reports ``RetryLater``, and nothing
is persisted.

If the target is an **inactive** slot, the change is validated and persisted
directly to the database. It will be applied when EVerest boots from that slot.

If the target is the **active** slot, the change is first persisted, marking it
to be applied on the next restart. Then, if the module is running and the
parameter is mutable at runtime, the change is delivered to the target module
and the manager waits for the module's verdict:

- If the module applies the change immediately, the in-memory configuration is
  updated and the parameter reports ``Applied``.
- If the module requires a restart, the change is already persisted and will be
  loaded on the next boot.
- If the module rejects the runtime change, it will still be applied on the next
  boot, because it has already been persisted.
- If no runtime-change forwarding is set up in the manager, the change is not
  delivered to the module. It has already been persisted and simply applies on
  the next restart, reported as ``WillApplyOnRestart``.

When the manager runs without ``--db``, the user-config YAML mirror is written
*before* the in-memory database: a failed mirror write rejects the update,
because the mirror is then the only persistence that survives a restart.

Finally the ConfigServiceCore sends a notification about the configuration
change — only if at least one parameter was written — and returns a detailed
result to the client for each parameter. The request-level status is ``Ok`` on
success, ``ModulesInTransientState`` in the transient case above, and ``Error``
otherwise. The outcome of every individual parameter (``Applied``,
``WillApplyOnRestart``, ``DoesNotExist``, ``RetryLater``, ``AccessDenied`` or
``Rejected``) is reported per parameter together with an explanation.

.. mermaid:: images/config_service_write_api_client.mmd

By an EVerest Module
====================

An EVerest module sends a write request to the manager via the Internal API.
Unlike the Configuration API, modules update a single parameter at a time and
always target the active slot. The change is first persisted to the database,
guaranteeing it will be active after a restart. If the parameter is mutable at
runtime, it is then forwarded to the target module to be applied immediately.
The final status — ``Accepted``, ``RebootRequired`` or ``Rejected`` — is
returned to the calling module, with an explanation carrying the reason, for
example the module's runtime veto.

.. mermaid:: images/config_service_write_module.mmd
