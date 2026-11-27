# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from pathlib import Path

import pytest

from contract import registry, snapshot

REPO_ROOT = Path(__file__).resolve().parents[3]

REGISTRY = {
    "deprecations": [
        {"id": "old-module", "covers": ["modules/Old"], "deprecated_in": "2026.10.0", "earliest_removal": "2027.04.0"},
        {"id": "no-contract-part", "deprecated_in": "2026.10.0", "earliest_removal": "2027.04.0"},
    ],
    "experimental": [{"id": "trial-api", "covers": ["asyncapi/trial_API", "modules/*/config/Trial*"], "since": "2026.10.0"}],
}


def test_covers_names_a_part_and_everything_below_it():
    assert registry.covers("modules/Old", "modules/Old")
    assert registry.covers("modules/Old", "modules/Old/config/a/default")
    assert not registry.covers("modules/Old", "modules/Older")
    assert not registry.covers("modules/Old/config/a", "modules/Old")
    assert registry.covers("asyncapi/x_API/*/receive_y", "asyncapi/x_API/channels/receive_y/messages/receive_y")
    assert registry.covers("asyncapi/x_API/**/severity", "asyncapi/x_API/channels/c/messages/m/payload/required/severity")
    assert not registry.covers("asyncapi/x_API/*/receive_y", "asyncapi/x_API/channels/receive_yes")


def test_experimental_component_may_change_in_any_release():
    accepted, not_due = registry.exemption("asyncapi/trial_API/channels/a", REGISTRY, "2026.10.1", patch_release=True)
    assert accepted.startswith("experimental since 2026.10.0") and not_due is None
    assert registry.exemption("modules/M/config/TrialPath/default", REGISTRY, None)[0] is not None


def test_deprecated_component_may_change_from_its_earliest_removal():
    assert registry.exemption("modules/Old", REGISTRY, "2027.04.0")[0].startswith("deprecated in 2026.10.0")
    assert registry.exemption("modules/Old", REGISTRY, "2027.10.0")[0] is not None
    accepted, not_due = registry.exemption("modules/Old", REGISTRY, "2026.10.1")
    assert accepted is None and "at the earliest" in not_due
    accepted, not_due = registry.exemption("modules/Old", REGISTRY, "2027.04.0", patch_release=True)
    assert accepted is None and "patch release" in not_due


def test_deprecation_needs_the_release_of_the_checked_tree():
    with pytest.raises(ValueError):
        registry.exemption("modules/Old", REGISTRY, None)


def test_uncovered_pointer_has_no_exemption():
    assert registry.exemption("modules/Other", REGISTRY, "2027.04.0") == (None, None)
    assert registry.exemption("modules/Other", {}, None) == (None, None)


def test_collect_keeps_what_the_check_needs(tmp_path):
    assert registry.collect(tmp_path) == {"deprecations": [], "experimental": []}
    path = tmp_path / registry.REGISTRY_FILES["deprecations"]
    path.parent.mkdir(parents=True)
    path.write_text(
        "- id: old-module\n  component: Old module\n  deprecated_in: 2026.10.0\n  earliest_removal: 2027.04.0\n"
        "  migration: Use the new one\n  covers:\n    - modules/Old\n"
    )
    assert registry.collect(tmp_path)["deprecations"] == [
        {"id": "old-module", "covers": ["modules/Old"], "deprecated_in": "2026.10.0", "earliest_removal": "2027.04.0"}
    ]


def _contract_paths(node, prefix):
    yield prefix
    if isinstance(node, dict):
        for key, value in node.items():
            yield from _contract_paths(value, f"{prefix}/{key}")


def test_repository_registry_covers_parts_of_this_tree():
    """A path that names nothing accepts nothing; this catches a misspelled one."""
    tree = snapshot.snapshot_tree(REPO_ROOT)
    paths = [
        path
        for section in ("asyncapi", "modules", "framework", "manager")
        for path in _contract_paths(tree[section], section)
    ] + [f"migrations/{name}" for name in tree["migrations"]]
    unmatched = [
        f"{entry['id']}: {pattern}"
        for entries in tree["registry"].values()
        for entry in entries
        for pattern in entry.get("covers") or []
        if not any(registry.covers(pattern, path) for path in paths)
    ]
    assert unmatched == []


def test_manager_warns_about_the_options_the_registry_lists_as_experimental():
    tree = snapshot.snapshot_tree(REPO_ROOT)
    listed = sorted(
        pattern.removeprefix("manager/options/")
        for entry in tree["registry"]["experimental"]
        for pattern in entry.get("covers") or []
        if pattern.startswith("manager/options/")
    )
    assert listed == tree["manager"]["experimental_options"]
