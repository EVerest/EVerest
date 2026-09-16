# RsEvseManager

A Rust implementation of [EvseManager](../EvseManager) that replaces it rather
than sitting beside it: the same five provided interfaces and the same config
keys. `docs/index.rst` says what it is and how a config selects it.

## Building

The module is behind `EVEREST_ENABLE_RS_SUPPORT`, which defaults to `OFF`
(`lib/everest/framework/CMakeLists.txt`). Without it CMake never reaches
`ev_add_module(RsEvseManager)` in `modules/EVSE/CMakeLists.txt`, so the module
does not build and no target for it exists.

```bash
cmake -S . -B build -G Ninja -DEVEREST_ENABLE_RS_SUPPORT=ON
ninja -C build rust_workspace/RsEvseManager
```

**No CI job sets that option.** Nothing under `.github/` names it or this module,
so CI never compiles, tests or lints anything here. The gates below are the only
check this module has, and they run only where someone runs them.

That ninja target only symlinks the module into the cargo workspace, so it
reports "no work to do" after a source edit; `ninja -t commands` names the
`cargo build` that does the work. The repository's `AGENTS.md` carries that and
the rest of the build sharp edges.

## Tests

Configured with `-DBUILD_TESTING=ON`, the package's tests are registered as one
ctest:

```bash
ctest --test-dir build -R RsEvseManager_unit_tests
```

## Gates

`scripts/` holds five checks. Each script's header comment says what it covers
and what it deliberately does not; read those rather than inferring, because the
coverage boundaries between them are not obvious.

| script | what it checks |
| --- | --- |
| `test-core.sh` | `core` and `boundary` standalone, without CMake or the framework. Strips `src/main.rs`, so it is blind to the boundary. |
| `check-boundary.sh` | Compiles `src/main.rs`. The only gate that compiles the boundary, and it compiles the production half only. |
| `test-boundary.sh` | Runs the tests inside `src/main.rs`. Needs an installed framework under `build/dist`. |
| `reachability.py` | Censuses surface that exists but is unreachable from production, against `scripts/reachability-baseline.txt`. A ratchet, not a proof. |
| `unconstructable.py` | Requires the compiler to refuse each construction the module's absent-collaborator invariants prohibit. |

## Running it

`config/config-sil-rs-evse-manager.yaml` (AC) and
`config/config-sil-dc-rs-evse-manager.yaml` (DC) are the stock SIL configs with
the `EvseManager` module type substituted for this one, which is all selecting it
takes. Their run scripts are generated into `build/run-scripts/`. `tests/README.md`
covers running the SIL and HLC suites, including the network namespace runner
needed for ISO 15118 without root.

## Documentation

| document | what it answers |
| --- | --- |
| `docs/index.rst` | What the module is and how it is selected. This is what the rendered reference page includes. |
| `docs/architecture.md` | The layering, the effect model, the charge modes, and every deliberate divergence from and preserved defect of the C++ module, each with the C++ site it was read from. |
| `docs/effect-ordering-timing.md` | Which effects are ordered against which, the measured framework command timeout, and why the stale-actuation window has no finite bound. |
| `docs/shutdown-contract-scope.md` | The bounded options for closing that window, and why none of them is in this module. |
