# Project agent memory

- For isolated HLC tests, start with `tests/setup-network-isolation.sh`.
  Creating only a network namespace leaves the existing sysfs mount showing
  host interfaces; compare `/proc/net/dev` with `/sys/class/net` when a module
  cannot read a test interface's MAC address.

- `--network-isolation` only binds an interface under pytest-xdist. The plugin
  assigns it from `pytest_configure_node`, an xdist controller hook, so a
  serial run leaves `EVEREST_TEST_NETWORK_INTERFACE` unset, the strategy in
  `core_utils/fixtures.py` is never injected, and `device: auto` silently
  picks a host NIC. Set that variable and its `_PROXY_` partner by hand for
  serial HLC runs. SDP is IPv6 multicast to `ff02::1`, so verify the chosen
  interface loops multicast back before trusting a run: unicast crossing a
  veth pair does not imply multicast does.

- The Lint Repository job runs clang-format from the everest-ci
  `build-env-base` image, not your local one: v1.5.4 carries 15.0.6, and
  versions reflow differently, so a file your own clang-format calls clean can
  still fail the job. Reproduce in that image. Scope the sweep to tracked
  `*.hpp`/`*.cpp`; a bare recursive run also walks the gitignored `build/`
  tree and buries the real hits under vendored headers.

- A red Lint or Build-with-CMake job leaves the Integration and OCPP suites
  `skipped`, not failed, so no e2e result is visible until both are green.
  Read a "skipped" e2e gate as "never ran", never as "nothing to report".

## Maintaining this file

Keep this file for knowledge useful to almost every future agent session in this project.
Do not repeat what the codebase already shows; point to the authoritative file or command instead.
Prefer rewriting or pruning existing entries over appending new ones.
When updating this file, preserve this bar for all agents and keep entries concise.
