# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

import base64

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.asymmetric.utils import encode_dss_signature


# Independent wire capture also used by lib/everest/iso15118/test/iso15118/ev/d2/
# certificate_installation_interop.cpp. Kept here because CI copies only tests/.
RESPONSE_B64 = (
    "gJgCKEU2LX/ropJKiVodHRwOi8vd3d3LnczLm9yZy9UUi9jYW5vbmljYWwtZXhpL0NWh0dHA6Ly93d3cudzMub3JnLzIwMDE"
    "vMDQveG1sZHNpZy1tb3JlI2VjZHNhLXNoYTI1NkQMRtLIYgStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJek"
    "KWh0dHA6Ly93d3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIKDkFx+5xeFduxMWndL6x63rLqdXVtEXkfm2WP1Tus"
    "fzBAxG0shkBK0Ojo4HReXu7u7lzuZlze5M5eqKRexsLc3tzSxsLYWsrw0l6QpaHR0cDovL3d3dy53My5vcmcvMjAwMS8wNC9"
    "4bWxlbmMjc2hhMjU2QgLb8dUepS6KaKITsoShJ4TD4LB5erl4BYZ/9HqJII6M0EDEbSyGYErQ6OjgdF5e7u7uXO5mXN7kzl6"
    "opF7Gwtze3NLGwthayvDSXpClodHRwOi8vd3d3LnczLm9yZy8yMDAxLzA0L3htbGVuYyNzaGEyNTZCC818h/n046JxEku478"
    "Hy+bQdIzvbjhoTZD3XRcXOc1xwQMRtLIaAStDo6OB0Xl7u7u5c7mZc3uTOXqikXsbC3N7c0sbC2FrK8NJekKWh0dHA6Ly93d"
    "3cudzMub3JnLzIwMDEvMDQveG1sZW5jI3NoYTI1NkIF/HhNyPO33/Df+huv9J2SV8tWMLaMuQAseoDKUE962WEoCyAiMuDZh"
    "a09Ld7fOvu7VyZIwdnfDJg8gHMlzvR8Jwgexq6EDVoJvBYCU1O+euOg6lvvFWr9r0pTHQetO7jagcgYALlAzCCAeEwggGGoA"
    "MCAQICAjBHMAoGCCqGSM49BAMCMEkxEzARBgNVBAMMClByb3ZTdWJDQTIxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAk"
    "RFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMB4XDTIzMDkyNjA3MzgzNFoXDTQ4MDUxNzA3MzgzNFowRzERMA8GA1UEAwwIQ1BTIE"
    "xlYWYxEDAOBgNVBAoMB0VWZXJlc3QxCzAJBgNVBAYTAkRFMRMwEQYKCZImiZPyLGQBGRYDQ1BTMFkwEwYHKoZIzj0CAQYIKo"
    "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"
)


def test_certificate_installation_capture(exi_generator):
    captured = base64.b64decode(RESPONSE_B64)
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
