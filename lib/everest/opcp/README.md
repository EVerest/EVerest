# libopcp – Open Plug&Charge Protocol client

`everest::opcp` is a small libcurl/OpenSSL based client for the OPCP services a charge point operator
uses to provision ISO 15118 SECC leaf certificates:

| Component | Endpoint |
|---|---|
| `oauth2.hpp` | `POST /oauth/token` (client credentials → bearer token), JWT payload decoding |
| `vra_client.hpp` | `PUT /v1/vra/cpo/endEntities` (Hubject station registration, not in the OPCP OpenAPI) |
| `rcp_client.hpp` | `GET /v1/root/rootCerts[?rootType=]` (root certificate pool) |
| `est_client.hpp` | RFC 7030 `simpleenroll` (bearer), `simplereenroll` (TLS client certificate), `cacerts` |
| `pkcs.hpp` | PKCS#10 → base64 DER, base64 PKCS#7 → PEM, chain assembly (leaf + sub-CAs, root stripped) |
| `enroller.hpp` | CSR → enroll → cacerts → chain → install against an abstract `SecurityStore` |
| `environment.hpp` | Hubject EU/US test/QA/prod presets and path templates (`{ca}`, `{iso}`, `{iso_opt}`, `{alg}`) |

Consumers: `tools/opcp-enroll` (CLI on top of libevse-security) and the `OpcpCertificateManager` module
(on top of the `evse_security` interface). The library is built only when libcurl is enabled; see the
module documentation in `modules/EVSE/OpcpCertificateManager/docs/index.rst` for the overall flow.

Tests: `tests/` (gtest, generates its PKI with `generate_test_pki.sh`).
