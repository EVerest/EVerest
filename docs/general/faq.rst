.. detail_faq:

.. _faq_main:

#############################################
Frequently Asked Questions And Best Practices
#############################################

This page will grow with questions from the mailing list and topics that
come up regularly in our EVerest development life. It is always a good idea
to have a look here when running into problems before asking for help via
the :ref:`mailing list <index_mailinglist>`.

EVerest modules
===============

Debug a single module
---------------------

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

Energy management
-----------------

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

Errors, warnings and Troubleshooting
====================================

.. _faq_gnu_compilers:

Compiling with GNU compilers
----------------------------
Building EVerest, you might want to use a GNU compiler. Handing over the flag
`CMAKE_CXX_COMPILER` to `cmake` lets you do that.

However, when using `gcc`, you might get errors about some
`unreferenced symbols` or linking issues.

Solution is simple: Use `g++` instead::

  cmake -D CMAKE_CXX_COMPILER=g++

`g++` will link std C++ files automatically
(`besides others <https://stackoverflow.com/a/173007/1168315>`_) which `gcc` won't do.

RPC communication timeout
-------------------------

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

EVerest OCPP 2.0.1 setup
------------------------
After successfully setting up EVerest and configured the
`libocpp module <https://github.com/EVerest/libocpp>`_, I get errors about
a failed websocket connection.

The `libocpp` module of EVerest operates - for now - as an OCPP client.
You will need to choose a backend system capable of OCPP 2.0.1 (like SteVe
for OCPP 1.6).

You may want to have a look at https://github.com/mobilityhouse/ocpp and
implement message handlers to get the communication working. Or you can have
a look at https://github.com/thoughtworks/maeve-csms. Note: This has not been
officially tested by us.

Testing
=======

.. _faq_testing:

Unit tests
----------

**How can I run the unit tests?**

To run the unit tests, you need to build with the cmake flag `-DBUILD_TESTING=ON`
and then run `make test` in the build directory.

**How do I name test targets in CMake?**

Test targets should be prefixed by the project name, to avoid conflicts when Building
libraries as dependency for other projects.
The best practice is to use the following naming scheme:

.. code-block:: cmake

  add_executable(${PROJECT_NAME}_tests)

Furthermore, the unit test should be include by the following condition:

.. code-block:: cmake

  if((${CMAKE_PROJECT_NAME} STREQUAL ${PROJECT_NAME} OR ${PROJECT_NAME}_BUILD_TESTING) AND BUILD_TESTING)
    add_executable(${PROJECT_NAME}_tests)
  endif()

This ensures that the test is only build when the project is build as a standalone project or
when the project is build as a dependency and the BUILD_TESTING flag is set and the ${PROJECT_NAME}_BUILD_TESTING flag is set.

Integration tests
-----------------

**How can I run the integration tests?**

To run the integration tests, you need to build and install everest-core:

.. code-block:: bash

  make
  make install

Then you need to install the testing tool:

.. code-block:: bash

  make install_everest_testing

Now you can run the integration tests in the source directory:

.. code-block:: bash

  cd ${SOURCE_DIR}/tests
  pytest --everest-prefix ${INSTALL_PREFIX} core_tests/*.py framework_tests/*.py
