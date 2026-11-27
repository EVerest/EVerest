# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from pathlib import Path

import pytest

from contract import allowlist
from contract.rules import ADD, BREAKING, NON_BREAKING, REMOVE, UNCLASSIFIED, Finding

HERE = Path(__file__).resolve().parent

REGISTRY = {
    "deprecations": [
        {"id": "old-module", "covers": ["modules/Old"], "deprecated_in": "2026.10.0", "earliest_removal": "2027.04.0"}
    ],
    "experimental": [{"id": "trial-module", "covers": ["modules/Trial"], "since": "2026.10.0"}],
}


def entry(pointer, **kwargs):
    return allowlist.AllowlistEntry(pointer=pointer, reason="test", **kwargs)


def test_glob_matching():
    finding = Finding("asyncapi/slac_API/channels/receive_request_reset", REMOVE, BREAKING, "x")
    assert entry("asyncapi/slac_API/**").matches(finding)
    assert entry("asyncapi/*/channels/receive_request_reset").matches(finding)
    assert not entry("asyncapi/slac_API/*").matches(finding)
    assert not entry("asyncapi/slac_API").matches(finding)
    assert not entry("asyncapi/other_API/**").matches(finding)


def test_classification_and_kind_filters():
    breaking = Finding("modules/X/config/a", REMOVE, BREAKING, "x")
    unclassified = Finding("modules/X/config/a", ADD, UNCLASSIFIED, "x")
    assert entry("modules/X/**").matches(breaking)
    assert not entry("modules/X/**").matches(unclassified)
    assert entry("modules/X/**", classifications=[BREAKING, UNCLASSIFIED]).matches(unclassified)
    assert not entry("modules/X/**", kinds=[ADD]).matches(breaking)
    assert entry("modules/X/**", kinds=[REMOVE]).matches(breaking)


def test_load_allowlist_rejects_malformed_entries(tmp_path):
    path = tmp_path / "allowlist.yaml"
    path.write_text("- pointer: a\n")
    with pytest.raises(ValueError):
        allowlist.load_allowlist(path)
    path.write_text("- pointer: a\n  reason: r\n  index: deprecation\n")
    with pytest.raises(ValueError):
        allowlist.load_allowlist(path)
    path.write_text("pointer: a\n")
    with pytest.raises(ValueError):
        allowlist.load_allowlist(path)
    path.write_text("- pointer: a\n  reason: r\n  kinds: [remove]\n")
    assert allowlist.load_allowlist(path)[0].kinds == ["remove"]


def test_registry_and_allowlist_accept_findings():
    findings = [
        Finding("modules/Old", REMOVE, BREAKING, "x"),
        Finding("modules/Trial/config/a", REMOVE, BREAKING, "x"),
        Finding("modules/Kept/config/a", REMOVE, BREAKING, "x"),
        Finding("modules/Other", REMOVE, BREAKING, "x"),
        Finding("modules/New", ADD, NON_BREAKING, "x"),
    ]
    result = allowlist.apply_exceptions(findings, REGISTRY, [entry("modules/Kept/**")], target_release="2027.04.0")
    assert [(f.pointer, why.split()[0]) for f, why in result.accepted] == [
        ("modules/Old", "deprecated"), ("modules/Trial/config/a", "experimental"), ("modules/Kept/config/a", "test"),
    ]
    assert [f.pointer for f in result.remaining] == ["modules/Other", "modules/New"]
    assert result.not_due == []
    assert result.unused == []


def test_entry_that_accepts_nothing_is_unused():
    findings = [Finding("modules/Old", REMOVE, BREAKING, "x")]
    stale = [entry("modules/Old"), entry("modules/Gone")]
    result = allowlist.apply_exceptions(findings, REGISTRY, stale, target_release="2027.04.0")
    assert [unused.pointer for unused in result.unused] == ["modules/Old", "modules/Gone"]


def test_removal_before_the_earliest_removal_release_remains():
    removal = Finding("modules/Old", REMOVE, BREAKING, "x")
    result = allowlist.apply_exceptions([removal], REGISTRY, [], target_release="2026.10.1")
    assert result.remaining == [removal] and not result.accepted
    assert "2027.04.0 at the earliest" in result.not_due[0][1]


def test_patch_release_accepts_only_experimental_components():
    findings = [
        Finding("modules/Old", REMOVE, BREAKING, "x"),
        Finding("modules/Trial", REMOVE, BREAKING, "x"),
        Finding("modules/Kept/config/a", REMOVE, BREAKING, "x"),
    ]
    result = allowlist.apply_exceptions(
        findings, REGISTRY, [entry("modules/Kept/**")], target_release="2027.04.0", patch_release=True
    )
    assert [f.pointer for f, _ in result.accepted] == ["modules/Trial"]
    assert [f.pointer for f in result.remaining] == ["modules/Old", "modules/Kept/config/a"]


def test_baseline_without_a_registry_accepts_nothing():
    removal = Finding("modules/Old", REMOVE, BREAKING, "x")
    assert allowlist.apply_exceptions([removal], {}, [], target_release="2027.04.0").remaining == [removal]


def test_repository_allowlist_loads():
    allowlist.load_allowlist(HERE / "allowlist.yaml")
