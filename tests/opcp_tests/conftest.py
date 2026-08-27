# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Fixtures for the OPCP certificate enrollment tests: test PKI, mock OPCP server, per-test certificate store."""
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from copy import deepcopy
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List

import pytest

from everest.testing.core_utils.fixtures import *  # noqa: F401,F403 - everest_core & friends
from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import \
    EverestConfigAdjustmentStrategy

REPO_ROOT = Path(__file__).resolve().parents[2]
PKI_SCRIPT = REPO_ROOT / "lib/everest/opcp/tests/generate_test_pki.sh"
MOCK_SERVER = Path(__file__).resolve().parent / "mock_opcp_server.py"

CN_ISO2 = "DE*PNX*E12345*1"
CN_ISO20 = "DE-PNX-S-00000000000000000000000000000001-1"
CLIENT_ID = "test-client"
CLIENT_SECRET = "test-secret"


def pytest_configure(config):
    config.addinivalue_line("markers", "mock_opcp_options(*args): extra command line options for the mock OPCP server")
    config.addinivalue_line("markers", "opcp_module_config(**kwargs): OpcpCertificateManager config overrides")


def _everest_prefix(request) -> Path:
    prefix = Path(request.config.getoption("--everest-prefix"))
    if not prefix.is_absolute():
        for candidate in (Path.cwd() / prefix, request.config.rootpath / prefix, request.config.rootpath.parent / prefix):
            if candidate.exists():
                return candidate.resolve()
    return prefix


def _free_port() -> int:
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@pytest.fixture(scope="session")
def opcp_pki(tmp_path_factory) -> Path:
    """CPO test PKI (root, sub-CA 1, sub-CA 2) generated with openssl"""
    pki_dir = tmp_path_factory.mktemp("opcp_pki") / "pki"
    subprocess.run(["sh", str(PKI_SCRIPT), str(pki_dir)], check=True, capture_output=True)
    return pki_dir


@dataclass
class MockOpcp:
    base_url: str
    server_ca: Path
    pki_dir: Path
    requests_file: Path
    process: subprocess.Popen
    options: List[str] = field(default_factory=list)

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()

    def requests(self) -> List[Dict]:
        """Request log written by the mock on shutdown"""
        self.stop()
        if not self.requests_file.exists():
            return []
        return json.loads(self.requests_file.read_text())


@pytest.fixture
def mock_opcp(request, tmp_path, opcp_pki) -> MockOpcp:
    """Mock OPCP server; extra command line options via @pytest.mark.mock_opcp_options("--flag", ...)"""
    marker = request.node.get_closest_marker("mock_opcp_options")
    options = list(marker.args) if marker else []
    port = _free_port()
    out_dir = tmp_path / "mock"
    requests_file = tmp_path / "mock_requests.json"
    process = subprocess.Popen(
        [sys.executable, str(MOCK_SERVER), "--pki-dir", str(opcp_pki), "--out-dir", str(out_dir), "--port", str(port),
         "--client-id", CLIENT_ID, "--client-secret", CLIENT_SECRET, "--dump-requests", str(requests_file)] + options,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    deadline = time.time() + 10
    while time.time() < deadline:
        with socket.socket() as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                break
        if process.poll() is not None:
            raise RuntimeError("mock OPCP server exited: " + (process.stdout.read() or ""))
        time.sleep(0.1)
    else:
        process.kill()
        raise RuntimeError("mock OPCP server did not start listening")
    mock = MockOpcp(base_url=f"https://127.0.0.1:{port}", server_ca=out_dir / "server_ca.pem", pki_dir=opcp_pki,
                    requests_file=requests_file, process=process, options=options)
    yield mock
    mock.stop()


@pytest.fixture
def certs_dir(tmp_path) -> Path:
    """Empty evse_security store layout"""
    root = tmp_path / "certs"
    for sub in ("ca/csms", "ca/mf", "ca/mo", "ca/v2g", "client/csms", "client/cso"):
        (root / sub).mkdir(parents=True)
    return root


@pytest.fixture
def opcp_enroll(request) -> Path:
    binary = _everest_prefix(request) / "bin" / "opcp-enroll"
    if not binary.exists():
        pytest.skip(f"{binary} not installed")
    return binary


def run_opcp_enroll(binary: Path, mock: MockOpcp, certs_dir: Path, *extra: str, iso: str = "both",
                    expect_success: bool = True) -> subprocess.CompletedProcess:
    args = [str(binary), "--environment", "custom", "--api-url", mock.base_url,
            "--auth-url", mock.base_url + "/oauth/token", "--server-ca-bundle", str(mock.server_ca),
            "--client-id", CLIENT_ID, "--client-secret-env", "OPCP_TEST_SECRET",
            "--iso", iso, "--common-name-iso2", CN_ISO2, "--common-name-iso20", CN_ISO20,
            "--organization", "EVerest Test", "--country", "DE",
            "--manufacturer", "Pionix", "--device-name", "Test Charger", "--device-sw-version", "1.0",
            "--evse-serial", "SN1", "--ocpp-version", "2.0.1", "--charge-box-serial", "CB1",
            "--certs-dir", str(certs_dir)] + list(extra)
    env = dict(os.environ, OPCP_TEST_SECRET=CLIENT_SECRET)
    result = subprocess.run(args, env=env, capture_output=True, text=True, timeout=120)
    if expect_success:
        assert result.returncode == 0, f"opcp-enroll failed:\n{result.stdout}\n{result.stderr}"
    return result


def secc_chain_files(certs_dir: Path) -> List[Path]:
    return sorted((certs_dir / "client/cso").glob("CPO_CERT_SECC_LEAF*CHAIN*.pem"))


class OpcpTestConfigStrategy(EverestConfigAdjustmentStrategy):
    """Points EvseSecurity at the per-test store and OpcpCertificateManager at the mock server"""

    def __init__(self, certs_dir: Path, mock: MockOpcp, module_overrides: Dict):
        self.certs_dir = certs_dir
        self.mock = mock
        self.module_overrides = module_overrides

    def adjust_everest_configuration(self, everest_config: Dict) -> Dict:
        config = deepcopy(everest_config)
        security = config["active_modules"]["evse_security"]["config_module"]
        for key, value in list(security.items()):
            if key.endswith("_bundle") or key.endswith("_directory"):
                security[key] = str(self.certs_dir / value)
        manager = config["active_modules"]["opcp_certificate_manager"]["config_module"]
        manager["api_base_url"] = self.mock.base_url
        manager["server_ca_bundle"] = str(self.mock.server_ca)
        manager.update(self.module_overrides)
        return config


@pytest.fixture
def everest_config_strategies(request, certs_dir, mock_opcp) -> list:
    """Overrides the everest-testing fixture: wire the per-test store and mock into the EVerest config.
    Module config overrides via @pytest.mark.opcp_module_config(key=value, ...)."""
    marker = request.node.get_closest_marker("opcp_module_config")
    overrides = dict(marker.kwargs) if marker else {}
    return [OpcpTestConfigStrategy(certs_dir, mock_opcp, overrides)]
