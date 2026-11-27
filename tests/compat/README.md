# Compatibility tests

Checks that a change to EVerest keeps the public API contract defined in
`docs/source/project/releases/releases-and-versioning.rst`.

The contract diff in `contract/` compares the public contract of two git refs statically: AsyncAPI
specs, JSON RPC API, module and config contracts, OCPP configuration files, SQL migrations, the
manager's command line options and the schemas of its configuration and settings files, classified
by `breaking-changes.rst`. It is described for integrators in
`docs/source/how-to-guides/compatibility-testing.rst`.

## Where the checks are enforced

Development happens on main, towards the next release line. A release line is maintained on
`stable/<line>`, where every release after `<line>.0` is a patch release. The rules differ:

| Event | Contract diff |
|---|---|
| PR and merge queue to main | against the merge base; only changes to what the latest release ships count, with the exceptions its registry documents |
| scheduled run on main | also against the latest release tag |
| PR and merge queue to `stable/<line>` | against the merge base and against the latest release of the line, as a patch release: only experimental components may change |

The comparison of main with the release tag does not run on pull requests. A release is cut from
its stable branch, so the tag can hold a change that main does not have yet, and the comparison
reports it as removed whatever the pull request changes. It describes the branch: the scheduled
run shows what still has to be forward-ported or announced before the next release.

A stable branch whose line has no release yet is still development towards that release and is
checked like main. A workflow runs from the branch it targets, so a stable branch enforces these
checks once it carries `tests/compat` and the workflow.

The tree states the release it becomes as its CMake project version: a
deprecated component may only be removed from the release its deprecation row names as the earliest
removal. A removal on a tree whose version is still the released one therefore fails; the version
is raised first.

## Contract diff

```bash
pip install pyyaml                       # the only dependency
python3 tests/compat/contract/compat_contract.py check --base 2026.10.0 --tree .
python3 tests/compat/contract/compat_contract.py check --base origin/main --tree . --released latest-release
python3 tests/compat/contract/compat_contract.py check --base 2026.10.0 --head stable/2026.10 --patch-release
python3 tests/compat/contract/compat_contract.py baseline --target-branch stable/2026.10
python3 tests/compat/contract/compat_contract.py snapshot --ref 2026.02.1 -o old.json
python3 tests/compat/contract/compat_contract.py diff old.json new.json --show-non-breaking
```

`snapshot.py` reduces a tree (or a ref, via `git archive`) to one JSON document, `rules.py` classifies
the differences as breaking or non-breaking, and `registry.py` accepts the breaking ones that a
release documents as deprecated or experimental. Exit code 1 means that a breaking or unclassified
change remains. Unclassified is a difference in a construct that no rule knows, for example an
unknown schema keyword; it is resolved by teaching `rules.py` the construct. `baseline` prints the
release tag a branch is compared with (`release.py`).

Findings are addressed by pointer, for example
`asyncapi/ocpp_consumer_API/channels/receive_is_connected`,
`modules/EvseManager/config/connector_id/default`, `manager/options/conf` or
`framework/config/properties/settings`.

### Deprecated and experimental components

A release records them in `docs/source/project/releases/deprecations.yaml` and `experimental.yaml`,
which the index pages of the documentation render. An entry names the parts of the contract it
applies to under `covers`, as pointer globs; a path includes everything below it:

```yaml
- id: ocpp-module
  component: ...
  deprecated_in: 2026.10.0
  earliest_removal: 2027.04.0
  migration: ...
  covers:
    - modules/OCPP
```

The contract diff reads these files **from the release**, not from the tree: from the baseline when
that is a release tag, from the release named by `--released <tag>` when the baseline is a merge
base. The tree cannot decide: it deletes the entry of a removed component with the component, and
an entry it adds announces nothing to deployments that run the release.

- A change to an experimental component is accepted in any release.
- A change to a deprecated component is accepted once the release the tree becomes has reached the
  entry's `earliest_removal`. That release is the CMake project version of the tree, or
  `--target-release 2027.04.0`.
- With `--patch-release`, the rule of a stable branch, only experimental components may change.
- A release without the files accepts nothing.

The report lists a finding that an entry covers without accepting it, with the reason.
`test_registry.py` fails for a `covers` path that names nothing in the tree.

With `--released`, only changes to what that release ships count. A module, option, channel or
variable that was added after the release is in no deployment and may change or disappear again
until a release ships it; the report lists such findings as not counted. A finding counts when the
tree also differs from the release at the same place.

### Allowlist

`allowlist.yaml` accepts a finding that is breaking by the rules although no deployment is
affected, such as a constraint that states what the implementation always enforced. An entry
matches findings by pointer glob and states the `reason`. It does not apply on a stable branch, and
`--no-allowlist` ignores the file. The report of a comparison with a release lists the entries that
accept nothing, which can be removed. The CI job requires the `EVerestAPI breaking change` label on a
pull request that touches it.

Unit tests of the rules: `python3 -m pytest tests/compat/contract` (any Python with pytest and
PyYAML, the build venv works).
