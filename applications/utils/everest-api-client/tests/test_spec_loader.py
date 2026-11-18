# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the spec loader shared by the client and the docs generator."""

import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from everest_api.spec_loader import (  # noqa: E402
    SpecError, SpecSet, find_request_operation, json_compatible, load_spec, validate_spec)
from everest_api.specs import find_spec_dir  # noqa: E402

SPEC_DIR = find_spec_dir()
SPEC_FILES = sorted(SPEC_DIR.glob("*.yaml"))


def test_yaml_1_1_boolean_words_stay_strings(tmp_path):
    """A DC power supply mode is 'Off'; YAML 1.1 would read it as false."""
    spec = tmp_path / "x_API.yaml"
    spec.write_text("enum: [Off, On, Yes, No, y, n, off]\n"
                    "flags: [true, false, True, FALSE]\n", encoding="utf-8")
    doc = load_spec(spec)
    assert doc["enum"] == ["Off", "On", "Yes", "No", "y", "n", "off"]
    assert doc["flags"] == [True, False, True, False]


def test_load_spec_rejects_non_mapping(tmp_path):
    spec = tmp_path / "x_API.yaml"
    spec.write_text("- just\n- a list\n", encoding="utf-8")
    with pytest.raises(Exception, match="not a mapping"):
        load_spec(spec)


def test_cross_file_ref_resolves():
    target_file, local_ref, node = SpecSet(SPEC_DIR).resolve(
        "auth_token_provider_API.yaml", "auth_consumer_API.yaml#/components/schemas/ProvidedIdToken")
    assert target_file == "auth_consumer_API.yaml"
    assert local_ref == "#/components/schemas/ProvidedIdToken"
    assert isinstance(node, dict)


@pytest.mark.parametrize("ref", [
    "missing_API.yaml#/components/schemas/X",
    "auth_consumer_API.yaml#/components/schemas/DoesNotExist",
    "../outside/auth_consumer_API.yaml#/components/schemas/IdToken",
    "no-pointer",
])
def test_bad_refs_fail_loudly(ref):
    with pytest.raises(SpecError):
        SpecSet(SPEC_DIR).resolve("auth_consumer_API.yaml", ref)


def test_json_compatible_drops_trailing_zero_of_integral_floats():
    assert json_compatible({"a": 230.0, "b": [1.5, True, 2.0], "c": "x"}) == \
        {"a": 230, "b": [1.5, True, 2], "c": "x"}


@pytest.mark.parametrize("spec", SPEC_FILES, ids=lambda p: p.stem)
def test_every_spec_is_valid_asyncapi(spec):
    validate_spec(load_spec(spec), spec.name)


def test_validate_spec_reports_every_violation_with_its_location():
    doc = load_spec(SPEC_DIR / "powermeter_API.yaml")
    doc["bogus"] = 1
    del doc["info"]["version"]
    with pytest.raises(SpecError) as err:
        validate_spec(doc, "powermeter_API.yaml")
    message = str(err.value)
    assert message.startswith("powermeter_API.yaml is not a valid AsyncAPI 3.0 document")
    assert "'bogus'" in message
    assert "info: 'version' is a required property" in message


def test_find_request_operation_skips_non_mapping_entries():
    operations = {
        "old": None,
        "request": {"reply": {"channel": {"$ref": "#/channels/reply"}}},
    }
    assert find_request_operation(operations, "reply") == ("request", operations["request"])
    assert find_request_operation({"old": None}, "reply") == (None, None)
