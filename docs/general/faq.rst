.. detail_faq:

.. _faq_main:

#############################################
Frequently Asked Questions And Best Practices
#############################################

This page will grow with questions from the mailing list and topics that
come up regularly in our EVerest development life. It is always a good idea
to have a look here when running into problems before asking for help via
the :ref:`mailing list <index_mailinglist>`.

Debug a single module
=====================

**How can I debug a single module?**

It is possible to use the GNU Debugger (GDB) to debug a single EVerest module.

The easiest way is to run the module in standalone mode. Say, for example, you
want to debug the Auth module for the SIL config (config-sil.yaml).

Let's assume you are in directory *build/dist*.

Start the manager with

.. code-block:: bash

  ./bin/manager --config config-sil --standalone auth

This will start EVerest with the config-sil.yaml as configuration, but it
won't start the Auth module (note *auth* is written small because it is the
*module instance id* - this way there can be multiple Auth module instances
in your config).

Now you need to start the Auth module manual using gdb. When using
Visual Studio Code, the debug configuration (launch.json) looks like this:

.. code-block:: bash

  {
    "version": "0.2.0",
    "configurations": [
        {
            "name": "AuthManager",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}/build/dist/libexec/everest/modules/Auth/Auth",
            "args": ["--config", "config-sil", "--module", "auth"],
            "stopAtEntry": false,
            "cwd": "/workspace/everest-core",
            "environment": [],
            "externalConsole": false,
            "MIMode": "gdb",
            "setupCommands": [
                {
                    "description": "Enable pretty-printing for gdb",
                    "text": "-enable-pretty-printing",
                    "ignoreFailures": true
                },
                {
                    "description": "Set Disassembly Flavor to Intel",
                    "text": "-gdb-set disassembly-flavor intel",
                    "ignoreFailures": true
                }
            ]
        },
    ]
  }

This will then start the Auth module instance.

Also note the argument *--module auth*, which again specifies the module
instance id and needs to match the one you've used for standalone.

Now, EVerest will continue to start and breakpoints set in the source file of
the Auth module should be taken.

Furthermore and very important, don't forget to compile the project with
debugging enabled. One easy way to achieve this is to call

.. code-block:: bash

  cmake -B build -DCMAKE_BUILD_TYPE=Debug

from the root folder of everest-core, assuming you have already created the
*build* directory.

.. note::

  It is also possible to debug the whole manager process. This way, you will
  have the disadvantage of possibly bad performance. The reason is that the
  manager spawns/forks new processes, which then need to be attached to the
  debugger too.

  Just in case you want to go this direction, you need to
  "set detach-on-fork off" and "follow-fork-mode" depending on what you want to
  achieve.

RPC communication timeout
=========================

**In the Admin Panel, I sometimes get the following error when saving a config
file:**

.. code-block::

  Failed to save test_config Reason: RPC communication timeout to everest
  controller process.

**How can I solve this?**

In this case, the Admin Panel timeouted while waiting for the response of the
EVerest process trying to save the file.

The timeout is currently 2s.

The problem with your setup might be that running EVerest as well as running
an UI session with a browser on one hardware is just too much for it. This
can sometimes happen on Raspberry Pies, for example.

You may try to connect from a desktop PC to IP_OF_THE_RASPBERRY:8849. This way,
the client-side processing of the Admin Panel javascript code gets offloaded
from the Raspberry and it might be able to process the save faster.

Another hint for environments with very limited ressources is to fill in the
workspace information into the yaml config manually without using the Admin
Panel.

Energy management
=================

**How does the EVSE Manager use information like `grid_connection_point`
(with parameters `fuse_limit_A` and `phase_count`) or `energy_manager`
(e.g. `nominal_ac_voltage`?**

The EVSE Manager module defaults to 0A/0W energy consumption and requires that
some other module allocates power through the `energy` interface.

The `energy manager` just supplies whatever the limit on the power path is,
and the fuse module is just loaded to model the typical input fuse limit of
the charger connection (so the energy manager will limit to that even if the
car is requesting more).

As a visualization, imagine the energy management in EVerest as a tree
structure. What makes energy management in EVerest quite flexible is the
concept of having constraints and limits you can provide to it. For each node
in that tree, limits can be provided which are recognized by the energy
manager which calculates the energy that is to be provided by the EVSE
managers.
