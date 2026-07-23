# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

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


class ManagerStatusFifo:
    """Status-fifo line constants written by the EVerest manager (without trailing newline)."""

    ALL_MODULES_STARTED = "ALL_MODULES_STARTED"
    WAITING_FOR_STANDALONE_MODULES = "WAITING_FOR_STANDALONE_MODULES"

    MANAGER_INITIALIZING = "MANAGER_INITIALIZING"
    MANAGER_STARTING_MODULES = "MANAGER_STARTING_MODULES"
    MANAGER_RUNNING = "MANAGER_RUNNING"
    MANAGER_RESTART_REQUESTED = "MANAGER_RESTART_REQUESTED"
    MANAGER_CRASH_SHUTDOWN_IN_PROGRESS = "MANAGER_CRASH_SHUTDOWN_IN_PROGRESS"
    MANAGER_SHUTDOWN_REQUESTED = "MANAGER_SHUTDOWN_REQUESTED"
    MANAGER_FORCE_TERMINATING = "MANAGER_FORCE_TERMINATING"
    MANAGER_SHUTDOWN_FINALIZING = "MANAGER_SHUTDOWN_FINALIZING"
    MANAGER_IDLE = "MANAGER_IDLE"
    MANAGER_EXITING = "MANAGER_EXITING"

    SIGINT_RECEIVED = "SIGINT_RECEIVED"
    ALL_MODULES_STOPPED_CLEAN = "ALL_MODULES_STOPPED_CLEAN"
    FORCE_SHUTDOWN_TIMEOUT = "FORCE_SHUTDOWN_TIMEOUT"
    CRASH_RECOVERY_EXHAUSTED = "CRASH_RECOVERY_EXHAUSTED"

    @staticmethod
    def crash_recovery_attempt(attempt: int, max_attempts: int) -> str:
        return f"CRASH_RECOVERY_ATTEMPT:{attempt}/{max_attempts}"


class StatusFifoListener:
    def __init__(self, status_fifo_path: Path):
        if not status_fifo_path.exists():
            os.mkfifo(status_fifo_path)

        # Open RDWR|NONBLOCK (Linux) so we never see a spurious EOF from
        # read() before the manager opens its write end. With O_RDONLY|
        # O_NONBLOCK, a read with no writers returns 0 immediately, which
        # made start() abort and then hang in a blocking stderr readline.
        self._fd = os.open(status_fifo_path, flags=(os.O_RDWR | os.O_NONBLOCK))
        self._lock = threading.Lock()
        self._condition = threading.Condition(self._lock)
        self._pending_lines: list[str] = []
        self._read_buffer = b""

    def _append_lines(self, data: bytes) -> None:
        if not data:
            return
        self._read_buffer += data
        while b"\n" in self._read_buffer:
            line, self._read_buffer = self._read_buffer.split(b"\n", 1)
            if line:
                self._pending_lines.append(line.decode())
        self._condition.notify_all()

    def _read_available(self) -> Optional[bool]:
        """Read available fifo bytes.

        Caller must hold ``self._condition``.

        Returns:
            True when data was read,
            None when no bytes were available (or a spurious empty read).
        """
        read_any = False
        while True:
            try:
                chunk = os.read(self._fd, 4096)
            except BlockingIOError:
                break
            if chunk == b"":
                # Should not happen with our RDWR keep-alive; treat as no data
                # rather than "writer closed" so start() does not abort early.
                break
            read_any = True
            self._append_lines(chunk)
        return True if read_any else None

    def _drain_pending(self, match_status: list[str]) -> list[str]:
        matched_status = [status for status in match_status if status in self._pending_lines]
        if matched_status:
            self._pending_lines = [line for line in self._pending_lines if line not in matched_status]
        return matched_status

    def _take_pending_matches(self, match_status: list[str]) -> list[str]:
        matched_status = self._drain_pending(match_status)
        if matched_status:
            return matched_status
        if not match_status and self._pending_lines:
            received_status = self._pending_lines
            self._pending_lines = []
            return received_status
        return []

    def discard_pending(self) -> None:
        """Drop buffered fifo lines so subsequent waits/assertions only see new events."""
        with self._condition:
            self._pending_lines.clear()
            self._read_buffer = b""

    def close(self) -> None:
        """Release the fifo read end."""
        with self._condition:
            try:
                os.close(self._fd)
            except OSError:
                pass
            self._condition.notify_all()

    def wait_for_status(self, timeout: float, match_status: list[str]) -> Optional[list[str]]:
        if match_status is None:
            match_status = []

        end_time = time.monotonic() + timeout
        poll_interval = 0.05

        with self._condition:
            while True:
                matched_status = self._take_pending_matches(match_status)
                if matched_status:
                    return matched_status

                timeout_remaining = end_time - time.monotonic()
                if timeout_remaining <= 0:
                    return []

                self._read_available()
                matched_status = self._take_pending_matches(match_status)
                if matched_status:
                    return matched_status

                # Wake when another waiter reads new lines, or poll the fifo periodically.
                self._condition.wait(timeout=min(timeout_remaining, poll_interval))


Connections = dict[str, List[Requirement]]


class EverestCore:
    """This class can be used to configure, start and stop a full build of EVerest
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
            temp_dir = Path(tempfile.mkdtemp(prefix=self.everest_uuid))
            temp_everest_config_file = tempfile.NamedTemporaryFile(
                delete=False, mode="w+", suffix=".yaml", dir=temp_dir)
            self.everest_config_path = Path(temp_everest_config_file.name)
            self.everest_core_user_config_path = Path(
                temp_everest_config_file.name).parent / 'user-config'
            self.everest_core_user_config_path.mkdir(parents=True, exist_ok=True)
            self._status_fifo_path = temp_dir / "status.fifo"
        else:
            config_dir = tmp_path / "everest_config"
            config_dir.mkdir()
            self.everest_core_user_config_path = config_dir / "user-config"
            self.everest_core_user_config_path.mkdir()
            self.everest_config_path = config_dir / "everest_config.yaml"
            self._status_fifo_path = tmp_path / "status.fifo"

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

        # Open the fifo read end before manager startup so tests can observe early
        # lifecycle messages while start() is running in a background thread.
        self.status_listener = StatusFifoListener(self._status_fifo_path)

    def _reset_status_listener(self) -> None:
        """Reopen the status fifo read end for a new manager process.

        After a manager exits the write end is closed and readers observe EOF.
        Reopening before the next start avoids treating that stale EOF as the end
        of the new run and ensures select() wakes when the new writer connects.
        """
        if self.status_listener is not None:
            self.status_listener.close()
        self.status_listener = StatusFifoListener(self._status_fifo_path)

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

        logging.info(self._status_fifo_path)

        args = [str(manager_path.resolve()), '--config', str(self.everest_config_path),
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

        # Reopen the fifo only when restarting after a previous manager run.
        # Resetting on every start() breaks lifecycle tests that call start() in a
        # background thread while the main thread waits on the same listener.
        if self.process is not None:
            self._reset_status_listener()

        self.process = subprocess.Popen(
            args, cwd=self.prefix_path, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        self.log_reader_thread = Thread(target=self.read_everest_log)
        self.log_reader_thread.start()

        expected_status = ManagerStatusFifo.ALL_MODULES_STARTED if standalone_module == None else ManagerStatusFifo.WAITING_FOR_STANDALONE_MODULES

        status = self.status_listener.wait_for_status(STARTUP_TIMEOUT, [expected_status])
        if not status:
            # Do not call read_everest_log() here: the log-reader thread already
            # owns stderr, and a blocking readline on a live process hangs forever.
            raise TimeoutError(
                f"Timeout while waiting for EVerest to start "
                f"(expected status-fifo message(s): {expected_status})"
            )

        logging.info("EVerest has started")
        if expected_status == ManagerStatusFifo.ALL_MODULES_STARTED:
            self.all_modules_started_event.set()

    def wait_for_manager_status(
        self,
        status: Union[str, List[str]],
        timeout_s: float = 60.0,
    ) -> None:
        if isinstance(status, str):
            status = [status]
        result = self.status_listener.wait_for_status(timeout_s, status)
        if not result:
            raise TimeoutError(f"Timeout waiting for manager status fifo message(s): {status}")

    def discard_manager_status_pending(self) -> None:
        """Drop buffered status-fifo lines (see StatusFifoListener.discard_pending)."""
        self.status_listener.discard_pending()

    def assert_no_manager_status(
        self,
        status: Union[str, List[str]],
        timeout_s: float = 5.0,
        *,
        ignore_pending: bool = False,
    ) -> None:
        if isinstance(status, str):
            status = [status]
        if ignore_pending:
            self.status_listener.discard_pending()
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            result = self.status_listener.wait_for_status(0.05, status)
            if result:
                raise AssertionError(f"Unexpected manager status fifo message(s): {result}")

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
        """Stops execution of EVerest by signaling SIGINT
        """
        logging.debug("CONTROLLER stop() function called...")
        if self.process:
            # NOTE (aw): we could also call process.kill()
            self.process.send_signal(SIGINT)
            self.process.wait()

        if self.log_reader_thread:
            self.log_reader_thread.join()

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

