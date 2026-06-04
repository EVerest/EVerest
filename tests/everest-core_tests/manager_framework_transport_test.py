#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import argparse
import os
import re
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid
from pathlib import Path


EXPECTED_VARIABLE = "framework-transport-variable-value"
EXPECTED_REQUEST = "framework-transport-command-request"
EXPECTED_RESPONSE = "framework-transport-command-response"
STARTUP_TIMEOUT_SECONDS = 20
SHUTDOWN_TIMEOUT_SECONDS = 8

VAR_RE = re.compile(r"FRAMEWORK_TRANSPORT_VAR value=(\S+)")
CMD_RE = re.compile(r"FRAMEWORK_TRANSPORT_CMD request=(\S+) response=(\S+)")
SHM_SHUTDOWN_ERROR_RE = re.compile(
    r"\[ERRO\].*SHM client error|SHM server liveness closed; control owner disconnected"
)
TEST_MODULES = ("TestFrameworkTransportProvider", "TestFrameworkTransportConsumer")


def reserve_tcp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def wait_for_tcp_port(port: int, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            if sock.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.05)
    raise RuntimeError(f"mosquitto did not open port {port}")


def start_mosquitto(temp_dir: Path) -> tuple[subprocess.Popen, int]:
    mosquitto = shutil.which("mosquitto")
    if mosquitto is None:
        raise RuntimeError("mosquitto broker executable is required for the MQTT equivalence run")

    port = reserve_tcp_port()
    config_path = temp_dir / "mosquitto.conf"
    log_path = temp_dir / "mosquitto.log"
    config_path.write_text(
        "\n".join(
            [
                f"listener {port} 127.0.0.1",
                "allow_anonymous true",
                "persistence false",
                f"log_dest file {log_path}",
                "log_type error",
                "",
            ]
        ),
        encoding="utf-8",
    )

    process = subprocess.Popen(
        [mosquitto, "-c", str(config_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
    )
    try:
        wait_for_tcp_port(port, 5)
    except Exception:
        process.terminate()
        try:
            output, _ = process.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate(timeout=2)
        raise RuntimeError(f"failed to start mosquitto: {output}")
    return process, port


def copy_tree(source: Path, destination: Path) -> None:
    if source.exists():
        shutil.copytree(source, destination, dirs_exist_ok=True)


def source_root() -> Path:
    return Path(__file__).resolve().parents[2]


def build_root_from_manager(manager: Path) -> Path:
    # build/<profile>/lib/everest/framework/src/manager
    return manager.parents[4]


def prepare_test_prefix(args: argparse.Namespace, temp_dir: Path) -> None:
    if args.prefix.exists():
        return

    root = source_root()
    build_root = build_root_from_manager(args.manager)
    prefix = temp_dir / "prefix"
    share_dir = prefix / "share" / "everest"
    etc_dir = prefix / "etc" / "everest"
    modules_dir = prefix / "libexec" / "everest" / "modules"

    (prefix / "bin").mkdir(parents=True)
    etc_dir.mkdir(parents=True)
    modules_dir.mkdir(parents=True)

    copy_tree(root / "lib" / "everest" / "framework" / "schemas", share_dir / "schemas")
    copy_tree(root / "interfaces", share_dir / "interfaces")
    copy_tree(root / "tests" / "everest-core_tests" / "interfaces", share_dir / "interfaces")
    copy_tree(root / "types", share_dir / "types")
    copy_tree(root / "errors", share_dir / "errors")
    copy_tree(root / "tests" / "everest-core_tests" / "errors", share_dir / "errors")
    (share_dir / "www").mkdir(parents=True)

    logging_cfg = root / "cmake" / "assets" / "logging.ini"
    shutil.copy2(logging_cfg, etc_dir / "default_logging.cfg")

    for module in TEST_MODULES:
        module_dir = modules_dir / module
        module_dir.mkdir(parents=True)
        shutil.copy2(root / "tests" / "everest-core_tests" / "modules" / module / "manifest.yaml", module_dir)
        module_binary = build_root / "tests" / "everest-core_tests" / "modules" / module / module
        shutil.copy2(module_binary, module_dir / module)

    args.prefix = prefix


def stop_process_group(process: subprocess.Popen, sig: signal.Signals, timeout_seconds: float) -> tuple[int, str]:
    if process.poll() is None:
        os.killpg(process.pid, sig)
    try:
        output, _ = process.communicate(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as exc:
        os.killpg(process.pid, signal.SIGKILL)
        output, _ = process.communicate(timeout=2)
        raise RuntimeError(f"process did not exit after {sig.name}") from exc
    return int(process.returncode), output


def write_config(temp_dir: Path, prefix: Path, transport: str, mqtt_port: int, run_id: str) -> Path:
    shm_socket_path = temp_dir / f"everest-framework-transport-{run_id}-{transport}.sock"
    config_path = temp_dir / f"config-{transport}.yaml"
    config_path.write_text(
        f"""settings:
  prefix: {prefix}
  telemetry_enabled: false
  framework_transport: {transport}
  mqtt_broker_host: 127.0.0.1
  mqtt_broker_port: {mqtt_port}
  mqtt_everest_prefix: everest_framework_transport_{run_id}_{transport}
  mqtt_external_prefix: external_framework_transport_{run_id}_{transport}
  shm_control_socket_path: {shm_socket_path}
  shm_topic_slots: 8
  shm_topic_slot_size: 4096
active_modules:
  transport_provider:
    module: TestFrameworkTransportProvider
  transport_consumer:
    module: TestFrameworkTransportConsumer
    connections:
      provider:
        - module_id: transport_provider
          implementation_id: main
""",
        encoding="utf-8",
    )
    return config_path


def parse_functional_result(transport: str, output: str) -> dict[str, str]:
    var_match = VAR_RE.search(output)
    cmd_match = CMD_RE.search(output)

    if var_match is None:
        raise AssertionError(f"{transport}: missing variable marker")
    if cmd_match is None:
        raise AssertionError(f"{transport}: missing command marker")

    result = {
        "variable": var_match.group(1),
        "request": cmd_match.group(1),
        "response": cmd_match.group(2),
    }
    expected = {
        "variable": EXPECTED_VARIABLE,
        "request": EXPECTED_REQUEST,
        "response": EXPECTED_RESPONSE,
    }
    if result != expected:
        raise AssertionError(f"{transport}: unexpected functional result {result}, expected {expected}")
    return result


def has_functional_result(output: str) -> bool:
    return VAR_RE.search(output) is not None and CMD_RE.search(output) is not None


def run_manager_scenario(args: argparse.Namespace, temp_dir: Path, transport: str, mqtt_port: int) -> dict[str, str]:
    run_id = uuid.uuid4().hex[:12]
    config_path = write_config(temp_dir, args.prefix, transport, mqtt_port, run_id)
    process = subprocess.Popen(
        [str(args.manager), "--prefix", str(args.prefix), "--config", str(config_path)],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        start_new_session=True,
        cwd=args.prefix / "bin",
    )

    output_chunks: list[str] = []
    deadline = time.monotonic() + STARTUP_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        ready, _, _ = select.select([process.stdout], [], [], 0.1)
        if ready:
            line = process.stdout.readline()
            if line:
                output_chunks.append(line)
                if has_functional_result("".join(output_chunks)):
                    break
            elif process.poll() is not None:
                break
        if process.poll() is not None:
            break

    if not has_functional_result("".join(output_chunks)):
        returncode, rest = stop_process_group(process, signal.SIGTERM, SHUTDOWN_TIMEOUT_SECONDS)
        output = "".join(output_chunks) + rest
        raise AssertionError(f"{transport}: Manager did not emit functional markers, returncode={returncode}\n{output}")

    returncode, rest = stop_process_group(process, signal.SIGINT, SHUTDOWN_TIMEOUT_SECONDS)
    output = "".join(output_chunks) + rest
    if returncode not in (0, -signal.SIGINT):
        raise AssertionError(f"{transport}: Manager exited with unexpected return code {returncode}\n{output}")
    if transport == "shm" and SHM_SHUTDOWN_ERROR_RE.search(output):
        raise AssertionError(f"{transport}: controlled shutdown emitted SHM client/server error\n{output}")

    return parse_functional_result(transport, output)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manager", type=Path, required=True)
    parser.add_argument("--prefix", type=Path, required=True)
    args = parser.parse_args()

    args.manager = args.manager.resolve()
    args.prefix = args.prefix.resolve()
    if not args.manager.exists():
        raise RuntimeError(f"manager binary does not exist: {args.manager}")

    with tempfile.TemporaryDirectory(prefix="everest-framework-transport-") as temp:
        temp_dir = Path(temp)
        prepare_test_prefix(args, temp_dir)
        broker, mqtt_port = start_mosquitto(temp_dir)
        try:
            results = {
                transport: run_manager_scenario(args, temp_dir, transport, mqtt_port)
                for transport in ("shm", "mqtt")
            }
        finally:
            stop_process_group(broker, signal.SIGTERM, 3)

    if results["shm"] != results["mqtt"]:
        raise AssertionError(f"SHM and MQTT functional results differ: {results}")

    print(f"FRAMEWORK_TRANSPORT_COMPARISON_OK {results['shm']}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as exc:
        print(exc, file=sys.stderr)
        sys.exit(1)
