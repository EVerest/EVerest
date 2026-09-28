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

## Single-process mode (proof of concept)

With `-DEVEREST_BUILD_MODULE_PLUGINS=ON` every C++ module is built as a shared object
(`libexec/everest/modules/<Name>/<Name>.so`) and the executable `everest-neo` runs a whole
configuration in one process. It reuses `ManagerConfig` to resolve the connection graph and loads
each module through the exported `everest_module_entry` symbol (`framework/module_plugin.hpp`).
Each hosted module is driven by an `Everest::LocalModule` (`framework/local_module.hpp`), which
owns no transport and no dispatch threads:

- variables, commands and errors pass as C++ objects over `Everest::LocalBus`
  (`framework/local_bus.hpp`): one shared adaptive thread pool, in-order delivery per
  subscription, commands executed on the caller's thread;
- configuration reads, writes and runtime change notifications go through
  `Everest::config::LocalConfigService` straight to the config service core;
- `ready()` runs on a thread of its own per module, shutdown handlers are called directly;
- external MQTT and telemetry use one connection to a real broker, when one is reachable.

A watchdog in the bus warns about subscriber callbacks or command calls that block for more
than five seconds, and `SIGUSR1` logs the state of every subscription. Python and Rust modules
are not supported in this mode.
