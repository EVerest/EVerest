# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

"""Helpers to spawn a full EVerest stack from tests (`EverestCore`).

**Parallel pytest / gcov**

When the tree is built with coverage instrumentation (CMake option ``EVEREST_ENABLE_COVERAGE`` in
everest-core), ``gcov`` writes ``.gcda`` files when processes exit. By default those files are
placed next to the instrumented object files in the **build tree**. Several ``EverestCore``
instances (for example ``pytest-xdist`` workers) would then write the **same** paths concurrently.
That corrupts profiling data (``libgcov`` merge mismatch) and can also perturb process teardown
timing.

``EverestCore`` therefore sets ``GCOV_PREFIX`` (and ``GCOV_PREFIX_STRIP``) on the **manager**
subprocess so each instance writes under its own workspace directory (``<workspace>/gcda/``). This
keeps coverage data isolated per test session without changing non-coverage builds (the env vars
are harmless when binaries are not instrumented).

**Stopping the manager subprocess**

``stop()`` mirrors what a human operator does: graceful shutdown first (``SIGINT``, which the
manager treats as normal shutdown), then bounded waits. If the process does not exit, it escalates
to ``SIGTERM`` and finally ``SIGKILL`` so CI and local runs do not hang indefinitely on a wedged
stack. If escalation was required, ``stop()`` raises ``RuntimeError`` so tests that require a clean
SIGINT-only shutdown cannot silently ignore a broken teardown.
"""

import logging
import os
import signal
from threading import Thread
import threading
import time
import subprocess
from pathlib import Path
import tempfile
from typing import List, Optional, Union, Dict
import uuid
import yaml
import selectors
from signal import SIGINT

from everest.framework import RuntimeSession
from everest.testing.core_utils.common import Requirement
from ._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy
from ._configuration.everest_configuration_strategies.mqtt_configuration_strategy import \
    EverestMqttConfigurationAdjustmentStrategy
from ._configuration.everest_configuration_strategies.probe_module_configuration_strategy import \
    ProbeModuleConfigurationStrategy

STARTUP_TIMEOUT = 30
SHUTDOWN_TIMEOUT_SIGINT_SECONDS = 60
SHUTDOWN_TIMEOUT_SIGTERM_SECONDS = 15
SHUTDOWN_TIMEOUT_SIGKILL_SECONDS = 5

Connections = dict[str, List[Requirement]]


class StatusFifoListener:
    def __init__(self, status_fifo_path: Path):
        if not status_fifo_path.exists():
            os.mkfifo(status_fifo_path)

        # note: open doesn't support non-blocking, so we use os.open to get the fd
        fd = os.open(status_fifo_path, flags=(os.O_RDONLY | os.O_NONBLOCK))
        self._file_obj = open(fd)

        selector = selectors.DefaultSelector()
        selector.register(self._file_obj, selectors.EVENT_READ)
        self._selector = selector

    def wait_for_status(self, timeout: float, match_status: list[str]) -> Optional[list[str]]:
        if match_status is None:
            match_status = []

        end_time = time.time() + timeout

        while True:
            for _key, _mask in self._selector.select(timeout):
                data = self._file_obj.read()
                if len(data) == 0:
                    return None

                # plural!
                received_status = data.splitlines()

                if len(match_status) == 0:
                    # we're not trying to match any messages
                    return received_status

                # return the filtered matched messages
                matched_status = [status for status in match_status if status in received_status]
                if len(matched_status) > 0:
                    return matched_status

            timeout = end_time - time.time()

            if timeout < 0:
                return []


class EverestCore:
    """Configure, start, and stop a full EVerest install from tests.

    Each instance gets a private workspace (temp dir or supplied ``tmp_path``) including generated
    config, status FIFO, and (when applicable) a per-instance ``gcda`` directory for coverage; see
    module docstring.
    """

    def __init__(self,
                 prefix_path: Path,
                 config_path: Path = None,
                 standalone_module: Optional[Union[str, List[str]]] = None,
                 manager_extra_args: Optional[List[str]] = None,
                 everest_configuration_adjustment_strategies: Optional[
                     List[EverestConfigAdjustmentStrategy]] = None,
                 tmp_path: Optional[Path] = None) -> None:
        """Initialize EVerest using everest_core_path and everest_config_path

        Args:
            everest_prefix (Path): location of installed everest distribution".
            standalone_module (str): Standalone module parameter provided to EVerest manager app (can be overwritten in startup)
        """

        self.process = None
        self.everest_uuid = uuid.uuid4().hex

        if not tmp_path:
            self._workspace_root = Path(tempfile.mkdtemp(prefix=self.everest_uuid))
            temp_dir = self._workspace_root
            temp_everest_config_file = tempfile.NamedTemporaryFile(
                delete=False, mode="w+", suffix=".yaml", dir=temp_dir)
            self.everest_config_path = Path(temp_everest_config_file.name)
            self.everest_core_user_config_path = Path(
                temp_everest_config_file.name).parent / 'user-config'
            self.everest_core_user_config_path.mkdir(parents=True, exist_ok=True)
            self._status_fifo_path = temp_dir / "status.fifo"
            self._db_path = temp_dir / "everest.db"
        else:
            self._workspace_root = tmp_path
            config_dir = tmp_path / "everest_config"
            config_dir.mkdir()
            self.everest_core_user_config_path = config_dir / "user-config"
            self.everest_core_user_config_path.mkdir()
            self.everest_config_path = config_dir / "everest_config.yaml"
            self._status_fifo_path = tmp_path / "status.fifo"
            self._db_path = tmp_path / "everest.db"

        # See module docstring: isolate gcov output per workspace for parallel pytest.
        self._gcda_root = self._workspace_root / "gcda"
        self._gcda_root.mkdir(parents=True, exist_ok=True)

        self.prefix_path = prefix_path
        self.etc_path = Path('/etc/everest') if prefix_path == '/usr' else prefix_path / 'etc/everest'

        if config_path is None:
            config_path = self.etc_path / 'config-sil.yaml'

        self.mqtt_external_prefix = f"external_{self.everest_uuid}"

        self._write_temporary_config(config_path, everest_configuration_adjustment_strategies)

        logging.info(f"everest uuid: {self.everest_uuid}")
        logging.info(f"temp everest config: {self.everest_config_path} based on {config_path}")

        self.test_control_modules = None

        self.log_reader_thread: Thread = None
        self.everest_running = False
        self.all_modules_started_event = threading.Event()

        self._standalone_module = standalone_module
        self._manager_extra_args = manager_extra_args or []

    def _manager_subprocess_env(self) -> Dict[str, str]:
        """Environment for the manager child: redirects gcov output when instrumentation is on."""
        env = os.environ.copy()
        env["GCOV_PREFIX"] = str(self._gcda_root) + os.sep
        env["GCOV_PREFIX_STRIP"] = "0"
        return env

    @property
    def everest_config(self) -> Dict:
        with self.everest_config_path.open("r") as f:
            return yaml.safe_load(f)

    def _write_temporary_config(self, template_config_path: Path, everest_configuration_adjustment_strategies: Optional[
        List[EverestConfigAdjustmentStrategy]]):
        everest_configuration_adjustment_strategies = everest_configuration_adjustment_strategies if everest_configuration_adjustment_strategies else []
        everest_configuration_adjustment_strategies.append(
            EverestMqttConfigurationAdjustmentStrategy(everest_uuid=self.everest_uuid,
                                                       mqtt_external_prefix=self.mqtt_external_prefix))
        everest_config = yaml.safe_load(template_config_path.read_text())
        for strategy in everest_configuration_adjustment_strategies:
            everest_config = strategy.adjust_everest_configuration(everest_config)
        with self.everest_config_path.open("w") as f:
            yaml.dump(everest_config, f)

    def start(self, standalone_module: Optional[Union[str, List[str]]] = None, test_connections: Connections = None):
        """Starts EVerest in a subprocess

        Args:
            standalone_module (str, optional): If set, a submodule can be started separately. EVerest will then wait for the submodule to be started.
             Defaults to None.
        """

        standalone_module = standalone_module if standalone_module is not None else self._standalone_module

        manager_path = self.prefix_path / 'bin/manager'

        logging.info(f'config: {self.everest_config_path}')

        # FIXME (aw): clean up passing of modules_to_test
        self.test_connections = test_connections if test_connections != None else {}
        self._create_testing_user_config()

        self.status_listener = StatusFifoListener(self._status_fifo_path)
        logging.info(self._status_fifo_path)

        args = [str(manager_path.resolve()), '--config', str(self.everest_config_path), '--db', str(self._db_path),
                '--status-fifo', str(self._status_fifo_path), '--prefix', str(self.prefix_path.resolve())]

        if standalone_module:
            logging.info(f"Standalone module {standalone_module} was specified")
            if not isinstance(standalone_module, list):
                standalone_module = [standalone_module]
            for s in standalone_module:
                args.extend(['--standalone', s])

        if self._manager_extra_args:
            args.extend(self._manager_extra_args)

        logging.info(" ".join(args))

        logging.info('Starting EVerest...')
        logging.info('  '.join(args))

        self.process = subprocess.Popen(
            args,
            cwd=self.prefix_path,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=self._manager_subprocess_env(),
        )

        self.log_reader_thread = Thread(target=self.read_everest_log)
        self.log_reader_thread.start()

        expected_status = 'ALL_MODULES_STARTED' if standalone_module == None else 'WAITING_FOR_STANDALONE_MODULES'

        status = self.status_listener.wait_for_status(STARTUP_TIMEOUT, [expected_status])
        if status == None or len(status) == 0:
            self.read_everest_log()
            raise TimeoutError("Timeout while waiting for EVerest to start")

        logging.info("EVerest has started")
        if expected_status == 'ALL_MODULES_STARTED':
            self.all_modules_started_event.set()

    def read_everest_log(self):
        while self.process.poll() == None:
            stderr_raw = self.process.stderr.readline()
            stderr_formatted = stderr_raw.strip().decode(errors="ignore")
            logging.debug(f'  {stderr_formatted}')

        if self.process.returncode == 0:
            logging.info("EVerest stopped with return code 0")
        elif self.process.returncode < 0:
            logging.info(f"EVerest stopped by signal {signal.Signals(-self.process.returncode).name}")
        else:
            logging.warning(f"EVerest stopped with return code: {self.process.returncode}")

        logging.debug("EVerest output stopped")

    def stop(self):
        """Request manager shutdown and wait for the subprocess to exit.

        Sends ``SIGINT`` first (EVerest manager graceful path), waits up to
        ``SHUTDOWN_TIMEOUT_SIGINT_SECONDS``, then ``SIGTERM`` / ``SIGKILL`` with shorter timeouts if
        needed. Raises ``RuntimeError`` if escalation beyond SIGINT was required so callers can treat
        a stuck stack as a failure; see module docstring.
        """
        logging.debug("CONTROLLER stop() function called...")
        escalation_signal = None
        if self.process:
            # NOTE (aw): we could also call process.kill()
            if self.process.poll() is None:
                self.process.send_signal(SIGINT)
                try:
                    self.process.wait(timeout=SHUTDOWN_TIMEOUT_SIGINT_SECONDS)
                except subprocess.TimeoutExpired:
                    logging.warning(
                        f"EVerest did not stop after SIGINT within {SHUTDOWN_TIMEOUT_SIGINT_SECONDS}s, sending SIGTERM"
                    )
                    escalation_signal = "SIGTERM"
                    self.process.terminate()
                    try:
                        self.process.wait(timeout=SHUTDOWN_TIMEOUT_SIGTERM_SECONDS)
                    except subprocess.TimeoutExpired:
                        logging.warning(
                            f"EVerest did not stop after SIGTERM within {SHUTDOWN_TIMEOUT_SIGTERM_SECONDS}s, sending SIGKILL"
                        )
                        escalation_signal = "SIGKILL"
                        self.process.kill()
                        try:
                            self.process.wait(timeout=SHUTDOWN_TIMEOUT_SIGKILL_SECONDS)
                        except subprocess.TimeoutExpired:
                            logging.error(
                                "EVerest child not reaped after SIGKILL within "
                                f"{SHUTDOWN_TIMEOUT_SIGKILL_SECONDS}s (pid may be stuck as zombie)"
                            )

        if self.log_reader_thread:
            self.log_reader_thread.join(timeout=5)

        if escalation_signal is not None:
            raise RuntimeError(
                f"EVerest stop() required escalation to {escalation_signal}. "
                "Tests must shut down cleanly via SIGINT only."
            )

    def _create_testing_user_config(self):
        """Creates a user-config file to include the PyTestControlModule in the current SIL simulation.
        If a user-config already exists, it will be re-named
        """

        if len(self.test_connections) == 0:
            # nothing to do here
            return

        file = self.everest_core_user_config_path / self.everest_config_path.name
        logging.info(f"temp everest user-config: {file.resolve()}")

        # FIXME (aw): we need some agreement, if the module id of the probe module should be fixed or not
        logging.info(f'Adding test control module(s) to user-config: {self.test_control_modules}')
        user_config = {}
        user_config = ProbeModuleConfigurationStrategy(connections=self.test_connections).adjust_everest_configuration(user_config)

        file.write_text(yaml.dump(user_config))

    def get_runtime_session(self):
        return RuntimeSession(str(self.prefix_path), str(self.everest_config_path))

