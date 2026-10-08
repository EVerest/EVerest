.. _exp_dev_tools_management_api_cli:

##########################
everest-management-api-cli
##########################

``everest-management-api-cli`` is a command line client for the
:ref:`management APIs <exp_management_apis>` of the EVerest manager: the
*lifecycle API* (start and stop the modules, observe the module status) and
the *configuration API* (manage configuration slots and parameters).

It is a small Python package living in ``applications/utils/everest-management-api-cli``.
Its client modules are also what the integration tests in ``tests/management_api_tests``
use to drive the APIs, so the tests cover the same code path as the command line.
It offers an interactive shell that also prints status updates and configuration
notices as they arrive, and every command can be run one-shot as well, which
makes it usable from scripts.

For a hands-on walkthrough see the :ref:`management APIs tutorial <tutorial_management_apis>`.

*******
Install
*******

Building EVerest with testing enabled (``-DBUILD_TESTING=ON``, see the
:ref:`Quick Start Guide <htg_getting_started_sw>`) installs the client into the
Python venv of the build directory, next to ``everest-testing``:

.. code-block:: bash

    cd build/
    source ./venv/bin/activate
    everest-management-api-cli --help

To install it into any other Python environment:

.. code-block:: bash

    pip install -e applications/utils/everest-management-api-cli

The only dependencies are ``paho-mqtt`` and ``pyyaml``.

*****
Usage
*****

The quickest way to explore the APIs is the interactive shell:

.. code-block:: text

    $ everest-management-api-cli shell
    EVerest management API shell. Type 'help' for commands, 'quit' to leave.
    Connected to 127.0.0.1:1883; watching everest_api/1/lifecycle/e2m/status,
    everest_api/1/configuration/e2m/active_slot and everest_api/1/configuration/e2m/config_updates.
    Status updates and notices are printed as they arrive.
    everest-api> active
    {
      "active_slot_id": 0,
      "next_boot_slot_id": 0
    }
    everest-api> mark 1
    [active_slot] {"active_slot_id": 0, "next_boot_slot_id": 1, "status": "Running", "tstamp": "2026-09-02T17:24:10.937Z"}
    {
      "result": "Success"
    }

The shell keeps one connection open. Besides answering the commands typed at the prompt it
prints lifecycle status updates and the ``active_slot`` / ``config_updates`` notices of the
configuration API as ``[status]``, ``[active_slot]`` and ``[config_updates]`` lines the moment
they arrive. Since the notices are not retained by the broker, start the shell before the
manager to see the ones published at boot. ``help [COMMAND]`` shows the commands and their
options, ``timeout [SECONDS]`` shows or sets the reply timeout, ``quit`` leaves the shell.

For scripts every command is also available as a one-shot invocation that connects, sends
one request, prints the reply and exits:

.. code-block:: text

    everest-management-api-cli [--host HOST] [--port PORT] [--timeout SECONDS] [--compact] COMMAND ...

The broker defaults to ``127.0.0.1:1883`` or, if set, to the ``MQTT_SERVER_ADDRESS``
and ``MQTT_SERVER_PORT`` environment variables the manager itself uses.
Replies are printed as JSON; ``--compact`` prints one line per reply, which is
convenient for ``jq``. The commands are the same in both modes:

.. list-table::
    :header-rows: 1
    :widths: 35 65

    * - Command
      - API request
    * - ``status [--all]``
      - Latest lifecycle status (the topic is retained, so this works whenever a manager has published one)
    * - ``version``
      - ``get_everest_version``
    * - ``start`` / ``stop``
      - ``start_modules`` / ``stop_modules``
    * - ``slots`` / ``active``
      - ``list_all_slots`` / ``get_active_slot``
    * - ``mark SLOT`` / ``delete SLOT``
      - ``mark_active_slot`` / ``delete_slot``
    * - ``dup SLOT [-d DESCRIPTION]``
      - ``duplicate_slot``
    * - ``desc SLOT TEXT...``
      - ``set_description``
    * - ``load FILE [-d DESCRIPTION] [--slot SLOT]``
      - ``load_from_yaml`` (a full config file is reduced to its ``active_modules``)
    * - ``config SLOT [--db]``
      - ``get_configuration``
    * - ``getp SLOT MODULE PARAMETER [--impl IMPL] [--db]``
      - ``get_config_parameters`` for one parameter
    * - ``getp SLOT --file FILE [--db]``
      - ``get_config_parameters`` for every parameter listed in the file
    * - ``setp SLOT MODULE PARAMETER VALUE [--impl IMPL]``
      - ``set_config_parameters`` for one parameter
    * - ``setp SLOT --file FILE``
      - ``set_config_parameters`` for every parameter in the file
    * - ``watch [--duration SECONDS]``
      - Print status updates and configuration notices as they arrive (the one-shot counterpart of the shell's live output)
    * - ``raw API COMMAND [JSON]``
      - Send an arbitrary request, e.g. to test malformed payloads
    * - ``clear-retained-status``
      - Remove the retained last-will status of a killed manager from the broker

One-shot exit codes are ``0`` when a reply was received (a reply such as ``Rejected``
is a valid API answer), ``1`` on a timeout, connection failure or invalid
local input, and ``2`` on usage errors.

Parameter files
===============

``setp --file`` and ``getp --file`` take a YAML file in the layout of a module
configuration, reduced to the parameters of interest. ``getp`` ignores the
values:

.. code-block:: yaml

    active_modules:
      example:                  # module id
        config_module:          # module-level parameters
          log_interval: 3
        config_implementation:
          example:              # implementation id
            current: 40
