# EVerest tests

This folder contains the basic test functions for EVerest's e2e testing. You can either extend the '*\*_tests.py*' files in the "EVerest/**tests/core_tests**" folder or write your own!

## Prerequisites

In order to run any of the e2e tests, you need to have EVerest compiled
and installed on your system. Please also make sure to install the python
requirements.

```bash
cd EVerest/
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --target install --parallel -j$(nproc)
. build/venv/bin/activate
cmake --build build --target everestpy_pip_install_dist # install everestpy
cmake --build build --target everest-testing_pip_install_dist # install everest-testing
cmake --build build --target iso15118_pip_install_dist # install iso15118 for ev side simulation
python3 -m pip install "aiofile>=3.7.4"
python3 -m pip install "netifaces>=0.11.0"
```

## Execute locally

All test suites are run via the unified `run-tests.sh` script:

```bash
cd ~/checkout/everest-workspace/EVerest/tests

./run-tests.sh all          # all tests (core, framework, async, OCPP)
./run-tests.sh core         # core tests only
./run-tests.sh framework    # framework tests only
./run-tests.sh asyncapi        # async API tests only
./run-tests.sh integration  # core + framework + async
./run-tests.sh ocpp         # all OCPP tests (1.6, 2.0.1, 2.1)
./run-tests.sh --serial all # run serially (no parallelism)
./run-tests.sh -j4 all      # limit to 4 parallel workers
./run-tests.sh --help       # show all options
```

The script automatically sets up network isolation for parallel ISO 15118 tests
(requires sudo or CAP_NET_ADMIN) and falls back to sequential execution otherwise.

After execution `result.xml` and `report.html` are written to the `tests/` directory.

### ISO 15118 and HLC tests need the right network interface

`EvseV2G` and the EV simulator are configured with `device: auto`, which binds
the **first interface that carries an IPv6 link-local address**
(`choose_first_ipv6_interface` in `modules/EVSE/EvseV2G/tools.cpp`). On a
developer machine that is whatever comes first in the interface list, typically
`wlan0` or `eth0`; the EV then never discovers the SECC over it and the session
dies in SDP (`SDPFailedError`, repeated `TimeoutError` after an `SDPRequest`).
With no link-local interface at all, the SECC does not start in the first place.
Both end the same way: every ISO 15118 test fails.

**Neither failure looks like a network problem.** The test failure is a
downstream assertion, typically `assert False` on a `StartTransaction`, and the
cause appears only as an `EvseV2G` error buried in the EVerest log, for example:

```
Device auto: Failed to initialize connection: No IPv6 link-local interface found
```

One measured sweep lost 41 tests to this, all of them ISO 15118, against the
same commit and the same binary as a run that passed them. Read from a test
report alone it looks like roughly 40 charging regressions rather than a
networking problem, so check the interface before blaming a branch:

```bash
ip -o -6 addr show scope link | head -1   # this is the device "auto" will pick
```

`run-tests.sh` prints the same interface on startup.

Where a working interface normally comes from: `setup-network-isolation.sh`
brings up the `ev_test*` veth pairs and the kernel gives each one a link-local
address. It needs root or `CAP_NET_ADMIN`. Without either, use
`run-in-netns.sh`.

### Running ISO 15118 and HLC tests without root

`run-in-netns.sh` runs a command inside a private network namespace whose only
link-local interface is a `v2g0` veth pair, so `device: auto` can only resolve
to the V2G link. It also starts an MQTT broker on port 1883 inside the
namespace. It needs no root and touches nothing outside the namespace:

```bash
cd ~/checkout/everest-workspace/EVerest/tests

./run-in-netns.sh ./run-tests.sh ocpp16
./run-in-netns.sh ./run-tests.sh ocpp
./run-in-netns.sh python3 -m pytest core_tests/smoke_tests.py::test_iso15118_ac_session
```

It requires `unshare`, `ip` and `mosquitto` on `PATH` and permitted unprivileged
user namespaces, and it names whichever of those is missing instead of letting
the suite fail confusingly. It exits 70 when it cannot build the namespace, so a
harness failure stays distinguishable from a test failure.

### Running individual tests directly

For a single test file or test case, invoke `pytest` directly from the `tests/` directory:

```bash
cd ~/checkout/everest-workspace/EVerest/tests

# Run a single test file
python3 -m pytest core_tests/smoke_tests.py

# Run a single test by name
python3 -m pytest core_tests/smoke_tests.py::test_iso15118_ac_session
```

Network isolation is not set up automatically in this mode — ISO 15118 tests will
run sequentially via their `xdist_group` marker.

## Add own test sets

To create own test sets, you need to write a new python file in the "EVerest/**tests/core_tests/**" folder. A basic file template would look like this:

```python
import logging
import pytest

from everest.testing.core_utils.fixtures import *
from validations.base_functions import wait_for_and_validate_event
from everest.testing.core_utils.test_control_module import TestControlModule
from everest.testing.core_utils.everest_core import EverestCore
from validations.user_functions import *

@ pytest.mark.asyncio
async def test_001_my_first_test(everest_core: EverestCore, 
                                 test_control_module: TestControlModule):
    logging.info(">>>>>>>>> test_001_my_first_test <<<<<<<<<")
    everest_core.start()

    assert await wait_for_and_validate_event(test_control_module, 
                                             exp_event='my_event', 
                                             exp_data={"my_data":"test"},
                                             validation_function=my_validation_function,
                                             timeout=30) 
```

\*Note: All test functions' names NEED to start with "test" (see pytest documentation: https://docs.pytest.org/)

In the above example you then would also have to write a (user-)validation function by the name of "my_validation_function()" in the "EVerest/**tests/core_tests/validations/user_functions.py**" file.

For this, you can utilize helper functions from the "EVerest/**tests/core_tests/validations/base_functions.py**" file (e.g. "get_key_if_exists()" to traverse and retrieve event data from an event-object).

**Attention**: When you change something in the test- or validation files, please do not forget to run "*make install*" on EVerest before starting the tests, as otherwise the changes are not reflected in the test run!

### Controlling more functionality with PyTestControlModule

If you would like to receive other everest-internal data, send commands to other EVerest modules or would like to change the received events for everest-testing, you can modify the **PyTestControlModule** in the "EVerest/**modules/PyTestControlModule**" folder.

**Attention:** Please be aware, though, that the use of this module is preliminary and changes in the *everest-framework* may render this module **obsolete** in the future!

## Troubleshooting

**1. Problem:** Multiple concatenated tests that use the *PyTestControlModule* (in standalone configuration) result in an exception and subsequent crash of everest-testing.

**Cause:** There is currently no way to unload a module in *EVerest*. But because pytest runs out of the same context for all tests in a testset, a once loaded *PyTestControlModule* stays active for the rest of the pytest/everest-testing run. When a new test tries to load a second instance of the *PyTestControlModule* it collides with the already running instance and causes everest-testing to crash.

**Solution:** Unfortunately, this requires a change in the everest-framework. This change is currently under development, but not yet merged into main. 
For the moment, there is a *workaround available:* Use a new test-set (another python file containing a new set of tests) for every test that requires control via the *PyTestControlModule*. As soon as a fix for the underlying issue is merged, this will be updated!

**2. Problem:** On an unmodified run of "basic_charging_tests.py" the test *test_001_charge_defined_ammount* fails and reports an amount of 0.0kWh charged.

**Cause:** Currently there is a bug in the EVSE manager implementation in EVerest, causing a race-condition between resetting the "amount charged" parameter and the event reporting for a "*transaction_finished*" event. Sometimes it reports correctly, other times it reports a value of 0.0kWh charged.

**Solution:** This problem is known and a fix is under way. For the moment, just re-run the test. Usually, this should then produce a passing result.


# Code coverage

To activate code coverage, you need to configure the project with
`BUILD_TESTING` enabled.  Furthermore, `gcovr` needs to be installed.

## Generating coverage reports

Before generating a coverage report you will typically need to run some
executables or tests, which contain or link to code, that generates coverage
statistics.

After that, just run:
```bash
cmake --build build --target everest-core_create_coverage
# or ninja -C build everest-core_create_coverage
```

The generated `index.html` will be available at
`build/everest-core_create_coverage/index.html`.

**Note**: the code coverage for a specific compilation unit (cpp file) will
accumulate if you run multiple executables that use this compilation unit.  The
code coverage is recorded in `gcda` files.  If you want to get separate coverage
reports for different executables, these `gcda` files need to be deleted for
each new *run*.


## Adding code/targets to code coverage

By default, all *cpp* modules are added automatically to a list of targets that
will be compiled with coverage flags.  If additional libraries or tests need to
be added to code coverage, you should use the following functions in your
*cmake* code:
```cmake
ev_register_library_target(LIBRARY_TARGET_NAME)
ev_register_test_target(TEST_TARGET_NAME)
```

## Conventions

For the `ev_register_*_target` functions, call these function close to their
scope of detail - meaning near `add_library` for libraries like:

```cmake
add_library(evse_security_conversions STATIC)
add_library(everest::evse_security_conversions ALIAS evse_security_conversions)
ev_register_library_target(evse_security_conversions)
```

and near `add_test` for tests like:

```cmake
add_test(${TLS_GTEST_NAME} ${TLS_GTEST_NAME})
ev_register_test_target(${TLS_GTEST_NAME})
```

and not scattered around.

---

By default, all directories ending with `tests` will be excluded from the code
coverage report (because we're not interested in the code coverage of the unit
test code itself).  So when adding unit tests for code coverage, place them into
a `tests` folder.

## Troubleshooting

`no_working_dir_found`: while developing code (e.g. an `example.cpp`), it might
happen, that the created object file `example.cpp.o` will get out of sync with
its corresponding `example.cpp.gcno` (`gcov` note file) and/or
`example.cpp.gcda` (`gcov` data file).  When creating a report, `gcovr` will
complain, because it can't find the corresponding `example.cpp.o` file for a
stale `example.cpp.gcno` file.  It might also not be able to find the
`example.cpp.gcno` file for a stale `example.cpp.gcda` file or an older `gcda`
file might got out of sync due to a newer `gcno` file and so forth.  Do not
simply ignore these warnings/errors, as the report might not be accurate
anymore.

The current sledgehammer approach is to manually delete the files in question or
just do (**be careful** with `rm` commands and wildcards!):
```bash
find ./build -name "*.gcno" -delete
find ./build -name "*.gcda" -delete
ninja -C build clean
ninja -C build
# run code
# create coverage
```

We're currently trying to write some tooling around this to make this more
usable.
