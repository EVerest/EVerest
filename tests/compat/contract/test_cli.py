# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import json

import pytest

from contract import compat_contract

REGISTRY = {
    "deprecations": [
        {"id": "old-module", "covers": ["modules/Old"], "deprecated_in": "2026.10.0", "earliest_removal": "2027.04.0"}
    ],
    "experimental": [{"id": "trial-module", "covers": ["modules/Trial"], "since": "2026.10.0"}],
}

ALLOWLIST = """
- pointer: modules/Harmless
  kinds: [remove]
  reason: nothing uses it
"""


@pytest.fixture
def run(tmp_path):
    """Runs ``diff`` on a baseline that ships the given modules and a tree that ships none."""
    allowlist = tmp_path / "allowlist.yaml"
    allowlist.write_text(ALLOWLIST)

    def _run(removed_modules, *options, version="2027.04.0"):
        old = tmp_path / "old.json"
        new = tmp_path / "new.json"
        report = tmp_path / "report.json"
        old.write_text(
            json.dumps(
                {
                    "format": 1,
                    "asyncapi": {"some_API": {}},
                    "modules": {"Kept": {}, **{name: {} for name in removed_modules}},
                    "registry": REGISTRY,
                }
            )
        )
        new.write_text(
            json.dumps({"format": 1, "asyncapi": {"some_API": {}}, "modules": {"Kept": {}}, "version": version})
        )
        status = compat_contract.main(
            ["diff", str(old), str(new), "--allowlist", str(allowlist), "--json", str(report), "--quiet", *options]
        )
        return status, json.loads(report.read_text())

    return _run


def test_removal_is_exempt_in_the_release_the_tree_states(run):
    status, report = run(["Old", "Trial", "Harmless"])
    assert status == 0
    assert len(report["accepted"]) == 3


def test_removal_is_not_exempt_before_the_earliest_removal_release(run):
    status, report = run(["Old"], version="2026.10.1")
    assert status == 1
    assert len(report["not_due"]) == 1


def test_target_release_option_overrides_the_tree(run):
    status, _ = run(["Old"], "--target-release", "2027.04.0", version="2026.10.1")
    assert status == 0


def test_tree_without_a_release_is_a_usage_error(tmp_path):
    old, new = tmp_path / "old.json", tmp_path / "new.json"
    tree = {"format": 1, "modules": {"Kept": {}}, "asyncapi": {"some_API": {}}}
    old.write_text(json.dumps({**tree, "modules": {"Kept": {}, "Old": {}}, "registry": REGISTRY}))
    new.write_text(json.dumps(tree))
    arguments = ["diff", str(old), str(new), "--no-allowlist", "--quiet"]
    assert compat_contract.main(arguments) == 2
    assert compat_contract.main([*arguments, "--target-release", "2027.04.0"]) == 0


def test_snapshot_of_another_format_is_a_usage_error(tmp_path):
    snapshot = tmp_path / "snapshot.json"
    snapshot.write_text(json.dumps({"format": 0, "modules": {"Kept": {}}, "asyncapi": {"some_API": {}}}))
    assert compat_contract.main(["diff", str(snapshot), str(snapshot), "--quiet"]) == 2


def test_unknown_base_ref_is_a_usage_error():
    assert compat_contract.main(["check", "--base", "no-such-ref", "--quiet"]) == 2


def test_unclassified_finding_fails(tmp_path):
    def snapshot(name, mappings):
        path = tmp_path / name
        path.write_text(
            json.dumps(
                {
                    "format": 1, "version": "2027.04.0", "modules": {"Kept": {}}, "asyncapi": {"some_API": {}},
                    "ocpp": {"v16_custom_key_mappings": {"mappings": mappings}},
                }
            )
        )
        return str(path)

    old, new = snapshot("old.json", [{"ocpp16_key": "A"}]), snapshot("new.json", [{"ocpp16_key": "B"}])
    assert compat_contract.main(["diff", old, old, "--no-allowlist", "--quiet"]) == 0
    assert compat_contract.main(["diff", old, new, "--no-allowlist", "--quiet"]) == 1


def test_patch_release_accepts_only_experimental_exemptions(run):
    assert run(["Trial"], "--patch-release")[0] == 0
    status, report = run(["Old", "Trial", "Harmless"], "--patch-release")
    assert status == 1
    assert len(report["accepted"]) == 1
