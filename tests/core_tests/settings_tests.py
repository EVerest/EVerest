#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import os
import subprocess
from pathlib import Path

import pytest

EXAMPLE_SETTINGS = ["settings-all-options.yaml", "settings-mqtt-tcp.yaml", "settings-mqtt-uds.yaml"]


def _resolve_everest_prefix(config: pytest.Config) -> Path:
    everest_prefix = Path(config.getoption("--everest-prefix"))
    if everest_prefix.is_absolute():
        return everest_prefix
    candidates = [
        (Path.cwd() / everest_prefix).resolve(),
        (config.rootpath / everest_prefix).resolve(),
        (config.rootpath.parent / everest_prefix).resolve(),
    ]
    return next((path for path in candidates if path.exists()), candidates[0])


def pytest_generate_tests(metafunc):
    if "installed_settings" in metafunc.fixturenames:
        settings_files = sorted((_resolve_everest_prefix(metafunc.config) / "etc/everest").glob("settings-*.yaml"))
        metafunc.parametrize("installed_settings", settings_files, ids=[path.stem for path in settings_files])


@pytest.fixture
def everest_prefix(request) -> Path:
    return _resolve_everest_prefix(request.config)


def run_manager_check(everest_prefix: Path, settings: Path) -> subprocess.CompletedProcess:
    # MQTT_SERVER_ADDRESS conflicts with a socket path in the settings file. --check connects to no broker.
    env = {key: value for key, value in os.environ.items() if key not in ("MQTT_SERVER_ADDRESS", "MQTT_SERVER_PORT")}
    return subprocess.run(
        [str(everest_prefix / "bin/manager"), "--prefix", str(everest_prefix), "--check",
         "--config", str(everest_prefix / "etc/everest/config-empty.yaml"), "--settings", str(settings)],
        env=env, capture_output=True, text=True, timeout=60)


def test_example_settings_are_installed(everest_prefix: Path):
    for name in EXAMPLE_SETTINGS:
        assert (everest_prefix / "etc/everest" / name).is_file()


def test_installed_settings_pass_check(everest_prefix: Path, installed_settings: Path):
    result = run_manager_check(everest_prefix, installed_settings)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "Loaded settings file at:" in output


def test_invalid_settings_file_fails_check(everest_prefix: Path, tmp_path: Path):
    settings = tmp_path / "settings-unknown-key.yaml"
    settings.write_text("mqtt_broker_hots: 'localhost'\n")
    result = run_manager_check(everest_prefix, settings)
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert settings.name in output
