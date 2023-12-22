.. everest_testing_framework:

.. _testing_framework_main:

#############################
The EVerest testing framework
#############################

********
Overview
********

A dedicated testing framework based on Python / pytest has been and is developed to support testing of
- individual EVerest modules,
- the integration of several EVerest modules together, as well as
- full simulation stacks (e.g. against third-party backends)

The testing framework can be found and is documented in https://github.com/EVerest/everest-utils/tree/main/everest-testing

It provides
- essential tooling to start a sandboxed EVerest instance
- tooling to simply customize configurations for each test
- tooling to verify test results
- tooling for "mocking", i.e. replacement of parts of EVerest by mock-ups.

An exemplary test may look like this::

    @pytest.mark.everest_core_config('everest-config-sil-iso.yaml')
    @pytest.mark.asyncio
    @pytest.mark.probe_module(connections={'auth_con': [Requirement('auth', 'main')]})
    async def test_set_connection_timeout(everest_core: EverestCore,
                                          probe_module: ProbeModule):
        res = await probe_module.call_command("auth_con", "set_connection_timeout", 42)
        assert res is None
        # ...


Here, we want to test the Auth module. We load a specified Everest configuration and the test (better-said the `everest_core` "dependency" takes care of this)
and then use our "mock up" Probe Module to send an EVerest command to another module. After that (not in this snippet), we
could ensure that desired side-effects of this command have happened.


************
Requirements & Installation
************

In order to run the tests, you need:
1. A build of EVerest
2. A environment in which EVerest can be started. This is, a running MQTT broker.
3. A python environment with the everestpy and everest-testing  and required dependencies installed

For steps one and two, please see the quick-start guide!


TODO: detailed guide for step 3 How to install everestpy (framework) and everest-testing (utils), and
other prerequisites (Python packages, pytest extensions, etc.)

*************
Pytest basics
*************

Pytest is a powerful and yet simple testing framework for python.

For a nice and thorough introduction, see the official documentation at https://docs.pytest.org/

The EVerest framework heavily exploits the following concepts:
- _Fixtures_: Those provide a sort of "dependency injections". This allows to simply inject and execute "preconfigured" code. For example, the `everest_core` fixture
  starts an EVerest instance with a specified configuration. The framework provides some basic fixtures. In particular, overriding those allows deeper test customization (for details, see the everest-testing documentation).
- _Markers_: Markers provide a quick way to do simple modifications before the test. For example, the `everest_core_config` marker specifies which configuration
  should be loaded before the test is executed.
- Temporary files: All configuration files and resources are processed and copied to a temporary directories. This allows to analyze those even after the test has finished.
(Running pytest with the flag `--basetemp=<DIR>` set let's you specify where those files are stored>


In general, a test is run with::

    python -m pytest [--basetemp=<target dir>] --everest-prefix=<Path to dist folder of Everest build, e.g. everest-core/build/dist> <test_file.py>

TODO: Explain missing concepts of: conftest, test files/test methods
and naming conventions ("test_" prefix). Provide links to pytest docs where appropriate

******************************************
Example: Running a SIL charging simulation
******************************************
TODO: Show an example test that runs the SIL simulation

************************
The probe module
************************
TODO: Explain how the probe module works and how to use it

TODO: Explain what the magic probe module is and how to use it

*****************
Debugging modules
*****************
TODO: Explain how to run a module under a debugger while testing
(make it standalone, start it under a debugger with the temp config file path)

*****************************
Reference: Important fixtures
*****************************
TODO: Explain the most commonly used fixtures in everest-testing core-utils and ocpp-utils

****************************
Reference: Important markers
****************************
TODO: Explain the most commonly used markers in everest-testing core-utils and ocpp-utils

*******************
Overriding fixtures
*******************
TODO: (briefly) explain how fixture overriding works in pytest, and give examples for why
it is useful

****************************
Config adjustment strategies
****************************
TODO: Explain what config adjustment strategies are and how everest-testing adjusts the
runtime config step by step. Explain how to create and add your own config adjustments.

******************************
OCPP testing: Mocking the CSMS
******************************
TODO: Explain how to use the ChargePoint from everest-testing to mock an OCPP CSMS

**********************
Testing best practices
**********************
TODO: List some best practices when testing, e.g. making sure that tests only use
temporary files, that they can run in parallel with other tests, etc.
