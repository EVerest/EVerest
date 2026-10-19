# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Tests for the spec loader shared by both generators."""

import pytest

from everest_api_client_gen.spec_loader import load_spec


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

