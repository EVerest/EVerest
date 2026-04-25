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
their individual configuration parameters. Module configurations are stored in a database file
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

Using the configuration API with the CLI Client
===============================================

This sections uses the CLI clients which are part of EVerest.
Consider running some MQTT monitoring in parallel to observe the sent and received frames.

The management APIs are always part of a build, but they need to be activated when starting EVerest.

Don't forget to run ``cmake --install .`` in the ``build`` folder.

Read-Only Usage
***************

In order to activate the configuration API, append the *manager* command line options as shown below
(this will work with every run-script):

.. code-block:: bash

    $ ./run-scripts/run-example.sh --configuration-api=ro

    ...
    2026-05-15 10:55:11.708968 [INFO] manager          :: Starting ConfigurationAPI in read-only mode
    ...
    2026-05-15 10:55:12.808325 [INFO] example:Example  :: Config log "actual [original value]": log_interval=10 [10]; enum_test="one" ["one"]; example|current=42.000000 [42.000000]; example|enum_test="one" ["one"]; example|enum_test2="2" ["2"]

Observe the manager logging that the configuration API has been started.
Also note, that the Example module is periodically logging its current configuration.

For now we started the API in read-only mode.
Prepare another terminal and change your working directory into the same build folder.
From there run the CLI API client:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli
    Usage: everest-config-api-cli [options] <command> [args...]
    
    Global options:
      -h [ --help ]                   This help message
      --host arg (=localhost)         MQTT broker host
      --port arg (=1883)              MQTT broker port
      --api-name arg (=configuration) API name to form the MQTT topic (e.g., 
                                      configuration)
      --command arg                   Command to execute
      --cmd_args arg                  Arguments for command
    
    Commands:
      list_slots
      show_slot_metadata <slot_id>
      get_active_slot
      mark_active_slot <slot_id>
      delete_slot <no>
      duplicate_slot <no> [<description>]
      load_yaml <filename>[--description <desc>] [--slot-id <slot_id>]
      set_description <slot_id> <description>
      get_configuration <slot_id>
      set_config_parameters <slot_id> <filename>
      get_config_parameters <slot_id> <filename>
      monitor [--suppress-parameter-updates]

This will show how it is used. Now query the API for the list of configuration slots and the currently active slot.

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli list_slots
    Available slots:
      [0] <no description>
    $ ./dist/bin/everest-config-api-cli get_active_slot
    Active slot: [0]
    Next boot slot: [0]

We find, there is only a single slot existing (0) and it is used as  the *active slot*
(the one which is used *if* EVerest is running right now) and also as the one to be
used for the next restart of the manager process.

There is also some metadata available for a configuration slot:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli show_slot_metadata 0
    Slot metadata:
      Slot ID      : 0
      Description  : <no description>
      Last Updated : 2026-05-15T11:41:24.354Z
      Config File  : /home/user/workspace/EVerest/config/config-example.yaml

The full configuration stored in a slot can be retrieved.

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli get_configuration 0
    {
    "active_modules": [
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
    ...

.. important::

    Only the module configuration is stored and retrievable. Thus, in generel it's not
    possible to reconstruct the yaml file which may have been used to initially load this
    module configuration into the database, as it also contains the manager settings.

A client can also monitor the configuration API for updates. E.g. start the client as
follows and then stop EVerest and start it again:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli monitor
    Starting monitor... (Press Ctrl+C to stop)
    2026-05-15T12:47:54.373Z [Active Slot Update] Active slot is now: 0 "Stopping"; Next boot slot: 0
    2026-05-15T12:48:57.201Z [Active Slot Update] Active slot is now: 0 "Stopped"; Next boot slot: 0
    2026-05-15T12:48:57.201Z [Active Slot Update] Active slot is now: 0 "Starting"; Next boot slot: 0
    2026-05-15T12:48:57.792Z [Active Slot Update] Active slot is now: 0 "Running"; Next boot slot: 0

Read-write Usage
****************

Starting the manager with ``rw`` enables write access to the configuration API:

.. code-block:: bash

    $ ./run-scripts/run-example.sh --configuration-api=rw

    ...
    2026-05-15 13:55:11.708968 [INFO] manager          :: Starting ConfigurationAPI in read-write mode
    ...

Now we can load module configurations from a YAML file and subsequently make sure a new slot has been created:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli load_yaml --description "SIL setup" <your-EVerest-dir>/config/config-sil.yaml
    Successfully loaded YAML to slot 1 with description: SIL setup
    $ ./dist/bin/everest-config-api-cli list_slots
    Available slots:
      [0] <no description>
      [1] SIL setup
    $ ./dist/bin/everest-config-api-cli show_slot_metadata 1
    Slot metadata:
      Slot ID      : 1
      Description  : SIL setup
      Last Updated : 2026-05-16T08:14:06.722Z
      Config File  : <no config file>

To reboot from this slot the next time the modules start:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli mark_active_slot 1
    Successfully marked slot 1 as active.
    $ ./dist/bin/everest-config-api-cli get_active_slot
    Active slot: [0]
    Next boot slot: [1]

As you can see, the `get_active_slot` command now returns separate results for the currently active slot
and the one that would be used if a reboot would happen now.

.. note::

    If you try to stop the manager process and start it again in order to activate the new
    configuration, you will observe no change in the loaded configuration. This is because of
    the ``--reset-from-yaml`` CLI option the run-scripts contain by default. You can remove
    it from the script file and will get the expected result.

A different way to populate a new slot is to duplicate an existing one:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli duplicate_slot 1 "Created as duplicate of slot 1"
    Successfully duplicated to slot 2 with description: Created as duplicate of slot 1
    $ ./dist/bin/everest-config-api-cli list_slots
    Available slots:
      [0] <no description>
      [1] SIL setup
      [2] Created as duplicate of slot 1

Deleting an unused slot is possible as well, but not for the active slot:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli delete_slot 2
    Successfully deleted slot 2.
    $ ./dist/bin/everest-config-api-cli delete_slot 1
    Failed to delete slot 1

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

    The configuration API CLI client will only send parameters to the configuration API
    that differ from the current settings.

To test this, first make sure you restarted the manager with slot `0` and delete all others:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli mark_active_slot 0
    # restart the manager now
    $ ./dist/bin/everest-config-api-cli get_active_slot
    Active slot: [0]
    Next boot slot: [0]
    $ ./dist/bin/everest-config-api-cli delete_slot 1
    Successfully deleted slot 1.

Now create a duplicate from the existing slot `0` and modify the newly created configuration.

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli duplicate_slot 0 "Created as duplicate of slot 0"
    Successfully duplicated to slot 1 with description: Created as duplicate of slot 0
    $ ./dist/bin/everest-config-api-cli set_config_parameters 1 cfg_update_to.yaml
    example|log_interval                               ["Integer"] : 10 -> 3  "WillApplyOnRestart"
    example|enum_test                                  ["String"] : one -> two  "WillApplyOnRestart"
    example|example|current                            ["Decimal"] : 42.000000 -> 40  "WillApplyOnRestart"
    example|example|enum_test                          ["String"] : one -> two  "WillApplyOnRestart"
    example|example|enum_test2                         ["Integer"] : 2 -> 1  "WillApplyOnRestart"
    
    Changed 5 of 5 parameter(s), 0 applied.

As the example output shows, the API reports type, old vs. new value and if and when the
change becomes effective. Also take notice, that all changes will only become active on
the next restart. This is because slot `1` is not active.

Requesting the same changes to the active slot gives different results:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli set_config_parameters 0 cfg_update_to.yaml
    example|log_interval                               ["Integer"] : 10 -> 3  "Applied"
    example|enum_test                                  ["String"] : one -> two  "Applied"
    example|example|current                            ["Decimal"] : 42.000000 -> 40  "Applied"
    example|example|enum_test                          ["String"] : one -> two  "Applied"
    example|example|enum_test2                         ["Integer"] : 2 -> 1  "Rejected"
    
    Changed 4 of 5 parameter(s), 4 applied.

Changes to module configuration parameters can also be monitored. If a monitoring instance
of the CLI client was running while executing the `set_config_parameters` command as shown
above, it would show for the last command:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli monitor
    Starting monitor... (Press Ctrl+C to stop)
    ...
    [Parameter Update] slot_id: 0 [origin: external:unknown]
    example|!module|log_interval -> 3 ("Applied")
    example|!module|enum_test -> two ("Applied")
    example|example|current -> 40 ("Applied")
    example|example|enum_test -> two ("Applied")

You will also be able to observe this in the EVerest logs:

.. code-block:: bash

    ...
    Config log "actual [original value]": log_interval=10 [10]; enum_test="one" ["one"]; example|current=42.000000 [42.000000]; example|enum_test="one" ["one"]; example|enum_test2="2" ["2"]
    ...
    2026-05-15 14:20:07.605870 [INFO] example:Example  :: Cfg Update for 'log_interval' | old == '3', new == '3' accepted
    2026-05-15 14:20:07.716455 [INFO] example:Example  :: Cfg Update for 'enum_test' | old == 'two', new == 'two' accepted
    2026-05-15 14:20:07.772711 [INFO] example:Example  :: Cfg Update for 'current' | old == '40.000000', new == '40.000000' accepted
    2026-05-15 14:20:07.869164 [INFO] example:Example  :: Cfg Update for 'enum_test' | old == 'two', new == 'two' accepted
    2026-05-15 14:20:07.965126 [INFO] example:Example  :: Cfg Update for 'enum_test2' | old == '2', new == '1' rejected, as always-rejects
    ...
    Config log "actual [original value]": log_interval=3 [10]; enum_test="two" ["one"]; example|current=40.000000 [42.000000]; example|enum_test="two" ["one"]; example|enum_test2="2" ["2"]
    ...

This output is generated by the Example module and therefore not available with other modules.

Using the lifecycle API with the CLI Client
===========================================

Read-Only Usage
***************

The lifecycle CLI client has less options:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli 
    Usage: everest-lifecycle-api-cli [options] <command> [args...]
    
    Global options:
      -h [ --help ]               This help message
      --host arg (=localhost)     MQTT broker host
      --port arg (=1883)          MQTT broker port
      --api-name arg (=lifecycle) API name to form the MQTT topic (e.g., lifecycle)
      --command arg               Command to execute
      --cmd_args arg              Arguments for command
    
    Commands:
      stop_modules
      start_modules
      get_everest_version
      monitor

First start it in monitoring mode:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli monitor

In order to activate the lifecycle API when starting EVerest, append the *manager* command
line options as shown below (this will work with every run-script):

.. code-block:: bash

    $ ./run-scripts/run-example.sh --lifecycle-api=ro

    ...
    2026-05-16 12:55:11.708968 [INFO] manager          :: Starting LifecycleAPI in read-only mode
    ...

The client will print out the following line, as the modules started running, and the configuration
API is not available:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli monitor
    Monitoring lifecycle status. Press Ctrl+C to stop.
    ------------------------------------------------------------------------------------------------------------------------
    Timestamp                     Module Status       EVerest Running     Config API                    Lifecycle API       
    ------------------------------------------------------------------------------------------------------------------------
    2026-05-15T14:31:21.536Z      "NotRunning"        true                "N_A"                         RO
    2026-05-15T14:31:21.541Z      "Starting"          true                "N_A"                         RO
    2026-05-15T14:31:22.336Z      "Running"           true                "N_A"                         RO

If you add ``--configuration-api=ro/rw`` when calling the run script the output for the `Config API`
column changes accordingly.

Read-Write Usage
****************

Keep the monitoring CLI client running (or restart as above).

Stop the manager and watch the client output:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli monitor
    Monitoring lifecycle status. Press Ctrl+C to stop.
    ------------------------------------------------------------------------------------------------------------------------
    Timestamp                     Module Status       EVerest Running     Config API                    Lifecycle API       
    ------------------------------------------------------------------------------------------------------------------------
    ...
    2026-05-15T14:33:21.336Z      "Running"           true                "N_A"                         RO
    2026-05-15T14:34:55.442Z      "Stopping"          true                "N_A"                         RO
    <no-tstamp>                                       false

The MQTT LWT (Last Will and Testament) is used to set "EVerest Running" to `false`. As the messages on this
topic are retained, they will also be available if the client is only started now.

Start the manager with the ``--lifecycle-api=rw`` option set:

.. code-block:: bash

    $ ./run-scripts/run-example.sh --lifecycle-api=rw
    
    . . .
    2026-05-16 12:55:11.708968 [INFO] manager          :: Starting LifecycleAPI in read-write mode
    . . .

Now use the client to stop the modules:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli stop_modules
    Sending stop_modules request...
    Stop modules result:
      Status: "Stopping"

.. note::

    The client instance which issued the `stop_modules` command only reports, that the command
    has been accepted and the modules are going to stop, but it will not report when this has
    completed. If a client needs this information, it must subscribe to the same topic which is
    used by the `monitor` mode of the CLI client. Observe the log of the monitoring API client
    from above.

Starting the modules again:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli start_modules
    Sending start_modules request...
    Start modules result:
      Status: "Starting"

A monitoring session during these commands will look similar to this:

.. code-block:: bash

    $ ./dist/bin/everest-lifecycle-api-cli monitor
    Monitoring lifecycle status. Press Ctrl+C to stop.
    ------------------------------------------------------------------------------------------------------------------------
    Timestamp                     Module Status       EVerest Running     Config API                    Lifecycle API       
    ------------------------------------------------------------------------------------------------------------------------
    ...
    2026-05-15T14:41:04.072Z      "Running"           true                "N_A"                         RW
    2026-05-15T14:43:01.069Z      "Stopping"          true                "N_A"                         RW
    2026-05-15T14:43:01.110Z      "NotRunning"        true                "N_A"                         RW
    2026-05-15T14:43:25.349Z      "Starting"          true                "N_A"                         RW
    2026-05-15T14:43:25.957Z      "Running"           true                "N_A"                         RW
    ...

Complete Workflow Example
=========================

This section shows an example workflow which start in idle, proceedes with starting the modules after
selecting a slot and later selecting a different slot and restarting the modules using that one.

As the auto-generated run-scripts will always contain the ``--reset-from-yaml`` option by default,
you should create a version without this option:

.. code-block:: bash

    $ cat run-scripts/run-empty.sh | grep -v reset-from-yaml > run-scripts/run-empty_no_reset.sh
    $ chmod +x run-scripts/run-empty_no_reset.sh

Also remove the existing configuration database:

.. code-block:: bash

    $ rm dist/everest.db

Start the manager with both management APIs and without trying to start the modules (``--into-idle`` option):

.. code-block:: bash

    $ ./run-scripts/run-empty_no_reset.sh --configuration-api=rw --lifecycle-api=rw --into-idle

    ...
    2026-05-16 12:55:11.708968 [INFO] manager          :: Starting LifecycleAPI in read-write mode
    2026-05-16 12:55:11.709102 [INFO] manager          :: Starting ConfigurationAPI in read-write mode
    2026-05-16 12:55:11.709704 [INFO] manager          :: Manager state transition: Initializing -> Idle
    ...

The given configuration only contains an empty set of `module configurations`. We start by loading from
a YAML file, modifying it, marking the newly created slot to be used for the start and finally starting the modules:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli load_yaml --description "Example" <your-EVerest-dir>/config/config-example.yaml
    Successfully loaded YAML to slot 1 with description: Example
    $ ./dist/bin/everest-config-api-cli set_config_parameters 1 cfg_update_to.yaml
    example|log_interval                               ["Integer"] : 10 -> 3  "WillApplyOnRestart"
    example|enum_test                                  ["String"] : one -> two  "WillApplyOnRestart"
    example|example|current                            ["Decimal"] : 42.000000 -> 40  "WillApplyOnRestart"
    example|example|enum_test                          ["String"] : one -> two  "WillApplyOnRestart"
    example|example|enum_test2                         ["Integer"] : 2 -> 1  "WillApplyOnRestart"
    
    Changed 5 of 5 parameter(s), 0 applied.
    $ ./dist/bin/everest-config-api-cli mark_active_slot 1
    Successfully marked slot 1 as active.
    $ ./dist/bin/everest-lifecycle-api-cli start_modules
    Sending start_modules request...
    Start modules result:
      Status: "Starting"

Watch the EVerest log to see when 3 modules are started. Continue with loading into a different slot and switching to it:

.. code-block:: bash

    $ ./dist/bin/everest-config-api-cli load_yaml --description "SIL" <your-EVerest-dir>/config/config-sil.yaml
    Successfully loaded YAML to slot 2 with description: SIL
    $ ./dist/bin/everest-config-api-cli mark_active_slot 2
    Successfully marked slot 2 as active.
    $ ./dist/bin/everest-lifecycle-api-cli stop_modules
    Sending stop_modules request...
    Stop modules result:
    Status: "Stopping"
    # wait until the manager is in state "idle"
    $ ./dist/bin/everest-lifecycle-api-cli start_modules
    Sending start_modules request...
    Start modules result:
    Status: "Starting"

Now the 17 module SIL configuration is active. Alternatively ``start_modules`` can be
used directly, without calling ``stop_modules`` first.
