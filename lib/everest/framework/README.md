# EVerest Framework 

This subproject of EVerest is providing a mechanism to manage dependencies between different modules communicating with an wrapped MQTT protocol. On startup it parses a set of configuration file, checks them agains the manifests of different modules and launches each module needed.

Additional documentation can be found in [docs](docs).

The framework message handler thread pool scaling policy is selected at CMake
configure time with `EVEREST_FRAMEWORK_THREAD_POOL_SCALING_POLICY`. Supported
values are `latency` (default), `greedy`, `conservative`, `fixed_size` and
`custom`. The latency and fixed-size policies have additional CMake options for
their thresholds. See the main EVerest documentation under
`docs/source/explanation/dev-tools/edm.rst` for full build examples and the
custom policy interface.
