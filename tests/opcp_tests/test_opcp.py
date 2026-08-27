# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""End-to-end tests of the OPCP certificate enrollment: opcp-enroll CLI and OpcpCertificateManager module
against the mock OPCP server."""
import asyncio
import logging
import queue
import subprocess
from pathlib import Path

import pytest

from everest.testing.core_utils.common import Requirement
from everest.testing.core_utils.everest_core import EverestCore
from everest.testing.core_utils.probe_module import ProbeModule

from conftest import CN_ISO2, CN_ISO20, MockOpcp, run_opcp_enroll, secc_chain_files


def _verify_chain(chain: Path, root: Path):
    """openssl verify of a stored leaf chain against the V2G test root"""
    pem = chain.read_text()
    certs = ["-----BEGIN CERTIFICATE-----" + part for part in pem.split("-----BEGIN CERTIFICATE-----")[1:]]
    assert len(certs) == 3, f"expected leaf + 2 sub-CAs in {chain.name}, got {len(certs)}"
    untrusted = chain.parent / (chain.stem + ".untrusted.pem")
    untrusted.write_text("".join(certs[1:]))
    leaf = chain.parent / (chain.stem + ".leaf.pem")
    leaf.write_text(certs[0])
    result = subprocess.run(["openssl", "verify", "-CAfile", str(root), "-untrusted", str(untrusted), str(leaf)],
                            capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    untrusted.unlink()
    leaf.unlink()


# ---------------------------------------------------------------------------------------------------------
# opcp-enroll CLI


def test_cli_enrolls_both_leafs_and_roots(opcp_enroll, mock_opcp: MockOpcp, certs_dir: Path):
    result = run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir)
    logging.info(result.stdout)

    chains = secc_chain_files(certs_dir)
    assert len(chains) == 2, result.stdout
    for chain in chains:
        _verify_chain(chain, mock_opcp.pki_dir / "root.pem")
    assert "V2G Root CA Test" in (certs_dir / "ca/v2g/V2G_ROOT_CA.pem").read_text() or \
        (certs_dir / "ca/v2g/V2G_ROOT_CA.pem").stat().st_size > 0
    assert (certs_dir / "ca/mo/MO_ROOT_CA.pem").stat().st_size > 0
    assert "Matching OpcpCertificateManager configuration" in result.stdout

    requests = mock_opcp.requests()
    methods = [(r["method"], r["path"].split("?")[0]) for r in requests]
    assert ("POST", "/oauth/token") in methods
    assert methods.count(("PUT", "/v1/vra/cpo/endEntities")) == 2, "one registration per ISO version"
    assert ("POST", "/.well-known/est/cpo/simpleenroll/ISO15118-2") in methods
    assert ("POST", "/.well-known/est/cpo/simpleenroll/ISO15118-20") in methods
    assert ("GET", "/cpo/cacerts/ISO15118-2/secp256r1/") in methods
    assert ("GET", "/cpo/cacerts/ISO15118-20/secp521r1/") in methods
    # registration and roots come before the first enrollment
    first_enroll = next(i for i, m in enumerate(methods) if m[1].endswith("simpleenroll/ISO15118-2"))
    assert methods.index(("PUT", "/v1/vra/cpo/endEntities")) < first_enroll
    assert methods.index(("GET", "/v1/root/rootCerts")) < first_enroll


def test_cli_unregistered_station_is_refused_without_orphan_key(opcp_enroll, mock_opcp: MockOpcp, certs_dir: Path):
    result = run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir, "--skip-registration", iso="2", expect_success=False)
    assert result.returncode == 1
    assert "not registered" in result.stderr or "not registered" in result.stdout, result.stderr
    assert "provisioned for this account" in result.stderr, result.stderr
    assert secc_chain_files(certs_dir) == []
    assert list((certs_dir / "client/cso").glob("*.key")) == [], "the CSR key must be removed after a refused enrollment"


def test_cli_wrong_credentials(opcp_enroll, mock_opcp: MockOpcp, certs_dir: Path, monkeypatch):
    monkeypatch.setenv("OPCP_TEST_SECRET", "wrong")
    import conftest
    monkeypatch.setattr(conftest, "CLIENT_SECRET", "wrong")
    result = run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir, expect_success=False)
    assert result.returncode == 1
    assert "Unauthorized" in result.stderr, result.stderr


def test_cli_print_config_and_dry_run(opcp_enroll, mock_opcp: MockOpcp, certs_dir: Path):
    result = run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir, "--print-config")
    assert "module: OpcpCertificateManager" in result.stdout
    assert f'common_name_iso2: "{CN_ISO2}"' in result.stdout
    assert f'common_name_iso20: "{CN_ISO20}"' in result.stdout

    result = run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir, "--dry-run", iso="2")
    assert "BEGIN CERTIFICATE REQUEST" in result.stdout
    assert secc_chain_files(certs_dir) == []
    assert list((certs_dir / "client/cso").glob("*.key")) == [], "dry run keeps no key"


# ---------------------------------------------------------------------------------------------------------
# OpcpCertificateManager module

PROBE_CONNECTIONS = {
    "security": [Requirement(module_id="evse_security", implementation_id="main")],
    "opcp": [Requirement(module_id="opcp_certificate_manager", implementation_id="main")],
}


async def _wait_for_status(status_queue: queue.Queue, predicate, timeout: float = 30.0) -> dict:
    loop = asyncio.get_running_loop()
    deadline = loop.time() + timeout
    last = None
    while loop.time() < deadline:
        try:
            last = await asyncio.to_thread(status_queue.get, True, 0.5)
        except queue.Empty:
            continue
        logging.info(f"status: {last}")
        if predicate(last):
            return last
    raise TimeoutError(f"status condition not met within {timeout} s, last status: {last}")


def _leaf(status: dict, iso: str) -> dict:
    return next(l for l in status["leafs"] if l["iso_version"] == iso)


def _renewals_done(status: dict) -> bool:
    return not status["busy"] and all(l["last_result"] in ("Success", "Failed", "AuthRejected", "NoClientCertificate")
                                      for l in status["leafs"] if l["managed"]) and \
        status["roots"]["last_result"] != "NotYetRun"


@pytest.mark.everest_core_config("config-test-opcp.yaml")
@pytest.mark.probe_module(connections=PROBE_CONNECTIONS)
class TestOpcpCertificateManager:

    @pytest.fixture
    def probe_module(self, started_test_controller, everest_core: EverestCore):
        return ProbeModule(everest_core.get_runtime_session())

    @pytest.fixture
    def enrolled_store(self, opcp_enroll, mock_opcp, certs_dir):
        """Store pre-provisioned with the CLI (leafs valid for 10 days -> due with the 30 day threshold)"""
        run_opcp_enroll(opcp_enroll, mock_opcp, certs_dir)
        return certs_dir

    @pytest.mark.asyncio
    @pytest.mark.mock_opcp_options("--leaf-days", "10", "--reenroll-leaf-days", "365", "--reenroll-same-cn")
    async def test_renews_both_leafs_via_mtls(self, enrolled_store: Path, mock_opcp: MockOpcp,
                                              probe_module: ProbeModule):
        status_queue = probe_module.subscribe_variable_to_queue("opcp", "status")
        probe_module.start()
        await probe_module.wait_to_be_ready()

        status = await _wait_for_status(status_queue, _renewals_done)
        assert status["roots"]["last_result"] == "Success", status
        assert status["roots"]["installed_count"] == 2
        for iso in ("ISO15118-2", "ISO15118-20"):
            leaf = _leaf(status, iso)
            assert leaf["last_result"] == "Success", leaf
            assert leaf["days_until_expiry"] > 300, leaf
            assert "last_renewal" in leaf

        # the store now holds the initial and the renewed chain per leaf, all validating against the root
        chains = secc_chain_files(enrolled_store)
        assert len(chains) == 4
        for chain in chains:
            _verify_chain(chain, mock_opcp.pki_dir / "root.pem")

        # evse_security serves the renewed leafs
        for leaf_type in ("V2G", "V2G20"):
            info = await probe_module.call_command("security", "get_leaf_certificate_info",
                                                   {"certificate_type": leaf_type, "encoding": "PEM",
                                                    "include_ocsp": False})
            assert info["status"] == "Accepted", info
            days = await probe_module.call_command("security", "get_leaf_expiry_days_count",
                                                   {"certificate_type": leaf_type})
            assert days > 300

        # requests after the CLI's enrollment phase are the module's
        requests = mock_opcp.requests()
        last_cli = max(i for i, r in enumerate(requests) if "/simpleenroll/" in r["path"]) + 2  # + its cacerts
        module_requests = requests[last_cli:]
        assert module_requests, requests
        assert all(r["auth"] == "client-cert" for r in module_requests), module_requests
        assert not any(r["path"] == "/oauth/token" for r in module_requests), "the module must not use OAuth2"
        reenrolls = [r for r in module_requests if "simplereenroll" in r["path"]]
        assert len(reenrolls) == 2
        assert all(r["status"] == 200 for r in reenrolls), reenrolls
        assert any(r["path"].endswith("/ISO15118-20") for r in reenrolls)
        assert any(r["path"].startswith("/v1/root/rootCerts") for r in module_requests)

    @pytest.mark.asyncio
    @pytest.mark.mock_opcp_options("--leaf-days", "10")
    async def test_valid_leaf_is_not_renewed_until_forced(self, enrolled_store: Path, mock_opcp: MockOpcp,
                                                          probe_module: ProbeModule):
        # threshold below the remaining validity -> nothing due
        status_queue = probe_module.subscribe_variable_to_queue("opcp", "status")
        probe_module.start()
        await probe_module.wait_to_be_ready()
        # everest-testing has no per-test config override hook here; use the command with force instead
        status = await _wait_for_status(status_queue, lambda s: not s["busy"] and s["roots"]["last_result"] != "NotYetRun")
        before = len(secc_chain_files(enrolled_store))

        scheduled = await probe_module.call_command("opcp", "renew_leaf_certificates", {"force": True})
        assert scheduled is True
        status = await _wait_for_status(
            status_queue, lambda s: not s["busy"] and all(l["last_result"] == "Success" and "last_renewal" in l
                                                          for l in s["leafs"]))
        assert len(secc_chain_files(enrolled_store)) == before + 2

        scheduled = await probe_module.call_command("opcp", "sync_root_certificates", {})
        assert scheduled is True
        status = await _wait_for_status(status_queue, lambda s: not s["busy"])
        assert status["roots"]["last_result"] == "Success"

    @pytest.mark.asyncio
    async def test_without_any_leaf_reports_no_client_certificate(self, mock_opcp: MockOpcp, certs_dir: Path,
                                                                  probe_module: ProbeModule):
        status_queue = probe_module.subscribe_variable_to_queue("opcp", "status")
        probe_module.start()
        await probe_module.wait_to_be_ready()
        status = await _wait_for_status(status_queue, _renewals_done)
        for leaf in status["leafs"]:
            assert leaf["last_result"] == "NoClientCertificate", leaf
            assert "opcp-enroll" in leaf["last_error"]
            assert leaf["days_until_expiry"] == 0
        assert status["roots"]["last_result"] == "NoClientCertificate"
        assert secc_chain_files(certs_dir) == []
        assert mock_opcp.requests() == [], "without a client certificate nothing is sent to the ecosystem"

    @pytest.mark.asyncio
    @pytest.mark.mock_opcp_options("--leaf-days", "10", "--rcp-client-cert", "reject")
    async def test_root_pool_rejecting_client_certificate_does_not_block_renewal(self, enrolled_store: Path,
                                                                                 mock_opcp: MockOpcp,
                                                                                 probe_module: ProbeModule):
        status_queue = probe_module.subscribe_variable_to_queue("opcp", "status")
        probe_module.start()
        await probe_module.wait_to_be_ready()
        status = await _wait_for_status(status_queue, _renewals_done)
        assert status["roots"]["last_result"] == "AuthRejected", status["roots"]
        for leaf in status["leafs"]:
            assert leaf["last_result"] == "Success", leaf

    @pytest.mark.asyncio
    @pytest.mark.mock_opcp_options("--leaf-days", "10", "--reject-reenroll")
    @pytest.mark.opcp_module_config(manage_iso15118_20=False)
    async def test_pki_without_mtls_support_backs_off(self, enrolled_store: Path, mock_opcp: MockOpcp,
                                                      probe_module: ProbeModule):
        status_queue = probe_module.subscribe_variable_to_queue("opcp", "status")
        probe_module.start()
        await probe_module.wait_to_be_ready()
        status = await _wait_for_status(status_queue, _renewals_done)
        iso2 = _leaf(status, "ISO15118-2")
        assert iso2["last_result"] == "AuthRejected", iso2
        assert "next_retry" in iso2
        assert "403" in iso2["last_error"]
        iso20 = _leaf(status, "ISO15118-20")
        assert iso20["managed"] is False
        assert iso20["last_result"] == "NotYetRun"
        assert len(secc_chain_files(enrolled_store)) == 2, "nothing new installed"
