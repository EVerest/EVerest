# EVerest: Agent Guidelines

Guidance for AI coding agents working in `EVerest`, the mono-repository formerly named
`everest-core`: runtime manager, modules, shared libraries, interface definitions, test
infrastructure. C++ primary, plus Python, Rust and JavaScript. CMake with Ninja; CI also
builds with Bazel.

Libraries that once lived in separate `lib*` repositories are now in-tree under
`lib/everest/` and are edited in place; there is no upstream repository to mirror them
to. A few EVerest components are still external dependencies in `dependencies.yaml`,
notably the Python Josev stack (`ext-switchev-iso15118`).

Contributor policy (licensing, DCO, review, and the project's position on AI-generated
contributions) is in `docs/source/project/contributing.rst`. This file covers mechanics
only: build, run, test, code generation, and invariants.

## Documentation

Prefer project docs over your own assumptions, and cite them. When docs and the tree
disagree, the tree wins: manifests, `interfaces/*.yaml` and code describe current
behavior, docs describe intent. Say so when you find the two diverging.

Docs are rendered at <https://everest.github.io>, starting with the
[Getting Started Guides](https://everest.github.io/nightly/how-to-guides/getting-started/index.html).
Sphinx sources are under `docs/source/`: `explanation/` for concepts, `how-to-guides/`
for recipes. Per-module docs are at `modules/<Category>/<Module>/docs/index.rst`; update
them when a module's public behavior changes.

## Build

`make build`, `make test` and `make lint` do not exist here.

```bash
cmake -S . -B build -GNinja \
  -DCMAKE_INSTALL_PREFIX=./build/dist \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_TESTING=ON \
  -DEVEREST_ENABLE_COMPILE_WARNINGS=ON
ninja -C build install -j$(nproc)
```

Dependencies are pinned in `dependencies.yaml` and fetched by edm at configure time;
`-DDISABLE_EDM=ON` uses system packages. `ISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES`
defaults to ON, so development certificates need no extra flag.

To iterate faster, build one module (`ninja -C build OCPP201`) or skip heavy ones with
`-DEVEREST_EXCLUDE_MODULES="EvseSlac;EvseV2G;IsoMux"`, quoting the list. `cmake -LH build`
lists all options; the two easiest to miss are `EVEREST_ENABLE_COVERAGE` and
`CMAKE_RUN_CLANG_TIDY`, both OFF.

## Running

Generated run scripts are the simplest entry point:

```bash
ls build/run-scripts/                 # every generated configuration
build/run-scripts/run-sil.sh          # AC software-in-the-loop
```

Each wraps the manager with the environment it needs (template
`cmake/assets/run_template.sh.in`). By hand, set the same three things:

```bash
LD_LIBRARY_PATH=<prefix>/<libdir>:$LD_LIBRARY_PATH \
  ./build/dist/bin/manager --prefix <prefix> --config <full path to config yaml>
```

`<libdir>` is `lib` or `lib64`, following `CMAKE_INSTALL_LIBDIR`. `--config` also takes
an extensionless name, looked up in the installed config directory. `--check` validates
a config and exits 0 on success.

Log verbosity is per module in the deployed
`build/dist/share/everest/modules/<Module>/logging.ini` (template
`cmake/assets/logging.ini`). Severities: DEBG, INFO, WARN, ERRO, CRIT.

## Testing

```bash
cd build && ctest --output-on-failure
cd build && ctest -N                  # list registered names before filtering
cd build && ctest -R <regex>
```

`ctest -R` matches **registered test names, not build targets**, and the two often differ
in case and spelling. List with `ctest -N` first. Coverage needs
`-DEVEREST_ENABLE_COVERAGE=ON`; the `everest-core_create_coverage` target writes
`build/everest-core_create_coverage/index.html`.

Integration tests use pytest; the virtualenv is `build/venv`:

```bash
. build/venv/bin/activate
cmake --build build --target install_everest_testing
cd tests && pytest --everest-prefix ../build/dist core_tests/ framework_tests/
```

OCPP tests also need certificate and per-EVSE component-config fixtures in the prefix.
`tests/run-tests.sh` handles this; calling pytest directly means re-running the fixture
scripts after any `ninja install` that touches the prefix. From the repo root:

```bash
cmake --build build --target everestpy_pip_install_dist
cmake --build build --target everest-testing_pip_install_dist
cmake --build build --target iso15118_pip_install_dist
(cd tests/ocpp_tests/test_sets/everest-aux && \
  ./install_certs.sh ../../../../build/dist && \
  ./install_configs.sh ../../../../build/dist)
cd tests/ocpp_tests && pytest --everest-prefix ../../build/dist test_sets/ocpp201/ -v
```

ISO 15118 has no dedicated suite. It is covered by `tests/core_tests/smoke_tests.py`
(AC and DC end to end), CSMS-side helpers in
`tests/ocpp_tests/test_sets/validations.py`, and `modules/EVSE/EvseV2G/tests/`.

## Architecture

Each module is an independent process. It declares the interfaces it provides and
requires in `manifest.yaml`, is wired to other modules by a configuration YAML under
`config/`, and communicates only over MQTT through `everest-framework`. Contracts are
the source of truth for wiring: read `interfaces/*.yaml`, `types/*.yaml` and
`errors/*.yaml` before the C++. Lifecycle is `init()` then `ready()`, implemented per
provided interface in `*Impl.cpp`. Generated headers land under `build/generated/`.

```cpp
mod->r_evse_manager->subscribe_session_event(...);   // required interface, init() ok
mod->p_main->publish_ready(true);                    // provided interface
auto result = mod->r_auth->call_validate_token(tok); // command: ready() or later only
```

In a configuration YAML, a `connections` entry maps a requirement ID from the consuming
module's manifest to a `module_id` plus that module's `implementation_id`.

## Code generation with ev-cli

`ev-cli` turns `manifest.yaml`, `interfaces/*.yaml` and `types/*.yaml` into C++
scaffolding. It lives in-tree at `applications/utils/ev-dev-tools/`, installs into the
build venv, and templates are under `src/ev_cli/templates/`.

A file is ev-cli-managed if it carries `// ev@<uuid>:v1` markers. Content between paired
markers is yours; everything else is regenerated. This includes files that look
hand-written, notably `modules/<Category>/<Name>/<Name>.hpp`, which holds `struct Conf`.
Adding a config key to a manifest means regenerating, not editing the struct.

```bash
. build/venv/bin/activate
ev-cli mod update <Category>/<Name> --work-dir . --build-dir build --disable-clang-format -f
cmake -S . -B build     # codegen for build/generated/ runs at configure time
```

Traps:

- The module argument is a path relative to `modules/`, so `EVSE/EvseManager`, not a
  bare `EvseManager`. A bare name fails with `Could not open type definition file`.
- Regeneration is decided by mtime, not content. A target newer than its manifest is
  skipped with `Skipping <name> (up-to-date)`, so a recent edit makes ev-cli silently
  under-generate. Preview with `-d`, force with `-f`, and confirm the change landed.
- `--disable-clang-format` (used above): ev-cli formats with whatever clang-format is on
  `PATH`, not the version CI uses. Format afterwards (see Code style).
  `--clang-format-file` takes a directory, not a file.
- Editing `types/*.yaml` or `interfaces/*.yaml` fails `everest-core_API_serialize_tests`
  until the sha256 pins in
  `lib/everest/everest_api_types/tests/expected_{types,interfaces}_file_hashes.csv`
  are updated.

`ev-cli --help` lists the available actions; `--only which` lists the files an action
would touch.

## Code style

Full C++ conventions: `docs/source/how-to-guides/c++-coding-guidelines.rst`.

- clang-format, LLVM base, 4-space indent, 120 columns (`.clang-format`). CI enforces it
  over `.hpp` and `.cpp` via `.github/workflows/job_lint.yml`. Format changed files with
  the version CI uses:

  ```bash
  git diff --name-only --diff-filter=ACMR origin/main -- '*.cpp' '*.hpp' | \
    docker run --rm -i -v "$PWD:/source" -w /source \
      ghcr.io/everest/everest-ci/build-env-base:v1.6.0 xargs clang-format -i
  ```

  The tag tracks `ref_everest_ci` in `.github/workflows/on_pr.yaml`. A local clang-format
  also works, but versions differ in output; the container matches CI.
- PascalCase for classes and structs, `snake_case` for functions and methods,
  `m_`-prefixed `snake_case` for member variables, `snake_case` for library file names.
  No camelCase.
- `#pragma once` for include guards.
- `EVLOG_debug`, `EVLOG_info`, `EVLOG_warning`, `EVLOG_error` for logging.
- Return values and `std::optional` over exceptions for expected conditions.
- JavaScript and YAML: prettier, 2-space indent, single quotes (`.prettierrc.yaml`).

## Paths that surprise people

| Thing | Path | Note |
|---|---|---|
| OCPP 1.6 module | `modules/EVSE/OCPP/` | not `OCPP16/` |
| OCPP 2.0.1 and 2.1 | `modules/EVSE/OCPP201/` | one module serves both versions |
| API module | `modules/API/API/` | directory name is doubled |
| libocpp | `lib/everest/ocpp/` | in-tree |

## Invariants

- Never remove `// ev@<uuid>:v1` markers from `*Impl.cpp` or `CMakeLists.txt`.
- Never hand-edit ev-cli-managed content outside your own marker-delimited regions,
  `struct Conf` included. Regenerate instead.
- Never edit anything under `build/`, including `build/generated/`. It is overwritten.
- Never use `std::cout` or `fprintf` for logging.
- Never call `mod->r_*->call_*()` inside `init()`. Command calls belong in `ready()` or
  later.
- Never skip formatting before proposing C++ changes. CI fails on it, see Code style.

## Contributing essentials

Full policy: `docs/source/project/contributing.rst`. The items agents get wrong most
often:

- Sign off every commit (`Signed-off-by`, DCO), enforced by
  `.github/workflows/job_dco-check.yaml`.
- New files need copyright and license headers.
- While review is open, do not rebase or force-push, so reviewers can see that feedback
  was addressed. Squashing to a single commit once approved is how changes land.
- Every contribution must be reviewed and understood by a human before submission.

Commit subjects and pull request titles follow Conventional Commits and name the affected
module, for example `fix(EvseManager): handle unplug during timed charging`. Changes land
as GitHub squash merges, so the pull request title becomes the commit subject and the
description becomes its body.
