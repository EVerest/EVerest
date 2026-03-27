
#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from copy import deepcopy
import logging
import os
from pathlib import Path
import pty
import pytest
from tempfile import mkdtemp
from typing import Dict

import yaml

from everest.testing.core_utils.fixtures import *
from everest.testing.core_utils.everest_core import EverestCore, ManagerStatusFifo

from everest.testing.core_utils import EverestConfigAdjustmentStrategy


def config_has_no_modules(config_path: Path) -> bool:
    """True for configs with an empty or missing active_modules section (including empty files)."""
    loaded = yaml.safe_load(config_path.read_text()) or {}
    return not loaded.get("active_modules")


class EverestCoreConfigSilGenPmConfigurationAdjustment(EverestConfigAdjustmentStrategy):
    def __init__(self):
        self.temporary_directory = mkdtemp()
        self.serial_port_0, self.serial_port_1 = pty.openpty()
        # FIXME: cleanup socket after test
        self.serial_port_0_name = os.ttyname(self.serial_port_0)

    def adjust_everest_configuration(self, everest_config: Dict):
        adjusted_config = deepcopy(everest_config)

        adjusted_config["active_modules"]["serial_comm_hub"]["config_implementation"]["main"]["serial_port"] = self.serial_port_0_name

        return adjusted_config


@pytest.mark.everest_core_config('config-sil-gen-pm.yaml')
@pytest.mark.everest_config_adaptions(EverestCoreConfigSilGenPmConfigurationAdjustment())
@pytest.mark.asyncio
async def test_start_config_sil_gen_pm(everest_core: EverestCore):
    logging.info(">>>>>>>>> test_start_config_sil_gen_pm <<<<<<<<<")

    everest_core.start()


@pytest.mark.use_temporary_persistent_store
class TestConfigsInDirectory:
    @pytest.fixture(params=pytest.everest_configs['params'], ids=pytest.everest_configs['ids'])
    def core_config(self, request) -> EverestEnvironmentCoreConfiguration:
        everest_prefix = Path(request.config.getoption("--everest-prefix"))

        everest_config_path = request.param

        # A config without modules makes the manager exit at boot by default; with
        # --idle-on-failure it stays alive in Idle instead. The marker must be added here:
        # this fixture resolves before everest_environment reads everest_manager_args.
        if config_has_no_modules(everest_config_path):
            request.node.add_marker(pytest.mark.everest_manager_args("--idle-on-failure"))

        return EverestEnvironmentCoreConfiguration(
            everest_core_path=everest_prefix,
            template_everest_config_path=everest_config_path,
        )

    @pytest.mark.asyncio
    async def test_config(self, everest_core: EverestCore):
        logging.info(">>>>>>>>> test_config <<<<<<<<<")
        if everest_core.everest_config.get("active_modules"):
            everest_core.start()
        else:
            # No modules to start: with --idle-on-failure (injected by core_config) the
            # manager must end in Idle instead of reporting ALL_MODULES_STARTED.
            everest_core.start(expected_status=ManagerStatusFifo.MANAGER_IDLE)
