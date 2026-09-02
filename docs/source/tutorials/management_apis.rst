.. _tutorial_management_apis:

###############
Management APIs
###############

This tutorial demonstrates the basic usage of the management APIs in a hands-on fashion.
For a more in-depth explanation read the :ref:`explanation section <exp_management_apis>`.

The management APIs consist of the `lifecycle API` and the `configuration API`.
Both are implemented as part of the central manager process, but are disabled by default.

Short Configuration Introduction
================================

There is some knowledge of the concept of the EVerest configuration required to understand what is
happening in this tutorial.

EVerest has different types of configuration. For this tutorial you need to know, there are
a so-called *manager settings* and the actual *module configuration*.

Manager Settings
****************

These settings are required to start the manager process. They include paths to different components
(e.g. where the configuration database should be stored, where the module executables are stored, ...),
the MQTT settings (e.g. host and port or socket, etc.) and similar settings.

Starting the manager process requires a YAML file which contains these settings.

Module Configuration
********************

The *module configuration* is a list of modules which should be loaded, how they are connected and
their individual configuration parameters. Module configurations are stored in a database
which is maintained by the manager process. Multiple sets of module configurations can be prepared
and stored in the database alongside each other. Only one of them can be active at a time.
Switching between these so-called *slots* requires the modules to be stopped and started again (no
hot-swapping or dynamic loading of additional modules possible). To bootstrap the database the
*module configuration* can be provided in the same YAML file as the manager settings.

While switching *slots* (i.e. which modules are loaded and how they are connected) requires restarting
the modules, changes to configuration parameters of individual modules can be done anytime. If the
change is directed to a *slot* which is not active, then it modifies the configuration stored in the
database. If it is directed to the *slot* which holds the active *module configuration*, then the
changes are first sent to the modules, and if they accepted them then written to the database.

.. important::

    Without the ``--db`` command line option the manager keeps the configuration database in
    memory only: it is seeded from the YAML file on every start and everything you change through
    the API is lost when the manager exits. Pass ``--db <path>`` to persist the database in a file.
    Once the file holds a valid configuration, the database wins over the YAML file on the next
    start (``--reset-from-yaml`` discards it and re-seeds from the YAML file).

The CLI client
==============

This tutorial uses ``everest-management-api-cli``, the command line client for both management
APIs that is part of EVerest (see :ref:`its description <exp_dev_tools_management_api_cli>`).
Building EVerest with ``-DBUILD_TESTING=ON`` installs it into the Python venv of the build
directory. Open a second terminal
next to the one you will start EVerest in, change into the ``build`` folder, activate the venv and
start the client's interactive shell:

.. code-block:: bash

    $ cd build
    $ source venv/bin/activate
    $ everest-management-api-cli shell
    EVerest management API shell. Type 'help' for commands, 'quit' to leave.
    Connected to 127.0.0.1:1883; watching everest_api/1/lifecycle/e2m/status,
    everest_api/1/configuration/e2m/active_slot and everest_api/1/configuration/e2m/config_updates.
    Status updates and notices are printed as they arrive.
    everest-api>

The shell keeps one connection to the MQTT broker (``localhost:1883`` by default, see
``--host`` and ``--port``). Every command typed at the ``everest-api>`` prompt sends one request
and prints the reply as JSON. In addition, the shell subscribes to the topics on which the manager
publishes on its own and prints those messages the moment they arrive, prefixed with the name of
the topic: ``[status]`` for the lifecycle status, ``[active_slot]`` and ``[config_updates]`` for
the notices of the configuration API. All of the output shown below is from this single shell
session, so keep it running throughout the tutorial. ``help`` lists the commands, ``help <command>``
the options of one of them.

.. note::

    Every command is also available as a one-shot invocation for scripts, e.g.
    ``everest-management-api-cli slots``, and ``everest-management-api-cli watch`` prints only the
    ``[status]`` and notice lines, without a prompt.

Don't forget to run ``cmake --install .`` in the ``build`` folder before starting EVerest.

Using the configuration API
===========================

The management APIs are always part of a build, but they need to be activated when starting EVerest.

Read-Only Usage
***************

In order to activate the configuration API, append the *manager* command line options as shown below
(the generated run-scripts pass additional arguments on to the manager):

.. code-block:: bash

    $ ./run-scripts/run-example.sh --configuration-api=ro

    ...
    2026-09-02 19:23:38.988075 [INFO] manager          :: Starting ConfigurationAPI in read-only mode
    ...
    2026-09-02 19:23:39.578363 [INFO] example:Example  :: Config log "actual [original value]": log_interval=10 [10]; enum_test="one" ["one"]; example|current=42.000000 [42.000000]; example|enum_test="one" ["one"]; example|enum_test2="2" ["2"]

Observe the manager logging that the configuration API has been started.
Also note, that the Example module is periodically logging its current configuration.

For now we started the API in read-only mode. Switch to the shell and query the API for the list of
configuration slots and the currently active slot:

.. code-block:: text

    everest-api> slots
    {
      "slots": [
        {
          "config_file_path": "/home/user/workspace/everest-core/config/config-example.yaml",
          "last_updated": "2026-09-02T17:23:38.974Z",
          "slot_id": 0
        }
      ]
    }
    everest-api> active
    {
      "active_slot_id": 0,
      "next_boot_slot_id": 0
    }

We find, there is only a single slot existing (0) and it is used as the *active slot*
(the one which is used *if* EVerest is running right now) and also as the one to be
used for the next restart of the manager process. The slot list also carries the metadata of
each slot: an optional description, the time of the last update and, for a slot seeded from a
YAML file, the path of that file.

The full configuration stored in a slot can be retrieved:

.. code-block:: text

    everest-api> config 0
    {
      "module_configurations": [
        {
          "config_access": null,
          "connections": [
            {
              "fulfillments": [
                {
                  "implementation_id": "main",
                  "index": 0,
                  "module_id": "store"
                }
              ],
              "requirement_id": "kvs"
            }
          ],
          "implementation_configuration_parameters": [
            {
              "configuration_parameters": [
                {
                  "characteristics": {
                    "datatype": "Decimal",
                    "mutability": "ReadWrite"
                  },
                  "name": "current",
                  "value": "42.000000"
                },
    ...

.. important::

    Only the module configuration is stored and retrievable. Thus, in general it's not
    possible to reconstruct the yaml file which may have been used to initially load this
    module configuration into the database, as it also contains the manager settings.

In read-only mode every modifying request is answered, but refused:

.. code-block:: text

    everest-api> setp 0 example log_interval 5
    {
      "results": [
        "Rejected"
      ]
    }

The configuration API also announces changes of the active slot. Stop EVerest with ``Ctrl-C`` in
its terminal and start it again with the same command; the shell prints the ``[active_slot]``
notices as they arrive:

.. code-block:: text

    everest-api>
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 0, "status": "Stopping", "tstamp": "2026-09-02T17:23:52.774Z"}
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 0, "status": "Stopped", "tstamp": "2026-09-02T17:23:54.409Z"}
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 0, "status": "Starting", "tstamp": "2026-09-02T17:23:54.410Z"}
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 0, "status": "Running", "tstamp": "2026-09-02T17:23:54.879Z"}
    everest-api>

These notices are not retained by the broker, so they are only seen by clients that are
subscribed at the moment the manager publishes them. Once the lifecycle API is enabled as
well (see below), its ``[status]`` messages show up in the shell, too.

Read-write Usage
****************

Starting the manager with ``rw`` enables write access to the configuration API. Since we are
going to create slots that should survive a restart of the manager, we also give it a database file:

.. code-block:: bash

    $ ./run-scripts/run-example.sh --configuration-api=rw --db /tmp/everest-tutorial.db

    ...
    2026-09-02 19:24:01.961149 [INFO] manager          :: Starting ConfigurationAPI in read-write mode
    ...

Now we can load module configurations from a YAML file and subsequently make sure a new slot has been created:

.. code-block:: text

    everest-api> load --description "SIL setup" <your-EVerest-dir>/config/config-sil.yaml
    {
      "error_message": "",
      "slot_id": 1,
      "success": true
    }
    everest-api> slots
    {
      "slots": [
        {
          "config_file_path": "/home/user/workspace/everest-core/config/config-example.yaml",
          "last_updated": "2026-09-02T17:24:01.941Z",
          "slot_id": 0
        },
        {
          "description": "SIL setup",
          "last_updated": "2026-09-02T17:24:07.670Z",
          "slot_id": 1
        }
      ]
    }

.. note::

    ``config-sil.yaml`` is a complete EVerest configuration file, i.e. it also contains manager
    settings. The client only sends its ``active_modules`` section, as a slot holds module
    configuration only.

To reboot from this slot the next time the modules start:

.. code-block:: text

    everest-api> mark 1
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 1, "status": "Running", "tstamp": "2026-09-02T17:24:10.937Z"}
    {
      "result": "Success"
    }
    everest-api> active
    {
      "active_slot_id": 0,
      "next_boot_slot_id": 1
    }

The manager announces the new choice on the ``active_slot`` topic before it answers the request,
which is why the shell prints the notice ahead of the reply. As you can see, the ``active`` command
now returns separate results for the currently active slot and the one that would be used if a
reboot would happen now. Thanks to ``--db`` this choice is persisted: if you stop the manager
process and start it again with the same ``--db`` option, it boots the SIL configuration from
slot 1 instead of the YAML file.

A different way to populate a new slot is to duplicate an existing one:

.. code-block:: text

    everest-api> dup 1 --description "Created as duplicate of slot 1"
    {
      "slot_id": 2,
      "success": true
    }
    everest-api> slots
    {
      "slots": [
        {
          "config_file_path": "/home/user/workspace/everest-core/config/config-example.yaml",
          "last_updated": "2026-09-02T17:24:01.941Z",
          "slot_id": 0
        },
        {
          "description": "SIL setup",
          "last_updated": "2026-09-02T17:24:07.670Z",
          "slot_id": 1
        },
        {
          "description": "Created as duplicate of slot 1",
          "last_updated": "2026-09-02T17:24:14.219Z",
          "slot_id": 2
        }
      ]
    }

Deleting an unused slot is possible as well, but not for the active slot. A slot that is marked
for the next boot counts as active here, too:

.. code-block:: text

    everest-api> delete 2
    {
      "result": "Success"
    }
    everest-api> delete 1
    {
      "result": "CannotDeleteActiveSlot"
    }

Modification of configuration parameters via the CLI client is possible by providing
it with a file similar to a configuration YAML but reduced to the configuration parameters
which have actual changes. E.g. the following example file ``cfg_update_to.yaml`` can be
used to change the `log_interval` parameter of the `example` module to the value `3`,
`enum_test` to `"two"` and for the `example` implementation: `current` to `40`,
`enum_test` to `"two"` and `enum_test2` to `1`.

.. code-block:: yaml

    active_modules:
      example:
        config_module:
          log_interval: 3
          enum_test: "two"
        config_implementation:
          example:
            current: 40
            enum_test: "two"
            enum_test2: 1

.. note::

    A single parameter can also be changed without a file:
    ``setp 0 example log_interval 3`` for a module-level parameter, or with ``--impl example``
    for a parameter of the ``example`` implementation.

To test this, first mark slot 0 for booting again and delete the other slot:

.. code-block:: text

    everest-api> mark 0
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 0, "status": "Running", "tstamp": "2026-09-02T17:24:20.815Z"}
    {
      "result": "Success"
    }
    everest-api> delete 1
    {
      "result": "Success"
    }

Now create a duplicate from the existing slot `0` and modify the newly created configuration.
The file is looked up relative to the directory the shell was started in:

.. code-block:: text

    everest-api> dup 0 --description "Created as duplicate of slot 0"
    {
      "slot_id": 1,
      "success": true
    }
    everest-api> setp 1 --file cfg_update_to.yaml
    [config_updates] {"origin": {"external": true}, "slot_id": 1, "tstamp": "2026-09-02T17:24:25.745Z", "update_results": [...]}
    {
      "results": [
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart"
      ]
    }

The API reports one result per requested parameter, in the order of the request (which is the
order of the file). All changes will only become active on the next restart. This is because
slot `1` is not active. (The ``[config_updates]`` notice is shortened here; it is explained below.)

Requesting the same changes to the active slot gives different results:

.. code-block:: text

    everest-api> setp 0 --file cfg_update_to.yaml
    [config_updates] {"origin": {"external": true}, "slot_id": 0, "tstamp": "2026-09-02T17:24:27.393Z", "update_results": [...]}
    {
      "results": [
        "Applied",
        "Applied",
        "Applied",
        "Applied",
        "WillApplyOnRestart"
      ]
    }

.. note::

    Every accepted change is persisted to the slot *before* it is forwarded to the running
    module. A module may still reject the runtime change (the Example module always rejects
    runtime changes of `enum_test2`). In that case the parameter keeps its current value at
    runtime and the change is reported as `WillApplyOnRestart`: it becomes effective the next
    time the modules are restarted.

Changes to module configuration parameters are announced on the ``config_updates`` topic, so
every ``setp`` is accompanied by a ``[config_updates]`` notice in the shell. The shell prints
it on a single line; reformatted for readability, the notice of the last command reads:

.. code-block:: text

    [config_updates] {
      "origin": {"external": true},
      "slot_id": 0,
      "tstamp": "2026-09-02T17:24:27.393Z",
      "update_results": [
        {"result": "Applied", "update": {"cfg_param_id": {"implementation_id": "!module", "module_id": "example", "parameter_name": "log_interval"}, "value": "3"}},
        {"result": "Applied", "update": {"cfg_param_id": {"implementation_id": "!module", "module_id": "example", "parameter_name": "enum_test"}, "value": "two"}},
        {"result": "Applied", "update": {"cfg_param_id": {"implementation_id": "example", "module_id": "example", "parameter_name": "current"}, "value": "40"}},
        {"result": "Applied", "update": {"cfg_param_id": {"implementation_id": "example", "module_id": "example", "parameter_name": "enum_test"}, "value": "two"}},
        {"result": "WillApplyOnRestart", "update": {"cfg_param_id": {"implementation_id": "example", "module_id": "example", "parameter_name": "enum_test2"}, "value": "1"}}
      ]
    }

Module-level parameters show up with the pseudo implementation id ``!module`` in these notices.

You will also be able to observe this in the EVerest logs:

.. code-block:: bash

    ...
    Config log "actual [original value]": log_interval=10 [10]; enum_test="one" ["one"]; example|current=42.000000 [42.000000]; example|enum_test="one" ["one"]; example|enum_test2="2" ["2"]
    ...
    2026-09-02 19:24:27.405091 [INFO] example:Example  :: Cfg Update for 'log_interval' | old == '10', new == '3' accepted
    2026-09-02 19:24:27.489306 [INFO] example:Example  :: Cfg Update for 'enum_test' | old == 'one', new == 'two' accepted
    2026-09-02 19:24:27.574780 [INFO] example:Example  :: Cfg Update for 'current' | old == '42.000000', new == '40.000000' accepted
    2026-09-02 19:24:27.618743 [INFO] example:Example  :: Cfg Update for 'enum_test' | old == 'one', new == 'two' accepted
    2026-09-02 19:24:27.703004 [INFO] example:Example  :: Cfg Update for 'enum_test2' | old == '2', new == '1' rejected, as always-rejects
    ...
    Config log "actual [original value]": log_interval=3 [10]; enum_test="two" ["one"]; example|current=40.000000 [42.000000]; example|enum_test="two" ["one"]; example|enum_test2="2" ["2"]
    ...

This output is generated by the Example module and therefore not available with other modules.
Note how `enum_test2` still reports its old value `2` at runtime: the module rejected the
runtime change, so the new value `1` only becomes visible after the modules are restarted.

To query a specific set of configuration parameter values use ``getp`` with the same file
(its values are ignored, only the parameter names matter):

.. code-block:: text

    everest-api> getp 0 --file cfg_update_to.yaml
    {
      "parameter_values": [
        {
          "parameter": {
            "characteristics": {
              "datatype": "Integer",
              "mutability": "ReadWrite"
            },
            "name": "log_interval",
            "value": "3"
          },
          "status": "OK"
        },
        ...
        {
          "parameter": {
            "characteristics": {
              "datatype": "Integer",
              "mutability": "ReadWrite"
            },
            "name": "enum_test2",
            "value": "2"
          },
          "status": "OK"
        }
      ],
      "status": "Success"
    }

To query a configuration parameter value that will only become active after a restart, add
``--db``, which sets ``force_read_from_db`` in the request:

.. code-block:: text

    everest-api> getp 0 --file cfg_update_to.yaml --db
    {
      "parameter_values": [
        ...
        {
          "parameter": {
            "characteristics": {
              "datatype": "Integer",
              "mutability": "ReadWrite"
            },
            "name": "enum_test2",
            "value": "1"
          },
          "status": "OK"
        }
      ],
      "status": "Success"
    }

(Notice the different value for `enum_test2`.)

Using the lifecycle API
=======================

Read-Only Usage
***************

The lifecycle API has fewer commands: ``status``, ``version``, ``stop`` and ``start``.

Stop EVerest, keep the shell running and start EVerest again with the lifecycle API activated:

.. code-block:: bash

    $ ./run-scripts/run-example.sh --lifecycle-api=ro

    ...
    2026-09-02 19:24:38.973503 [INFO] manager          :: Starting LifecycleAPI in read-only mode
    ...

The shell prints the following lines as the modules start running. The configuration
API is reported as not available, since it has not been activated:

.. code-block:: text

    everest-api>
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": true, "module_status": "NotRunning", "tstamp": "2026-09-02T17:24:38.973Z"}
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": true, "module_status": "Starting", "tstamp": "2026-09-02T17:24:38.973Z"}
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": true, "module_status": "Running", "tstamp": "2026-09-02T17:24:39.478Z"}
    everest-api>

If you add ``--configuration-api=ro`` or ``--configuration-api=rw`` when calling the run script,
``configuration_api_available`` changes accordingly.

The status topic is retained by the broker, so the latest status is also available to a client
that connects later. ``status`` shows it:

.. code-block:: text

    everest-api> status
    {
      "configuration_api_available": "N_A",
      "everest_running": true,
      "lifecycle_api_ro": true,
      "module_status": "Running",
      "tstamp": "2026-09-02T17:24:39.478Z"
    }

Read-Write Usage
****************

Stop the manager with ``Ctrl-C`` and watch the shell output:

.. code-block:: text

    everest-api>
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": true, "module_status": "Stopping", "tstamp": "2026-09-02T17:24:47.151Z"}
    [status] {"everest_running": false}
    everest-api>

When the manager shuts down it publishes this ``"everest_running": false`` status itself, right before
disconnecting from the broker. Should it die without getting that far, the broker publishes the very
same payload as the manager's MQTT LWT (Last Will and Testament), so the two cases are
indistinguishable to a client. As the messages on this topic are retained, ``status`` reports it
as well until the next manager start:

.. code-block:: text

    everest-api> status
    {
      "everest_running": false
    }

Start the manager with the ``--lifecycle-api=rw`` option set:

.. code-block:: bash

    $ ./run-scripts/run-example.sh --lifecycle-api=rw

    ...
    2026-09-02 19:24:50.390991 [INFO] manager          :: Starting LifecycleAPI in read-write mode
    ...

Now use the shell to stop the modules:

.. code-block:: text

    everest-api> stop
    {
      "status": "Stopping"
    }
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Stopping", "tstamp": "2026-09-02T17:24:56.969Z"}
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": false, "module_status": "NotRunning", "tstamp": "2026-09-02T17:24:57.021Z"}

.. note::

    The reply to the ``stop`` command only reports that the command has been accepted and the
    modules are going to stop; the completion is not part of the reply. A client that needs this
    information has to subscribe to the status topic, as the shell does: the two ``[status]``
    lines following the reply report the progress. The retained status can be queried as well
    once the modules have stopped:

    .. code-block:: text

        everest-api> status
        {
          "configuration_api_available": "N_A",
          "everest_running": true,
          "lifecycle_api_ro": false,
          "module_status": "NotRunning",
          "tstamp": "2026-09-02T17:24:57.021Z"
        }

Starting the modules again:

.. code-block:: text

    everest-api> start
    {
      "status": "Starting"
    }
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Starting", "tstamp": "2026-09-02T17:25:04.643Z"}
    [status] {"configuration_api_available": "N_A", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Running", "tstamp": "2026-09-02T17:25:05.151Z"}

Complete Workflow Example
=========================

This section shows an example workflow which starts in idle, proceeds with starting the modules after
selecting a slot and later selects a different slot and restarts the modules using that one.

Start the manager from the empty configuration with both management APIs, a fresh database file and
without trying to start the modules (``--into-idle`` option):

.. code-block:: bash

    $ rm -f /tmp/everest-workflow.db
    $ ./run-scripts/run-empty.sh --configuration-api=rw --lifecycle-api=rw --db /tmp/everest-workflow.db --into-idle

    ...
    2026-09-02 19:25:11.228496 [INFO] manager          :: Starting ConfigurationAPI in read-write mode
    2026-09-02 19:25:11.228592 [INFO] manager          :: Starting LifecycleAPI in read-write mode
    2026-09-02 19:25:11.228632 [INFO] manager          :: Manager state transition: Initializing -> Idle
    ...

The lifecycle API confirms that nothing is running yet:

.. code-block:: text

    everest-api>
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "NotRunning", "tstamp": "2026-09-02T17:25:11.228Z"}
    everest-api> status
    {
      "configuration_api_available": "RW",
      "everest_running": true,
      "lifecycle_api_ro": false,
      "module_status": "NotRunning",
      "tstamp": "2026-09-02T17:25:11.228Z"
    }

The given configuration only contains an empty set of `module configurations`. We start by loading from
a YAML file, modifying it, marking the newly created slot to be used for the start and finally starting
the modules:

.. code-block:: text

    everest-api> load --description "Example" <your-EVerest-dir>/config/config-example.yaml
    {
      "error_message": "",
      "slot_id": 1,
      "success": true
    }
    everest-api> setp 1 --file cfg_update_to.yaml
    [config_updates] {"origin": {"external": true}, "slot_id": 1, "tstamp": "2026-09-02T17:25:20.154Z", "update_results": [...]}
    {
      "results": [
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart",
        "WillApplyOnRestart"
      ]
    }
    everest-api> mark 1
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 1, "status": "Stopped", "tstamp": "2026-09-02T17:25:21.801Z"}
    {
      "result": "Success"
    }
    everest-api> start
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 1, "status": "Stopped", "tstamp": "2026-09-02T17:25:23.450Z"}
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 1, "status": "Starting", "tstamp": "2026-09-02T17:25:23.454Z"}
    {
      "status": "Starting"
    }
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Starting", "tstamp": "2026-09-02T17:25:23.454Z"}
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 1, "status": "Running", "tstamp": "2026-09-02T17:25:23.965Z"}
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Running", "tstamp": "2026-09-02T17:25:23.965Z"}

The notices show the slot being taken into use: ``active_slot_id`` switches to 1 as the modules
start, and both APIs report the modules as running half a second later.

Watch the EVerest log to see the 3 modules of the example configuration being started; the Example
module already logs the modified values, as the slot was changed before it was ever started:

.. code-block:: bash

    2026-09-02 19:25:24.046729 [INFO] example:Example  :: Config log "actual [original value]": log_interval=3 [3]; enum_test="two" ["two"]; example|current=40.000000 [40.000000]; example|enum_test="two" ["two"]; example|enum_test2="1" ["1"]

Continue with loading into a different slot and switching to it:

.. code-block:: text

    everest-api> load --description "SIL" <your-EVerest-dir>/config/config-sil.yaml
    {
      "error_message": "",
      "slot_id": 2,
      "success": true
    }
    everest-api> mark 2
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 2, "status": "Running", "tstamp": "2026-09-02T17:25:33.314Z"}
    {
      "result": "Success"
    }
    everest-api> active
    {
      "active_slot_id": 1,
      "next_boot_slot_id": 2
    }
    everest-api> stop
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 2, "status": "Stopping", "tstamp": "2026-09-02T17:25:36.592Z"}
    {
      "status": "Stopping"
    }
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Stopping", "tstamp": "2026-09-02T17:25:36.592Z"}
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 2, "status": "Stopping", "tstamp": "2026-09-02T17:25:36.642Z"}
    [active_slot] {"active_slot_id": 1, "next_boot_slot_id": 2, "status": "Stopped", "tstamp": "2026-09-02T17:25:36.644Z"}
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "NotRunning", "tstamp": "2026-09-02T17:25:36.644Z"}
    everest-api> start
    [active_slot] {"active_slot_id": 2, "next_boot_slot_id": 2, "status": "Stopped", "tstamp": "2026-09-02T17:25:44.717Z"}
    [active_slot] {"active_slot_id": 2, "next_boot_slot_id": 2, "status": "Starting", "tstamp": "2026-09-02T17:25:44.737Z"}
    {
      "status": "Starting"
    }
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Starting", "tstamp": "2026-09-02T17:25:44.737Z"}
    [active_slot] {"active_slot_id": 2, "next_boot_slot_id": 2, "status": "Running", "tstamp": "2026-09-02T17:25:46.825Z"}
    [status] {"configuration_api_available": "RW", "everest_running": true, "lifecycle_api_ro": false, "module_status": "Running", "tstamp": "2026-09-02T17:25:46.825Z"}
    everest-api> active
    {
      "active_slot_id": 2,
      "next_boot_slot_id": 2
    }

Wait for the ``NotRunning`` status after ``stop`` before issuing ``start``. Now the 17 module SIL
configuration is active. Alternatively ``start`` can be used directly, without calling ``stop``
first; the reply is then ``"Restarting"`` instead of ``"Starting"``.
