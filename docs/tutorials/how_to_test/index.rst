.. tutorial_tests:

***********************
How To: Test in EVerest
***********************

This is a tutorial on how to setup your test environment for EVerest.

Requirements
============

A successful build of *everest-core* is required. Refer to
`everest-core <https://github.com/EVerest/everest-core/>`_ for this.

Some test cases require the installation of *everestpy*.
You can do this after a successful build of *everest-core*:

.. code-block:: bash

    cd everest-core
    cmake --build build --target everestpy_pip_install_dist

Sometimes the ``everstpy`` module might require manual installation.
That can be done by running:

.. code-block:: bash

    cd everest-framework/everestpy
    python3 -m pip install .

Install the python package containing the EVerest test utilities from
`everest-testing <https://github.com/EVerest/everest-utils/tree/main/everest-testing/>`_. 

Install the requirements of this repository:

.. code-block:: bash

    python3 -m pip install -r requirements.txt

Some test cases require the installation of *Josev*.
This should be located in your EVerest workspace:

.. code-block:: bash

    cd <path-to-josev>
    python3 -m pip install -r requirements.txt
    python3 -m pip install .

For most test cases you need a correct setup of certificates and configs within
EVerest. You can use

.. code-block:: bash

    cd test_sets/ocpp_tests/everest_aux
    ./install_certs.sh <path/to/everest-core>
    ./install_configs.sh <path/to/everest-core>

to install the certificates within *everest-aux* into the correct location of
*everest-core*.

An MQTT server is needed for testing.
Docker can be used for this.
Refer to
`docker-setup <https://everest.github.io/nightly/tutorials/docker_setup.html>`_
for this.

Test sets
=========

Run any test set (e.g. ocpp_compliance_tests.py) with:

.. code-block:: bash

    python3 -m pytest test_sets/ocpp_tests/ocpp16/ocpp_compliance_tests.py --everest-prefix <path-to-everest-core>/build/dist --libocpp <path-to-libocpp>

or run a single test case with:

.. code-block:: bash

    python3 -m pytest test_sets/ocpp_tests/ocpp16/ocpp_compliance_tests.py --everest-prefix <path-to-everest-core>/build/dist/ --libocpp <path-to-libocpp> -k 'test_remote_start_first' -s

or run all tests in parallel with:

.. code-block:: bash

    ./run-testing.sh

If running from the ``everest-core/tests/ocpp_tests`` directory, the path to
*everest-core* or *libocpp* can be relative, for example:

.. code-block:: bash

    --everest-prefix ../../../everest-core/build/dist --libocpp ../../../libocpp/

.. note::

    Here is a TODO: We have to update the documentation for known failing
    tests.

View OCPP test logs
===================

While running the tests, EVerest logs OCPP message to its log directory.
These logs are stored in HTML files.

When you open the following directory in your web browser you can view to logs:

.. code-block:: bash

    /tmp/everest_ocpp_test_logs/

VS Code Debugging
=================

Debugging can have various layers depending on the feature tested.
This part will focus on debugging inside VS Code.

Python debugging
----------------

Tests can be manually launched by adding the proper entries to the
'launch.json' file.
Example for debugging a test from ``ocpp_compliance_tests.py``:

.. code-block:: json

    "configurations": [
        {
            "name": "Python: OCPP Compliance Test",
            "type": "debugpy",
            "request": "launch",
            "module": "pytest",            
            "args": [                
                "test_sets/ocpp_tests/ocpp16/ocpp_compliance_tests.py",
                "--libocpp", "../libocpp/",
                "--everest-prefix", "../everest-core/build/dist",                
                "-s", "-vv", "-k", "your_test_here",
            ],
            "cwd": "${workspaceFolder}/ocpp-testing/",
            "console": "integratedTerminal",
            "justMyCode":false
        }
    ]

The paths can differ based on the workspace setup.

.. tutorial_tests_cpp_debug_attach:

C++ debugging
-------------

When a certain test case executes, there is a chance that C++ code can be
faulty, requiring a GDB attach in order to detect an issue.
In that case, the following steps can be followed:

- setup for python debugging
- setup for c++ debugging with the following config entry:

.. code-block:: json

    "configurations": [
        {
            "name": "(gdb) Attach PID",
            "type": "cppdbg",
            "request": "attach",
            "program": "${workspaceFolder}/everest-core/build/dist/bin/manager",
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
        }
    ]

- build *everest-core* with debugging enabled: ``cmake .. -DCMAKE_BUILD_TYPE=Debug``
- run the desired test from python debugging (```Python: OCPP Compliance Test```) and place a breakpoint near the test's entry point
- copy the PID from the variable 'test_controller: TestController' from the test's function: ``test_controller->_everest_core->process->pid``
- run ```pstree -pT ${pid}``` with the retrieved pid for example 102590:

.. code-block:: bash

    pstree -pT 102590
    manager(102590)─┬─auth:Auth(102653)
                    ├─car_simulator:J(102654)
                    ├─connector_1:Evs(102655)
                    ├─controller(102595)
                    ├─energy_manager:(102656)
                    ├─evse_security:E(102657)
                    ├─grid_connection(102658)
                    ├─ocpp:OCPP(102660)
                    ├─python3(102659)
                    ├─slac:JsSlacSimu(102661)
                    ├─system:System(102662)
                    ├─token_provider_(102663)
                    └─yeti_driver:JsY(102664)

- while the test is in the breakpoint, run the `(gdb) Attach PID` configuration with the desired module to attach to
- in the VS code terminal prompt input 'y' and insert the root password
- unpause the python `Python: OCPP Compliance Test` debug session

External integration
====================

The main motive for external integration is the flexibility of running
EVerest outside of the SIL environment.
Therefore, the *ocpp-tests* can be run with any out-of-tree versions of
EVerest.

Currently, there are different versions of EVerest that should be able to run
*ocpp-tests*:

- SIL
- BaseCamp

The ``test_sets`` folder can be embedded in any external repository that uses
EVerest.
The external EVerest does not have to include all the components required by
the SIL version.

Due to the limitations of *pytest*, the
`conftest.py <https://docs.pytest.org/en/latest/reference/fixtures.html#conftest-py-sharing-fixtures-across-multiple-files>`_
file must not be included, in order to preserve the needs of the external project.

In order to use the proper fixtures for the ``[test_sets](test_sets)`` inside an
external repository, a custom `conftest.py` specific for that project has to
provide all the necessary fixtures for running the tests.

Example fixture
---------------

The `test_F01_F02_F03`` requires the following fixtures:

.. code-block:: python

    async def test_F01_F02_F03(charge_point_v201: ChargePoint201, test_controller: TestController, test_utility: TestUtility):

The project-specific `conftest.py` must provide the proper fixtures that are
custom for the project - in our case the `test_controller` fixture:

.. code-block:: python

    # Add necessary handling in here
    class ExternalTestControllerAdapter(TestController):
        def __init__(self):
            pass

        def start_thread(self):        
            pass

        def stop_thread(self):
            pass    

        def start(self):
            pass

        def stop(self):
            pass

        def plug_in(self, connector_id=1):
            pass

        def plug_in_ac_iso(self, payment_type, connector_id):
            raise NotImplementedError()

        def plug_out(self, connector_id=1):
            pass

        async def swipe_async(self, token):
            pass

    @pytest.fixture
    def test_controller(everest_core: EverestCore):
        controller = ExternalTestControllerAdapter(
            everest_core        
        )

        controller.start()    
        yield controller
        controller.stop()

Example marker injection
------------------------

Sometimes the tests do not have all the required
`pytest markers <https://docs.pytest.org/en/latest/example/markers.html#marking-test-functions-and-selecting-them-for-a-run>`_.
The test ``test_F01_F02_F03`` might require additional markers in the context
of an external repo.

That can be achieved at runtime using
`pytest hooks <https://docs.pytest.org/en/latest/how-to/writing_hook_functions.html>`_.

For example if the test might require the 

.. code-block:: python

    pytest.mark.use_temporary_persistent_store
    pytest.mark.ocpp_config(Path("path-to-config"))

markers that are not present in the test header.
They can be injected by adding the following code to the `conftest.py` of the
specific external project:

.. code-block:: python

    def pytest_collection_modifyitems(session, config, items):    
        marks = (        
            pytest.mark.use_temporary_persistent_store,
            pytest.mark.ocpp_config(Path("path-to-config"))
        )

        for item in items:
            if "ocpp_testing" in item.path.as_posix():
                for marker in marks:
                    item.add_marker(marker)

        pass

The result is that before running each selected test under ``test_sets``, the
markers will be applied, modifying the default behavior of the tests.

Required mocks
==============

In order to properly run the tests, certain mocks have to be implemented.
Different versions of EVerest might require different mock implementations,
that might include but not be limited to:

- charge_point_v16
- charge_point_v201
- test_controller
- ocpp_test_mocks
- test_utility
