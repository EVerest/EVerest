# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import logging
import shutil
from copy import deepcopy
from pathlib import Path
from typing import Dict

from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy


class PyEvJosevPkiPathConfigurationStrategy(EverestConfigAdjustmentStrategy):
    """ Gives every PyEvJosev module a private copy of the EV certificates.

    PyEvJosev writes an installed contract certificate and its key into its PKI directory. Tests running in
    parallel against one installation would otherwise share that directory, and one EV could read the key
    another EV is writing at that moment. A pki_path already set in the configuration is kept, and without an
    installed certs directory the modules keep their default.
    """

    def __init__(self, source_certificates_directory: Path, target_certificates_directory: Path):
        self._source_certificates_directory = source_certificates_directory
        self._target_certificates_directory = target_certificates_directory

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        adjusted_config = deepcopy(everest_config)
        active_modules = (adjusted_config or {}).get("active_modules") or {}

        for module_cfg in active_modules.values():
            if module_cfg.get("module") != "PyEvJosev":
                continue
            config_module = module_cfg.setdefault("config_module", {})
            if config_module.get("pki_path"):
                continue
            if not self._copy_certificates():
                break
            config_module["pki_path"] = str(self._target_certificates_directory)

        return adjusted_config

    def _copy_certificates(self) -> bool:
        if self._target_certificates_directory.exists():
            return True
        if not self._source_certificates_directory.is_dir():
            logging.warning(f"No certificates in {self._source_certificates_directory}; "
                            "PyEvJosev keeps its default PKI directory")
            return False
        try:
            shutil.copytree(self._source_certificates_directory, self._target_certificates_directory)
        except shutil.Error as error:
            # Tests running in parallel may replace installed certificates while they are copied
            logging.warning(f"Some EV certificates could not be copied: {error}")
        return True
