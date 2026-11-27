# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import os
import sys

_PKG_SRC = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "src"))
if _PKG_SRC not in sys.path:
    sys.path.insert(0, _PKG_SRC)

from everest.testing.core_utils._configuration.everest_configuration_strategies.ev_pki_path_configuration_strategy import \
    EvPkiPathConfigurationStrategy  # noqa: E402


def _source_certs(tmp_path):
    source = tmp_path / "installed_certs"
    (source / "client" / "mo").mkdir(parents=True)
    (source / "client" / "mo" / "MO_LEAF.key").write_text("installed key")
    return source


def test_ev15118_gets_a_private_copy_of_the_certificates(tmp_path):
    source = _source_certs(tmp_path)
    target = tmp_path / "ev_certs"
    config = {"active_modules": {"ev": {"module": "Ev15118"}, "evse": {"module": "EvseManager"}}}

    adjusted = EvPkiPathConfigurationStrategy(source, target).adjust_everest_configuration(config)

    assert adjusted["active_modules"]["ev"]["config_module"]["pki_path"] == str(target)
    assert "config_module" not in adjusted["active_modules"]["evse"]
    assert (target / "client" / "mo" / "MO_LEAF.key").read_text() == "installed key"
    assert "config_module" not in config["active_modules"]["ev"]


def test_ev_controller_gets_a_private_copy_of_the_certificates(tmp_path):
    source = _source_certs(tmp_path)
    target = tmp_path / "ev_certs"
    config = {"active_modules": {"ev": {"module": "EvController", "config_module": {"connector_id": 1}}}}

    adjusted = EvPkiPathConfigurationStrategy(source, target).adjust_everest_configuration(config)

    assert adjusted["active_modules"]["ev"]["config_module"] == {"connector_id": 1, "pki_path": str(target)}
    assert (target / "client" / "mo" / "MO_LEAF.key").read_text() == "installed key"


def test_configured_pki_path_is_kept(tmp_path):
    source = _source_certs(tmp_path)
    target = tmp_path / "ev_certs"
    config = {"active_modules": {"ev": {"module": "Ev15118", "config_module": {"pki_path": "/somewhere"}}}}

    adjusted = EvPkiPathConfigurationStrategy(source, target).adjust_everest_configuration(config)

    assert adjusted["active_modules"]["ev"]["config_module"]["pki_path"] == "/somewhere"
    assert not target.exists()


def test_nothing_is_copied_without_an_ev_module(tmp_path):
    source = _source_certs(tmp_path)
    target = tmp_path / "ev_certs"
    config = {"active_modules": {"evse": {"module": "EvseManager"}}}

    assert EvPkiPathConfigurationStrategy(source, target).adjust_everest_configuration(config) == config
    assert not target.exists()


def test_missing_installed_certificates_keep_the_default(tmp_path):
    target = tmp_path / "ev_certs"
    config = {"active_modules": {"ev": {"module": "Ev15118"}}}

    adjusted = EvPkiPathConfigurationStrategy(tmp_path / "missing", target).adjust_everest_configuration(config)

    assert "pki_path" not in adjusted["active_modules"]["ev"].get("config_module", {})
    assert not target.exists()
