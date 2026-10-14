# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

from __future__ import annotations

import asyncio
import ctypes
import hashlib
import pytest
import queue
import os
from pathlib import Path
import threading
from types import FunctionType
from typing import List, Optional, Tuple

from datetime import datetime, timedelta, timezone

from cryptography import x509
from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec, rsa
from cryptography.hazmat.primitives.asymmetric.utils import decode_dss_signature
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.kdf.concatkdf import ConcatKDFHash
from cryptography.x509 import load_pem_x509_certificate
from cryptography.x509.oid import NameOID

from copy import deepcopy

from everest.testing.core_utils.common import OCPPVersion
from everest.testing.core_utils._configuration.everest_configuration_strategies.everest_configuration_strategy import (
    EverestConfigAdjustmentStrategy,
)
from everest.testing.core_utils._configuration.libocpp_configuration_helper import (
    GenericOCPP2XConfigAdjustment,
    OCPP2XConfigVariableIdentifier,
)

import json
import base64

from everest.testing.ocpp_utils.charge_point_utils import (
    OcppTestConfiguration,
    ChargePointInfo,
    CertificateInfo,
    FirmwareInfo,
    AuthorizationInfo,
    load_private_key,
)

from ocpp.charge_point import snake_to_camel_case, asdict, remove_nones
from ocpp.messages import _DecimalEncoder
from ocpp.v16 import call, call_result
from ocpp.v201 import call as call201
from ocpp.v16.enums import Action, DataTransferStatus
from ocpp.v201.enums import Action as Action201
from ocpp.routing import on

# for OCPP1.6 PnC whitepaper:
from ocpp.v201 import call_result as call_result201
from ocpp.v201.datatypes import IdTokenInfoType
from ocpp.v201.enums import (
    AuthorizationStatusEnumType,
    GenericStatusEnumType,
    Iso15118EVCertificateStatusEnumType,
    GetCertificateStatusEnumType,
)


# GetCertificateStatusResponse.ocspResult is a DER encoded OCSPResponse (RFC 6960), base64 encoded. This is the
# smallest well-formed one: responseStatus tryLater without responseBytes, i.e. the responder has no status yet.
OCSP_RESULT_TRY_LATER = "MAMKAQM="

# NOTE: The module name and the `Mode` enum values below are duplicated from
# `modules/EVSE/OCPPmulti/manifest.yaml`. They must stay in sync with that
# manifest: `OCPP_MULTI_MODULE_NAME` matches the module directory name and the
# values here must match the `Mode` config option's enum entries.
OCPP_MULTI_MODULE_NAME = "OCPPmulti"

OCPP_VERSION_TO_MULTI_MODE = {
    OCPPVersion.ocpp16: "Only1.6",
    OCPPVersion.ocpp201: "Only2",
    OCPPVersion.ocpp21: "Only2",
}


class OCPPMultiConfigurationStrategy(EverestConfigAdjustmentStrategy):
    """Rewrites the EVerest config so the OCPP module is the combined `OCPPmulti`
    module instead of legacy `OCPP`/`OCPP201`. Must run AFTER the framework's
    OCPPModuleConfigurationStrategy so the temporary libocpp paths are already
    present in `config_module` (this strategy only renames the module and sets `Mode`)."""

    def __init__(self, ocpp_version: OCPPVersion, ocpp_module_id: str = "ocpp"):
        self._ocpp_version = ocpp_version
        self._ocpp_module_id = ocpp_module_id

    def adjust_everest_configuration(self, everest_config: dict) -> dict:
        adjusted = deepcopy(everest_config)
        assert "active_modules" in adjusted and self._ocpp_module_id in adjusted["active_modules"], \
            f"OCPP module id '{self._ocpp_module_id}' missing from EVerest config"
        module_config = adjusted["active_modules"][self._ocpp_module_id]
        assert module_config["module"] in ("OCPP", "OCPP201"), \
            f"OCPPMultiConfigurationStrategy expected legacy 'OCPP'/'OCPP201' module, got '{module_config['module']}'"
        module_config["module"] = OCPP_MULTI_MODULE_NAME
        module_config.setdefault("config_module", {})
        module_config["config_module"]["Mode"] = OCPP_VERSION_TO_MULTI_MODE[self._ocpp_version]
        if self._ocpp_version == OCPPVersion.ocpp16:
            module_config["config_module"]["EnableLegacyConfigMigration"] = True
        return adjusted


class OCPPMultiModuleConfigStrategy(EverestConfigAdjustmentStrategy):
    """Sets additional `config_module` keys on the OCPP module, for OCPPmulti-only options such as
    `DelegateNetworkConfigurationToSystem`. Use together with `ocpp_multi_only`: the keys are applied before the
    rename to `OCPPmulti` and survive it because the framework strategies merge `config_module`."""

    def __init__(self, config_module: dict, ocpp_module_id: str = "ocpp"):
        self._config_module = config_module
        self._ocpp_module_id = ocpp_module_id

    def adjust_everest_configuration(self, everest_config: dict) -> dict:
        adjusted = deepcopy(everest_config)
        assert "active_modules" in adjusted and self._ocpp_module_id in adjusted["active_modules"], \
            f"OCPP module id '{self._ocpp_module_id}' missing from EVerest config"
        module_config = adjusted["active_modules"][self._ocpp_module_id]
        module_config.setdefault("config_module", {})
        module_config["config_module"].update(self._config_module)
        return adjusted


EXI_LIBRARY_NAME = "libcbv2g_json_wrapper.so"


def find_exi_library(everest_prefix) -> str:
    """The cbv2g JSON wrapper installed into an EVerest prefix."""
    prefix = Path(everest_prefix).resolve()
    for libdir in ("lib", "lib64"):
        candidate = prefix / libdir / EXI_LIBRARY_NAME
        if candidate.exists():
            return str(candidate)
    raise FileNotFoundError(f"{EXI_LIBRARY_NAME} is not installed in {prefix}")


class EXIGenerator:
    """Builds the ISO 15118-2 CertificateInstallationRes a certificate provisioning service returns.

    The EXI codec is libcbv2g_json_wrapper; the contract private key
    encryption ([V2G2-818]) and the CPS signature over the SignedInfo ([V2G2-771]) are done here.
    """

    # Paths below the test PKI root, as the EVerest certificate layout has them.
    OEM_LEAF_DER = "client/oem/OEM_LEAF.der"
    CONTRACT_LEAF_DER = "client/mo/MO_LEAF.der"
    CONTRACT_LEAF_KEY = "client/mo/MO_LEAF.key"
    CONTRACT_LEAF_KEY_PASSWORD = "client/mo/MO_LEAF_PASSWORD.txt"
    MO_SUB_CA2_DER = "ca/mo/MO_SUB_CA2.der"
    MO_SUB_CA1_DER = "ca/mo/MO_SUB_CA1.der"
    CPS_LEAF_DER = "client/cps/CPS_LEAF.der"
    CPS_LEAF_KEY = "client/cps/CPS_LEAF.key"
    CPS_LEAF_KEY_PASSWORD = "client/cps/CPS_LEAF_PASSWORD.txt"
    CPS_SUB_CA2_DER = "ca/cps/CPS_SUB_CA2.der"
    CPS_SUB_CA1_DER = "ca/cps/CPS_SUB_CA1.der"

    ISO2_NAMESPACE = "urn:iso:15118:2:2013:MsgDef"
    XMLDSIG_NAMESPACE = "http://www.w3.org/2000/09/xmldsig#"
    CANONICAL_EXI = "http://www.w3.org/TR/canonical-exi/"
    MAX_EXI_SIZE = 8192
    MAX_JSON_SIZE = 65536

    def __init__(self, certs_path, library_path: str):
        self.certs_path = certs_path
        self._lib = ctypes.CDLL(library_path)
        self._declare_functions()

        self.oem_leaf = x509.load_der_x509_certificate(self._read(self.OEM_LEAF_DER))
        self.contract_leaf_key = self._load_key(self.CONTRACT_LEAF_KEY, self.CONTRACT_LEAF_KEY_PASSWORD)
        self.signature_key = self._load_key(self.CPS_LEAF_KEY, self.CPS_LEAF_KEY_PASSWORD)
        contract_leaf = self._read(self.CONTRACT_LEAF_DER)
        self.contract_cert_chain = [contract_leaf, self._read(self.MO_SUB_CA2_DER), self._read(self.MO_SUB_CA1_DER)]
        self.cps_certificate_chain = [
            self._read(self.CPS_LEAF_DER),
            self._read(self.CPS_SUB_CA2_DER),
            self._read(self.CPS_SUB_CA1_DER),
        ]
        self.emaid = (
            x509.load_der_x509_certificate(contract_leaf)
            .subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0]
            .value
        )

    def _read(self, relative_path: str) -> bytes:
        with open(os.path.join(self.certs_path, relative_path), "rb") as f:
            return f.read()

    def _load_key(self, key_path: str, password_path: str):
        with open(os.path.join(self.certs_path, password_path), "r") as f:
            password = f.readline().rstrip().encode("utf-8")
        return serialization.load_pem_private_key(self._read(key_path), password=password or None)

    def _declare_functions(self):
        byte_p = ctypes.POINTER(ctypes.c_uint8)
        self._lib.cbv2g_encode.argtypes = [
            ctypes.c_char_p, ctypes.c_char_p, byte_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self._lib.cbv2g_encode.restype = ctypes.c_int
        self._lib.cbv2g_decode.argtypes = [
            byte_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        self._lib.cbv2g_decode.restype = ctypes.c_int
        self._lib.cbv2g_get_last_error.argtypes = []
        self._lib.cbv2g_get_last_error.restype = ctypes.c_char_p

    def _check_result(self, result):
        if result != 0:
            error = self._lib.cbv2g_get_last_error().decode("utf-8")
            raise RuntimeError(f"cbv2g JSON wrapper failed with {result}: {error}")

    def encode(self, message: dict, namespace: str = ISO2_NAMESPACE) -> bytes:
        out = (ctypes.c_uint8 * self.MAX_EXI_SIZE)()
        out_len = ctypes.c_size_t()
        self._check_result(self._lib.cbv2g_encode(
            json.dumps(message).encode("utf-8"), namespace.encode("utf-8"), out, len(out), ctypes.byref(out_len)))
        return bytes(out[:out_len.value])

    def decode(self, exi: bytes, namespace: str) -> dict:
        data = (ctypes.c_uint8 * len(exi)).from_buffer_copy(exi)
        out = ctypes.create_string_buffer(self.MAX_JSON_SIZE)
        self._check_result(self._lib.cbv2g_decode(data, len(data), namespace.encode("utf-8"), out, len(out)))
        return json.loads(out.value)

    def create_signed_info(self, response: dict) -> dict:
        references = []
        for name in ("ContractSignatureCertChain", "ContractSignatureEncryptedPrivateKey", "DHpublickey", "eMAID"):
            element = response[name]
            digest = hashlib.sha256(self.encode({name: element})).digest()
            references.append({
                "URI": "#" + element["Id"],
                "Transforms": {"Transform": [{"Algorithm": self.CANONICAL_EXI}]},
                "DigestMethod": {"Algorithm": "http://www.w3.org/2001/04/xmlenc#sha256"},
                "DigestValue": base64.b64encode(digest).decode("ascii"),
            })
        return {
            "CanonicalizationMethod": {"Algorithm": self.CANONICAL_EXI},
            "SignatureMethod": {"Algorithm": "http://www.w3.org/2001/04/xmldsig-more#ecdsa-sha256"},
            "Reference": references,
        }

    def _encrypt_contract_private_key(self) -> Tuple[bytes, bytes]:
        """ISO 15118-2 [V2G2-818]: ECDH with the OEM provisioning key, ConcatKDF-SHA256, AES-128-CBC."""
        ephemeral_key = ec.generate_private_key(ec.SECP256R1())
        dh_public_key = ephemeral_key.public_key().public_bytes(
            encoding=serialization.Encoding.X962,
            format=serialization.PublicFormat.UncompressedPoint,
        )
        shared_secret = ephemeral_key.exchange(ec.ECDH(), self.oem_leaf.public_key())
        session_key = ConcatKDFHash(
            algorithm=hashes.SHA256(), length=16, otherinfo=bytes([0x01, 0x55, 0x56])
        ).derive(shared_secret)
        init_vector = os.urandom(16)
        encryptor = Cipher(algorithms.AES(session_key), modes.CBC(init_vector)).encryptor()
        private_value = self.contract_leaf_key.private_numbers().private_value.to_bytes(32, "big")
        encrypted = init_vector + encryptor.update(private_value) + encryptor.finalize()
        return dh_public_key, encrypted

    @staticmethod
    def _chain(certs: List[bytes]) -> dict:
        return {
            "Certificate": base64.b64encode(certs[0]).decode("ascii"),
            "SubCertificates": {"Certificate": [base64.b64encode(cert).decode("ascii") for cert in certs[1:]]},
        }

    def generate_certificate_installation_res(
        self, base64_encoded_cert_installation_req: str, namespace: str
    ) -> str:
        request = self.decode(base64.b64decode(base64_encoded_cert_installation_req), namespace)
        session_id = request["V2G_Message"]["Header"]["SessionID"]
        dh_public_key, encrypted_private_key = self._encrypt_contract_private_key()
        response = {
            "ResponseCode": "OK",
            "SAProvisioningCertificateChain": self._chain(self.cps_certificate_chain),
            "ContractSignatureCertChain": {"Id": "id1", **self._chain(self.contract_cert_chain)},
            "ContractSignatureEncryptedPrivateKey": {
                "Id": "id2", "value": base64.b64encode(encrypted_private_key).decode("ascii")},
            "DHpublickey": {"Id": "id3", "value": base64.b64encode(dh_public_key).decode("ascii")},
            "eMAID": {"Id": "id4", "value": self.emaid},
        }
        signed_info = self.create_signed_info(response)
        signed_info_exi = self.encode({"SignedInfo": signed_info}, self.XMLDSIG_NAMESPACE)
        r, s = decode_dss_signature(self.signature_key.sign(signed_info_exi, ec.ECDSA(hashes.SHA256())))
        signature_value = r.to_bytes(32, "big") + s.to_bytes(32, "big")
        message = {"V2G_Message": {
            "Header": {
                "SessionID": session_id,
                "Signature": {
                    "SignedInfo": signed_info,
                    "SignatureValue": {"value": base64.b64encode(signature_value).decode("ascii")},
                },
            },
            "Body": {"CertificateInstallationRes": response},
        }}
        return base64.b64encode(self.encode(message)).decode("ascii")


def certificate_signed_response(csr: x509.CertificateSigningRequest):
    certs_path: str = Path(__file__).parent.resolve() / "everest-aux/certs/"
    ca_cert_file = certs_path / "ca/v2g/V2G_ROOT_CA.pem"
    ca_key_file = certs_path / "client/v2g/V2G_ROOT_CA.key"

    with open(ca_cert_file, "rb") as ca_cert_file, open(
        ca_key_file, "rb"
    ) as ca_key_file:
        ca_cert_data = ca_cert_file.read()
        ca_key_data = ca_key_file.read()

    ca_cert = x509.load_pem_x509_certificate(ca_cert_data)
    ca_key = load_private_key(ca_key_data, b"123456")

    validity_days = 365
    not_before = datetime.now(timezone.utc)
    not_after = not_before + timedelta(days=validity_days)

    signed_cert = (
        x509.CertificateBuilder()
        .serial_number(1)
        .subject_name(csr.subject)
        .issuer_name(ca_cert.subject)
        .public_key(csr.public_key())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .sign(ca_key, hashes.SHA256())
    )

    return signed_cert.public_bytes(serialization.Encoding.PEM).decode("utf-8")


def on_data_transfer(accept_pnc_authorize, exi_generator: EXIGenerator, **kwargs):
    req = call.DataTransfer(**kwargs)
    if req.vendor_id == "org.openchargealliance.iso15118pnc":
        if req.message_id == "Authorize":
            if accept_pnc_authorize:
                status = AuthorizationStatusEnumType.accepted
            else:
                status = AuthorizationStatusEnumType.invalid
            response = call_result201.Authorize(
                id_token_info=IdTokenInfoType(status=status)
            )
            return call_result.DataTransfer(
                status=DataTransferStatus.accepted,
                data=json.dumps(remove_nones(
                    snake_to_camel_case(asdict(response)))),
            )
        # Should not be part of DataTransfer.req from CP->CSMS
        elif req.message_id == "CertificateSigned":
            return call_result.DataTransfer(
                status=DataTransferStatus.unknown_message_id, data="Please implement me"
            )
        # Should not be part of DataTransfer.req from CP->CSMS
        elif req.message_id == "DeleteCertificate":
            return call_result.DataTransfer(
                status=DataTransferStatus.unknown_message_id, data="Please implement me"
            )
        elif req.message_id == "Get15118EVCertificate":
            data_payload = json.loads(kwargs["data"])
            exi_request = data_payload["exiRequest"]
            namespace = data_payload["iso15118SchemaVersion"]
            return call_result.DataTransfer(
                status=DataTransferStatus.accepted,
                data=json.dumps(
                    remove_nones(
                        snake_to_camel_case(
                            asdict(
                                call_result201.Get15118EVCertificate(
                                    status=Iso15118EVCertificateStatusEnumType.accepted,
                                    exi_response=exi_generator.generate_certificate_installation_res(
                                        exi_request, namespace
                                    ),
                                )
                            )
                        )
                    )
                ),
            )
        elif req.message_id == "GetCertificateStatus":
            return call_result.DataTransfer(
                status=DataTransferStatus.accepted,
                data=json.dumps(
                    remove_nones(
                        snake_to_camel_case(
                            asdict(
                                call_result201.GetCertificateStatus(
                                    status=GetCertificateStatusEnumType.accepted,
                                    ocsp_result=OCSP_RESULT_TRY_LATER,
                                )
                            )
                        )
                    )
                ),
            )
        # Should not be part of DataTransfer.req from CP->CSMS
        elif req.message_id == "InstallCertificate":
            return call_result.DataTransfer(
                status=DataTransferStatus.unknown_message_id, data="Please implement me"
            )
        elif req.message_id == "SignCertificate":
            return call_result.DataTransfer(
                status=DataTransferStatus.accepted,
                data=json.dumps(
                    asdict(
                        call_result201.SignCertificate(
                            status=GenericStatusEnumType.accepted
                        )
                    )
                ),
            )
        # Should not be part of DataTransfer.req from CP->CSMS
        elif req.message_id == "TriggerMessage":
            return call_result.DataTransfer(
                status=DataTransferStatus.unknown_message_id, data="Please implement me"
            )
        else:
            return call_result.DataTransfer(
                status=DataTransferStatus.unknown_message_id, data="Please implement me"
            )
    else:
        return call_result.DataTransfer(
            status=DataTransferStatus.unknown_vendor_id, data="Please implement me"
        )


def make_on_data_transfer_accept_authorize(exi_generator: EXIGenerator):
    @on(Action.data_transfer)
    def handler(**kwargs):
        return on_data_transfer(accept_pnc_authorize=True, exi_generator=exi_generator, **kwargs)
    return handler


def make_on_data_transfer_reject_authorize(exi_generator: EXIGenerator):
    @on(Action.data_transfer)
    def handler(**kwargs):
        return on_data_transfer(accept_pnc_authorize=False, exi_generator=exi_generator, **kwargs)
    return handler


def make_on_get_15118_ev_certificate(exi_generator: EXIGenerator):
    @on(Action201.get_15118_ev_certificate)
    def handler(**kwargs):
        payload = call201.Get15118EVCertificate(**kwargs)
        return call_result201.Get15118EVCertificate(
            status=GenericStatusEnumType.accepted,
            exi_response=exi_generator.generate_certificate_installation_res(
                payload.exi_request, payload.iso15118_schema_version
            ),
        )
    return handler


def get_everest_config_path_str(config_name):
    return (Path(__file__).parent / "everest-aux" / "config" / config_name).as_posix()


def parametrize_secc_config(d20_config: str, evsev2g_config: str):
    """Run an ISO 15118 test against both SECC stacks: Evse15118D20 and the
    legacy EvseV2G.

    The everest_core_config marker is carried per param; it overrides a
    class-level config marker, but a function-level one would win over it,
    so parametrized tests must not keep a function-level config marker.
    """
    return pytest.mark.parametrize(
        "secc_config",
        [
            pytest.param(
                "evse15118d20",
                id="Evse15118D20",
                marks=pytest.mark.everest_core_config(
                    get_everest_config_path_str(d20_config)
                ),
            ),
            pytest.param(
                "evsev2g",
                id="EvseV2G",
                marks=pytest.mark.everest_core_config(
                    get_everest_config_path_str(evsev2g_config)
                ),
            ),
        ],
    )


def get_everest_config(function_name, module_name):
    if module_name == "plug_and_charge_tests":
        return Path(__file__).parent / Path(
            "everest-aux/config/everest-config-sil-iso.yaml"
        )
    elif module_name in [
        "provisioning",
        "authorization",
        "remote_control",
        "security",
        "local_authorization_list",
        "transactions",
        "meterValues",
        "reservations",
        "everest_device_model"
    ]:
        return Path(__file__).parent / Path(
            "everest-aux/config/everest-config-ocpp201.yaml"
        )
    else:
        return Path(__file__).parent / Path(
            "everest-aux/config/everest-config-sil-ocpp.yaml"
        )


def load_test_config() -> OcppTestConfiguration:
    data = json.loads((Path(__file__).parent / "test_config.json").read_text())

    ocpp_test_config = OcppTestConfiguration(
        charge_point_info=ChargePointInfo(**data["charge_point_info"]),
        authorization_info=AuthorizationInfo(**data["authorization_info"]),
        certificate_info=CertificateInfo(**data["certificate_info"]),
        firmware_info=FirmwareInfo(**data["firmware_info"]),
    )

    ocpp_test_config.certificate_info.csms_cert = (
        Path(__file__).parent / ocpp_test_config.certificate_info.csms_cert
    )
    ocpp_test_config.certificate_info.csms_key = (
        Path(__file__).parent / ocpp_test_config.certificate_info.csms_key
    )
    ocpp_test_config.certificate_info.csms_root_ca = (
        Path(__file__).parent / ocpp_test_config.certificate_info.csms_root_ca
    )
    ocpp_test_config.certificate_info.csms_root_ca_invalid = (
        Path(__file__).parent /
        ocpp_test_config.certificate_info.csms_root_ca_invalid
    )
    ocpp_test_config.certificate_info.csms_root_ca_key = (
        Path(__file__).parent /
        ocpp_test_config.certificate_info.csms_root_ca_key
    )
    ocpp_test_config.certificate_info.mf_root_ca = (
        Path(__file__).parent / ocpp_test_config.certificate_info.mf_root_ca
    )

    ocpp_test_config.firmware_info.update_file = (
        Path(__file__).parent / ocpp_test_config.firmware_info.update_file
    )
    ocpp_test_config.firmware_info.update_file_signature = (
        Path(__file__).parent /
        ocpp_test_config.firmware_info.update_file_signature
    )
    if ocpp_test_config.firmware_info.update_file_keep_connectors_available is not None:
        ocpp_test_config.firmware_info.update_file_keep_connectors_available = (
            Path(__file__).parent /
            ocpp_test_config.firmware_info.update_file_keep_connectors_available
        )
    if ocpp_test_config.firmware_info.update_file_keep_connectors_available_signature is not None:
        ocpp_test_config.firmware_info.update_file_keep_connectors_available_signature = (
            Path(__file__).parent /
            ocpp_test_config.firmware_info.update_file_keep_connectors_available_signature
        )

    return ocpp_test_config


async def call_test_function_and_wait(test_function: FunctionType, timeout=20) -> bool:
    q = queue.Queue()

    def tst(q):
        res = test_function(timeout)
        q.put(res)

    test_thread = threading.Thread(target=tst, kwargs={"q": q})
    test_thread.start()

    result = False
    while q.empty():
        await asyncio.sleep(1)

    result = q.get()

    return result


async def send_message_without_validation(charge_point, call_msg):
    json_data = json.dumps(
        [
            call_msg.message_type_id,
            call_msg.unique_id,
            call_msg.action,
            call_msg.payload,
        ],
        # By default json.dumps() adds a white space after every separator.
        # By setting the separator manually that can be avoided.
        separators=(",", ":"),
        cls=_DecimalEncoder,
    )

    async with charge_point._call_lock:
        await charge_point._send(json_data)


class CertificateHashDataGenerator:
    """
    Compute the hash values for certificates.

    Note: EVSE Security uses the X509_pubkey_digest OpenSSL function for this.

    The hashes are not generated from the whole DER-encoded "Subject Public Key Information"
    field, but rather only from the bit-string representing the actual key bits (without the ASN.1
    tag and length).

    Unfortunately, there doesn't seem to be a generic method for
    doing this, so RSA and ECDSA keys are handled differently.
    If we need to add support for Ed25519 or others, we'd need to
    extend the logic here as well.

    Cf:
    - https://groups.google.com/g/mailing.openssl.users/c/1hhY2uECxsc
    - https://github.com/openssl/openssl/issues/8777
    - https://datatracker.ietf.org/doc/html/rfc5480

    """

    @staticmethod
    def _sha256(b: bytes) -> str:
        return hashlib.sha256(b).hexdigest()

    @classmethod
    def get_hash_data(
        cls, certificate_path: Path, issuer_certificate_path: Optional[Path] = None
    ):
        issuer_certificate_path = (
            issuer_certificate_path if issuer_certificate_path else certificate_path
        )

        certificate = load_pem_x509_certificate(
            certificate_path.read_bytes(), default_backend()
        )
        issuer_certificate = load_pem_x509_certificate(
            issuer_certificate_path.read_bytes(), default_backend()
        )

        issuer_name_hash = cls._get_name_hash(issuer_certificate)
        issuer_key_hash = cls._get_public_key_hash(issuer_certificate_path)

        assert issuer_name_hash == cls._get_issuer_name_hash(certificate)

        return {
            "hash_algorithm": "SHA256",
            "issuer_key_hash": issuer_key_hash,
            "issuer_name_hash": issuer_name_hash,
            "serial_number": hex(certificate.serial_number)[2:].lower(),
            # strip 0x according to OCPP spec (CertificateHashDataType)
        }

    @classmethod
    def _get_public_key_hash(cls, file: Path):
        certificate = load_pem_x509_certificate(
            file.read_bytes(), default_backend())
        # Get the raw key bytes - the method to do this differs by key type
        # try RSA
        try:
            return cls._sha256(
                certificate.public_key().public_bytes(
                    encoding=serialization.Encoding.DER,
                    format=serialization.PublicFormat.PKCS1,
                )
            )
        # try ECDSA (Note: We assume we're working with the uncompressed-point format here)
        except Exception:
            return cls._sha256(
                certificate.public_key().public_bytes(
                    encoding=serialization.Encoding.X962,
                    format=serialization.PublicFormat.UncompressedPoint,
                )
            )
        # if ECDSA also fails, then we need to adjust this method to add more options (e.g. Ed25519)

    @classmethod
    def _get_name_hash(cls, certificate: x509.Certificate):
        return cls._sha256(certificate.subject.public_bytes())

    @classmethod
    def _get_issuer_name_hash(cls, certificate: x509.Certificate):
        return cls._sha256(certificate.issuer.public_bytes())


class CertificateHelper:

    @staticmethod
    def _verify_private_key_matches_cert(private_key, cert: x509.Certificate):
        cert_public_key = cert.public_key().public_bytes(
            encoding=serialization.Encoding.DER,
            format=serialization.PublicFormat.SubjectPublicKeyInfo,
        )
        pkey_public_key = private_key.public_key().public_bytes(
            encoding=serialization.Encoding.DER,
            format=serialization.PublicFormat.SubjectPublicKeyInfo,
        )

        assert (
            cert_public_key == pkey_public_key
        ), f"Private key is for {pkey_public_key}; certificat has public key {pkey_public_key}"

    @classmethod
    def generate_certificate_request(
        cls, common_name: str, passphrase: str | bytes | None = None
    ) -> tuple[str, str]:
        """
        Returns: tuple of certificate request and private key
        """

        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        req = (
            x509.CertificateSigningRequestBuilder()
            .subject_name(
                x509.Name(
                    [
                        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
                        x509.NameAttribute(NameOID.COUNTRY_NAME, "DE"),
                    ]
                )
            )
            .sign(key, hashes.SHA256())
        )
        csr_data = req.public_bytes(serialization.Encoding.PEM)
        if isinstance(passphrase, str):
            passphrase = passphrase.encode("utf-8")
        private_key = key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=(
                serialization.BestAvailableEncryption(passphrase)
                if passphrase
                else serialization.NoEncryption()
            ),
        )
        return csr_data.decode("utf-8"), private_key.decode("utf-8")

    @classmethod
    def sign_certificate_request(
        cls,
        csr_data: str | bytes,
        issuer_certificate_path: Path,
        issuer_private_key_path: Path,
        issuer_private_key_passphrase: str | bytes | None = None,
        relative_valid_time: int = 0,
        relative_expiration_time: int = 9999999,
        serial: int = 42,
    ) -> str:

        if isinstance(issuer_private_key_passphrase, str):
            issuer_private_key_passphrase = issuer_private_key_passphrase.encode(
                "utf-8"
            )
        if isinstance(csr_data, str):
            csr_data = csr_data.encode("utf-8")

        issuer_private_key = load_private_key(
            issuer_private_key_path.read_bytes(), issuer_private_key_passphrase
        )
        issuer_cert = x509.load_pem_x509_certificate(
            issuer_certificate_path.read_bytes()
        )

        cls._verify_private_key_matches_cert(issuer_private_key, issuer_cert)

        csr = x509.load_pem_x509_csr(csr_data)

        # Create a new certificate
        now = datetime.now(timezone.utc)
        cert = (
            x509.CertificateBuilder()
            .subject_name(csr.subject)
            .public_key(csr.public_key())
            .not_valid_before(
                now
                + timedelta(
                    seconds=min(relative_valid_time,
                                relative_expiration_time - 1)
                )
            )
            .not_valid_after(now + timedelta(seconds=relative_expiration_time))
            .issuer_name(issuer_cert.subject)
            .serial_number(serial)
            .sign(issuer_private_key, hashes.SHA256())
        )
        signed_certificate = cert.public_bytes(serialization.Encoding.PEM)

        return signed_certificate.decode(encoding="utf-8")


class OCPPConfigReader:

    def __init__(self, config):
        self._config_json = config

    def get_variable(self, section: str, variable: str):
        identifier = OCPP2XConfigVariableIdentifier(section, variable)

        return GenericOCPP2XConfigAdjustment._get_value_from_v2_config(
            self._config_json, identifier
        )
