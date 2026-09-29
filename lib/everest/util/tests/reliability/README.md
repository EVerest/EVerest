<!-- SPDX-License-Identifier: Apache-2.0 -->
<!-- Copyright 2026 Contributors to EVerest -->

# Thread pool scaling reliability tests

`thread_pool_scaling_reliability_tests.cpp` keeps the bounded, normal cases short enough for pull-request runs.
It checks exactly-once delivery with unbounded and bounded queues, worker handoff and nested dependencies,
retirement and regrowth, future values and exceptions, shutdown draining, workerless restarts, timed-pop and
reentrant shutdown races, dependency chains, and single-worker callback serialization. The shutdown dependency
case also uses `LatencyScaling<50, 5>`, the framework's default timing policy.

The GTest case beginning `DISABLED_` is a selectable extended lifecycle run. Set
`EVEREST_THREAD_POOL_STRESS_ROUNDS` (default `100`) and `EVEREST_THREAD_POOL_STRESS_SEED` (default `9826`) to make
its workload reproducible. Run every normal case repeatedly as well as the disabled extended case with:

```bash
timeout --kill-after=10s 600s env EVEREST_THREAD_POOL_STRESS_ROUNDS=1000 EVEREST_THREAD_POOL_STRESS_SEED=9826 \
  build/AGENT/lib/everest/util/tests/reliability/everest_thread_pool_scaling_reliability_tests \
  --gtest_repeat=20 --gtest_also_run_disabled_tests
```

The Linux-only fault probes run from a subprocess driver with a five-second deadline per child. A nonzero child
exit, signal, or timeout fails the GTest; the documented bounded reentrant queue limit is the only expected
nonzero result and must exit `3`. The default matrix is short and serialized under CTest to avoid host thread
contention. Its disabled extended matrix adds larger drain-failure counts and bounded chaos/chain variants:

```bash
timeout --kill-after=10s 600s env EVEREST_THREAD_POOL_PROBE_STRESS=1 \
  build/AGENT/lib/everest/util/tests/reliability/everest_thread_pool_scaling_probe_driver \
  --gtest_filter='ThreadPoolScalingProbeDriver.DISABLED_ExtendedMatrix' --gtest_also_run_disabled_tests
```

The fault probe binaries intentionally interpose allocation and pthread creation in separate processes. Do not run
them in an AddressSanitizer or ThreadSanitizer build; use the sanitizer commands below for the normal reliability
target instead.

For an offline, focused CMake build that uses the checked-in utility header and the cached GoogleTest source:

```bash
cmake -S lib/everest/util/tests/reliability -B build/AGENT/util-reliability \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DEVEREST_GTEST_SOURCE_DIR=/path/to/googletest
cmake --build build/AGENT/util-reliability --parallel <jobs>
ctest --test-dir build/AGENT/util-reliability --output-on-failure
```

Build the same focused suite with sanitizers by adding `-DEVEREST_UTIL_RELIABILITY_FAULT_PROBES=OFF` and either
flag set to the configure command. Build the normal target and run only its registered tests; it has no allocation
or pthread symbol overrides.

```bash
-DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'

-DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer' \
-DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread'
```

```bash
cmake --build build/AGENT/util-reliability --target everest_thread_pool_scaling_reliability_tests --parallel <jobs>
ctest --test-dir build/AGENT/util-reliability -R ThreadPoolScalingReliability --output-on-failure
```

The repository build registers the normal suite as `everest_thread_pool_scaling_reliability_tests`; Bazel registers
the same source as `//lib/everest/util:thread_pool_scaling_reliability_tests` and the Linux subprocess matrix as
`//lib/everest/util:thread_pool_scaling_probe_driver`.
