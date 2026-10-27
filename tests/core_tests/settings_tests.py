#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import os
import subprocess
from pathlib import Path
from typing import Optional

import pytest
import yaml

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.probe_module import ProbeModule

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
    if "installed_config" in metafunc.fixturenames:
        # the configs that TestConfigsInDirectory starts, collected in tests/conftest.py
        everest_configs = getattr(pytest, "everest_configs", {"params": [], "ids": []})
        metafunc.parametrize("installed_config", everest_configs["params"], ids=everest_configs["ids"])


@pytest.fixture
def everest_prefix(request) -> Path:
    return _resolve_everest_prefix(request.config)


def run_manager_check(everest_prefix: Path, settings: Optional[Path] = None,
                      config: Optional[Path] = None) -> subprocess.CompletedProcess:
    # MQTT_SERVER_ADDRESS conflicts with a socket path in the settings. --check connects to no broker.
    env = {key: value for key, value in os.environ.items() if key not in ("MQTT_SERVER_ADDRESS", "MQTT_SERVER_PORT")}
    config = config or everest_prefix / "etc/everest/config-empty.yaml"
    args = [str(everest_prefix / "bin/manager"), "--prefix", str(everest_prefix), "--check", "--config", str(config)]
    if settings:
        args += ["--settings", str(settings)]
    return subprocess.run(args, env=env, capture_output=True, text=True, timeout=60)


def test_example_settings_are_installed(everest_prefix: Path):
    for name in EXAMPLE_SETTINGS:
        assert (everest_prefix / "etc/everest" / name).is_file()


def test_installed_settings_pass_check(everest_prefix: Path, installed_settings: Path):
    result = run_manager_check(everest_prefix, installed_settings)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "Loaded settings file at:" in output


def test_installed_configs_pass_check(everest_prefix: Path, installed_config: Path):
    # Integration tests strip the deprecated settings block of a config, so only --check validates it.
    result = run_manager_check(everest_prefix, config=installed_config)
    output = result.stdout + result.stderr
    assert result.returncode == 0, output
    assert "Config is valid" in output


def test_invalid_settings_file_fails_check(everest_prefix: Path, tmp_path: Path):
    settings = tmp_path / "settings-unknown-key.yaml"
    settings.write_text("mqtt_broker_hots: 'localhost'\n")
    result = run_manager_check(everest_prefix, settings)
    output = result.stdout + result.stderr
    assert result.returncode != 0, output
    assert settings.name in output


@pytest.mark.everest_core_settings("settings-mqtt-tcp.yaml")
def test_everest_core_passes_a_settings_file(everest_core: EverestCore, everest_prefix: Path):
    template = yaml.safe_load((everest_prefix / "etc/everest/settings-mqtt-tcp.yaml").read_text())
    settings = everest_core.everest_settings
    assert settings["mqtt_broker_port"] == template["mqtt_broker_port"]
    assert settings["mqtt_everest_prefix"] == f"everest_{everest_core.everest_uuid}"
    assert settings["mqtt_external_prefix"] == everest_core.mqtt_external_prefix
    assert "settings" not in everest_core.everest_config


@pytest.mark.asyncio
@pytest.mark.everest_core_config("config-example.yaml")
@pytest.mark.everest_core_settings("settings-mqtt-tcp.yaml")
@pytest.mark.probe_module(connections={"kvs": [Requirement("store", "main")]})
async def test_manager_and_probe_module_use_the_settings_file(everest_core: EverestCore):
    everest_core.start(standalone_module="probe")
    probe_module = ProbeModule(everest_core.get_runtime_session())
    probe_module.start()
    await probe_module.wait_to_be_ready()

    await probe_module.call_command("kvs", "store", {"key": "settings_test", "value": "from_settings_file"})
    assert await probe_module.call_command("kvs", "load", {"key": "settings_test"}) == "from_settings_file"
