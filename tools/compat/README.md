# EVerest Library Compatibility Checks

`check_everest_library_compat.py` compares the public API and ABI of
standalone libraries below `lib/everest/` between two git refs.

It uses only Python's standard library. The external analyzer is
`abi-compliance-checker`, which must already be installed on the system.

## Basic Usage

List discovered libraries:

```sh
python3 tools/compat/check_everest_library_compat.py --list-libraries
```

Compare one library:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old v2025.12.0 \
  --new HEAD \
  --library ocpp \
  --output compat-reports/ocpp
```

Compare several libraries:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old v2025.12.0 \
  --new HEAD \
  --library cbv2g \
  --library iso15118 \
  --library ocpp
```

Compare every discovered standalone library:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old v2025.12.0 \
  --new HEAD \
  --all
```

Rerun only the ABI/API analysis after changing descriptor logic, without
exporting refs or rebuilding:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old v2025.12.0 \
  --new HEAD \
  --library framework \
  --work-dir build-framework \
  --abi-only
```

## Build Arguments

Extra CMake arguments can be passed through:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old OLD_REF \
  --new NEW_REF \
  --library evse_security \
  --cmake-arg=-DDISABLE_EDM=ON \
  --cmake-arg=-DUSING_TPM2=OFF
```

Arguments can also be scoped to only one side:

```sh
python3 tools/compat/check_everest_library_compat.py \
  --old OLD_REF \
  --new NEW_REF \
  --library ocpp \
  --old-cmake-arg=-DSOME_OLD_ONLY_OPTION=ON \
  --new-cmake-arg=-DSOME_NEW_ONLY_OPTION=ON
```

Use the `--cmake-arg=-DNAME=VALUE` form for CMake options that start with
`-D`; otherwise the command-line parser may interpret the value as another
script option.

The script automatically adds common local EVerest package prefixes such as
`../everest-cmake` when they exist. Additional prefixes can be supplied with
`--cmake-prefix /path/to/prefix`.

The script also sets `CPM_SOURCE_CACHE` below the selected `--work-dir` so
standalone builds do not need to write to a shared workspace cache.

For standalone library builds, the script also passes source-directory cache
variables for sibling `lib/everest/*` projects, for example
`everest-sqlite_SOURCE_DIR`. Some EVerest library CMake files use these
variables to include helper CMake modules from sibling libraries.

Some libraries need sibling CMake targets when built standalone. For those,
the script creates a small wrapper project below `--work-dir/wrappers/` and
loads `everest-cmake` plus the exported repo's `cmake/ev-targets.cmake`
before adding the required sibling subdirectories. Currently this is used
for `framework`, which needs `log` and `sqlite`. The `everest_util`
dependency is provided as an interface target with the exported `util`
include directory, because adding the full `util` subdirectory would pull in
unrelated generated-type and helper-library dependencies.
It is also used for `evse_security`, which needs `log` and `timer`.

## Output

The output directory contains:

- `summary.md`: compact markdown summary suitable for PR review or CI output.
- `<library>/compat_report.html`: detailed report from `abi-compliance-checker`.
- `<library>/abi-compliance-checker.log`: analyzer log.
- `<library>/old.xml` and `<library>/new.xml`: generated ABI descriptor inputs.

The script returns non-zero if any selected library reports incompatibility
or if the analyzer/build fails.

## Important Notes

- The script exports refs with `git archive`. It does not modify the current
  worktree, but it also does not include uncommitted changes.
- Source/API compatibility is the signal for consumers that recompile against
  the new headers.
- Binary/ABI compatibility is meaningful only when both refs build shared
  library artifacts and expose comparable public headers.
- Header-only or static-only libraries may produce useful source/API results
  but no meaningful binary/ABI percentage.
- The generated descriptor compares the selected library's public headers.
  Installed third-party headers and CPM dependency headers are added as
  `search_headers` so the selected headers compile without treating
  dependencies as part of the library API.
- The analyzer creates an ABI dump for each side first, then compares the
  two dumps. This avoids mixing old and new include search paths while
  compiling headers.
- The script enables discovered `*INSTALL*` CMake options for each library
  so installed headers and libraries are preferred for analysis. If install
  fails, it falls back to source headers and build artifacts where possible.
