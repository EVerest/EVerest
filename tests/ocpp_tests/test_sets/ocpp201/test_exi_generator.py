# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import base64
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature


# Independent wire capture, shared with lib/everest/iso15118/test/iso15118/ev/d2/
# certificate_installation_interop.cpp.
RESPONSE_PATH = Path(__file__).parent.parent / "everest-aux" / "exi" / "certificate_installation_res.base64"


def test_certificate_installation_capture(exi_generator):
    captured = base64.b64decode(RESPONSE_PATH.read_bytes())
    assert len(captured) == 4143
    message = exi_generator.decode(captured, exi_generator.ISO2_NAMESPACE)
    assert exi_generator.encode(message) == captured

    v2g = message["V2G_Message"]
    response = v2g["Body"]["CertificateInstallationRes"]
    expected = v2g["Header"]["Signature"]
    actual = exi_generator.create_signed_info(response)
    assert len(actual["Reference"]) == 4
    assert actual == expected["SignedInfo"]

    signature = base64.b64decode(expected["SignatureValue"]["value"])
    signature_der = encode_dss_signature(
        int.from_bytes(signature[:32], "big"), int.from_bytes(signature[32:], "big")
    )
    leaf = x509.load_der_x509_certificate(base64.b64decode(response["SAProvisioningCertificateChain"]["Certificate"]))
    leaf.public_key().verify(
        signature_der,
        exi_generator.encode({"SignedInfo": actual}, exi_generator.XMLDSIG_NAMESPACE),
        ec.ECDSA(hashes.SHA256()),
    )
