#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
"""Mock of the OPCP / Hubject Plug&Charge services used by opcp-enroll and OpcpCertificateManager.

Implements, on one HTTPS port:
  POST /oauth/token                                   OAuth2 client credentials -> JWT-shaped bearer token
  PUT  /v1/vra/cpo/endEntities                        end entity registration (bearer)
  POST /.well-known[/est]/{ca}/simpleenroll[/{iso}]    CSR -> leaf, PKCS#7 certs-only (bearer, CN registered)
  POST /.well-known[/est]/{ca}/simplereenroll[/{iso}]  CSR -> leaf (TLS client certificate chained to the V2G root)
  GET  [/.well-known/est]/{ca}/cacerts/{iso}/{alg}/    sub-CA chain, PKCS#7 (bearer or client certificate)
  GET  /v1/root/rootCerts[?rootType=]                 root pool JSON (bearer, or client certificate if allowed)

The CPO PKI (root.pem/key, sub1.pem/key, sub2.pem/key) is taken from --pki-dir, as produced by
lib/everest/opcp/tests/generate_test_pki.sh. A server certificate for --host and a second root labelled MO
are generated at start-up; the server certificate's CA is written to <out-dir>/server_ca.pem for clients.
"""
import argparse
import base64
import datetime
import json
import os
import re
import signal
import ssl
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives.serialization import pkcs7
from cryptography.x509.oid import NameOID


def load_pem_cert(path):
    with open(path, "rb") as f:
        return x509.load_pem_x509_certificate(f.read())


def load_pem_key(path):
    with open(path, "rb") as f:
        return serialization.load_pem_private_key(f.read(), password=None)


def b64_der(cert):
    return base64.b64encode(cert.public_bytes(serialization.Encoding.DER)).decode()


def pkcs7_b64(certs):
    der = pkcs7.serialize_certificates(certs, serialization.Encoding.DER)
    return base64.b64encode(der).decode()


def make_self_signed(common_name, san_hosts=None, is_ca=False, days=3650):
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "DE"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "EVerest Test"),
        x509.NameAttribute(NameOID.COMMON_NAME, common_name),
    ])
    now = datetime.datetime.now(datetime.timezone.utc)
    builder = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
               .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
               .not_valid_after(now + datetime.timedelta(days=days))
               .add_extension(x509.BasicConstraints(ca=is_ca, path_length=None), critical=True))
    if san_hosts:
        sans = []
        for host in san_hosts:
            try:
                import ipaddress
                sans.append(x509.IPAddress(ipaddress.ip_address(host)))
            except ValueError:
                sans.append(x509.DNSName(host))
        builder = builder.add_extension(x509.SubjectAlternativeName(sans), critical=False)
    return key, builder.sign(key, hashes.SHA256())


class State:
    def __init__(self, args):
        self.args = args
        self.root = load_pem_cert(os.path.join(args.pki_dir, "root.pem"))
        self.sub1 = load_pem_cert(os.path.join(args.pki_dir, "sub1.pem"))
        self.sub2 = load_pem_cert(os.path.join(args.pki_dir, "sub2.pem"))
        self.sub2_key = load_pem_key(os.path.join(args.pki_dir, "sub2.key"))
        _, self.mo_root = make_self_signed("MO Root CA Test", is_ca=True)
        self.registered = set()
        self.tokens = set()
        self.lock = threading.Lock()
        self.requests = []  # (method, path, auth kind, status)

    def issue_token(self):
        header = base64.urlsafe_b64encode(b'{"alg":"none","typ":"JWT"}').rstrip(b"=").decode()
        payload = {
            "https://mock.opcp.test/role": ["CPO"],
            "https://mock.opcp.test/client_name": ["mock-client"],
            "permissions": ["rcpservice", "pkigateway"],
            "scope": "rcpservice pkigateway",
            "iss": "https://mock.opcp.test/",
            "exp": int(datetime.datetime.now().timestamp()) + 86400,
        }
        body = base64.urlsafe_b64encode(json.dumps(payload).encode()).rstrip(b"=").decode()
        token = f"{header}.{body}.mocksig"
        with self.lock:
            self.tokens.add(token)
        return token, payload

    def sign_csr(self, csr, days):
        key = csr.public_key()
        digest = hashes.SHA512() if isinstance(key.curve, ec.SECP521R1) else hashes.SHA256()
        now = datetime.datetime.now(datetime.timezone.utc)
        builder = (x509.CertificateBuilder().subject_name(csr.subject).issuer_name(self.sub2.subject)
                   .public_key(key).serial_number(x509.random_serial_number())
                   .not_valid_before(now - datetime.timedelta(minutes=5))
                   .not_valid_after(now + datetime.timedelta(days=days))
                   .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
                   .add_extension(x509.KeyUsage(digital_signature=True, key_agreement=True, content_commitment=False,
                                                key_encipherment=False, data_encipherment=False, key_cert_sign=False,
                                                crl_sign=False, encipher_only=False, decipher_only=False),
                                  critical=True)
                   .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(self.sub2.public_key()),
                                  critical=False))
        return builder.sign(self.sub2_key, digest)


ENROLL_RE = re.compile(r"^/\.well-known/(?:est/)?(?P<ca>[\w-]+)/(?P<op>simpleenroll|simplereenroll)(?:/(?P<iso>ISO15118-2|ISO15118-20))?/?$")
CACERTS_RE = re.compile(r"^(?:/\.well-known/est)?/(?P<ca>[\w-]+)/cacerts(?:/(?P<iso>ISO15118-2|ISO15118-20))?(?:/(?P<alg>\w+))?/?$")


class Handler(BaseHTTPRequestHandler):
    server_version = "MockOPCP/1.0"
    state: State = None

    def log_message(self, fmt, *args):  # quieter default log
        sys.stderr.write("[mock-opcp] %s\n" % (fmt % args))

    # --- helpers -------------------------------------------------------------------------------------
    def _body(self):
        length = int(self.headers.get("Content-Length", "0"))
        return self.rfile.read(length) if length > 0 else b""

    def _send(self, status, body=b"", content_type="text/plain"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self._record(status)

    def _record(self, status):
        with self.state.lock:
            self.state.requests.append({"method": self.command, "path": self.path, "auth": self._auth_kind(),
                                        "status": status})

    def _peer_cert(self):
        try:
            return self.connection.getpeercert(binary_form=True)
        except (AttributeError, ValueError):
            return None

    def _auth_kind(self):
        if self.headers.get("Authorization", "").startswith("Bearer "):
            return "bearer"
        if self._peer_cert():
            return "client-cert"
        return "none"

    def _bearer_ok(self):
        auth = self.headers.get("Authorization", "")
        return auth.startswith("Bearer ") and auth[7:] in self.state.tokens

    def _client_cert_ok(self):
        # ssl verified the presented chain against the V2G root (CERT_OPTIONAL + load_verify_locations)
        der = self._peer_cert()
        if not der:
            return False
        cert = x509.load_der_x509_certificate(der)
        return cert.issuer == self.state.sub2.subject

    def _client_cert_cn(self):
        der = self._peer_cert()
        if not der:
            return None
        cert = x509.load_der_x509_certificate(der)
        cns = cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        return cns[0].value if cns else None

    def _json_error(self, status, *messages):
        self._send(status, json.dumps({"errorMessages": list(messages), "traceId": "00000000-mock"}),
                   "application/json")

    # --- routes --------------------------------------------------------------------------------------
    def do_POST(self):
        if self.path == "/oauth/token":
            return self._token()
        match = ENROLL_RE.match(self.path)
        if match:
            return self._enroll(match)
        self._json_error(404, "unknown path " + self.path)

    def do_PUT(self):
        if self.path == "/v1/vra/cpo/endEntities":
            return self._register()
        self._json_error(404, "unknown path " + self.path)

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path == "/v1/root/rootCerts":
            return self._roots()
        match = CACERTS_RE.match(path)
        if match:
            return self._cacerts(match)
        self._json_error(404, "unknown path " + self.path)

    def _token(self):
        try:
            payload = json.loads(self._body() or b"{}")
        except json.JSONDecodeError:
            return self._send(400, json.dumps({"error": "invalid_request", "error_description": "not json"}),
                              "application/json")
        if payload.get("grant_type") != "client_credentials":
            return self._send(400, json.dumps({"error": "unsupported_grant_type",
                                               "error_description": "grant_type must be client_credentials"}),
                              "application/json")
        if payload.get("client_id") != self.state.args.client_id or \
                payload.get("client_secret") != self.state.args.client_secret:
            return self._send(401, json.dumps({"error": "access_denied", "error_description": "Unauthorized"}),
                              "application/json")
        if self.state.args.audience and payload.get("audience") != self.state.args.audience:
            return self._send(401, json.dumps({"error": "access_denied",
                                               "error_description": "wrong audience " + str(payload.get("audience"))}),
                              "application/json")
        token, claims = self.state.issue_token()
        self._send(200, json.dumps({"access_token": token, "scope": claims["scope"], "expires_in": 86400,
                                    "token_type": "Bearer"}), "application/json")

    def _register(self):
        if not self._bearer_ok():
            return self._json_error(401, "Unauthorized")
        try:
            payload = json.loads(self._body() or b"{}")
        except json.JSONDecodeError:
            return self._json_error(400, "body is not JSON")
        cn = payload.get("commonName")
        for field in ("commonName", "manufacture", "deviceName", "evseID", "evseISOversion"):
            if field not in payload:
                return self._json_error(400, "missing field " + field)
        if self.state.args.operator_id and not cn.upper().replace("*", "-").startswith(
                ("DE-" + self.state.args.operator_id).upper()) and \
                self.state.args.operator_id.upper() not in cn.upper()[2:6]:
            return self._json_error(403, f"EVSE Operator ID of '{cn}' is not assigned to this account")
        with self.state.lock:
            self.state.registered.add(cn)
        self._send(200, json.dumps({"commonName": cn, "status": "registered"}), "application/json")

    def _enroll(self, match):
        op = match.group("op")
        if op == "simpleenroll":
            if not self._bearer_ok():
                return self._json_error(401, "Unauthorized")
        else:
            if self.state.args.reject_reenroll:
                return self._json_error(403, "certificate based authentication not enabled")
            if not self._client_cert_ok():
                return self._json_error(401, "TLS client certificate required for simplereenroll")
        raw = self._body()
        try:
            der = base64.b64decode(re.sub(rb"\s", b"", raw), validate=True)
            csr = x509.load_der_x509_csr(der)
        except Exception as e:  # noqa: BLE001
            return self._json_error(400, "body is not a base64 DER PKCS#10: " + str(e))
        cns = csr.subject.get_attributes_for_oid(NameOID.COMMON_NAME)
        cn = cns[0].value if cns else ""
        if op == "simpleenroll" and self.state.args.require_registration and cn not in self.state.registered:
            return self._json_error(403, f"end entity '{cn}' is not registered")
        if op == "simplereenroll" and self.state.args.reenroll_same_cn and cn != self._client_cert_cn():
            return self._json_error(403, f"client certificate CN '{self._client_cert_cn()}' does not match CSR CN '{cn}'")
        # curve vs ISO version plausibility as the real PKI enforces it
        iso = match.group("iso") or "ISO15118-2"
        is_p521 = isinstance(csr.public_key().curve, ec.SECP521R1)
        if self.state.args.enforce_curve and (is_p521 != (iso == "ISO15118-20")):
            return self._json_error(400, f"key curve does not match {iso}")
        days = self.state.args.leaf_days
        if op == "simplereenroll" and self.state.args.reenroll_leaf_days > 0:
            days = self.state.args.reenroll_leaf_days
        leaf = self.state.sign_csr(csr, days)
        certs = [leaf] if not self.state.args.include_chain else [leaf, self.state.sub2, self.state.sub1]
        self._send(200, pkcs7_b64(certs), "application/pkcs7-mime; smime-type=certs-only")

    def _cacerts(self, match):
        if not (self._bearer_ok() or self._client_cert_ok()):
            return self._json_error(401, "Unauthorized")
        certs = [self.state.sub2, self.state.sub1]
        if self.state.args.cacerts_with_root:
            certs.append(self.state.root)
        self._send(200, pkcs7_b64(certs), "application/pkcs7-mime; smime-type=certs-only")

    def _roots(self):
        if self._bearer_ok():
            pass
        elif self._client_cert_ok() and self.state.args.rcp_client_cert == "accept":
            pass
        else:
            return self._json_error(401, "Unauthorized")
        query = self.path.split("?", 1)[1] if "?" in self.path else ""
        wanted = None
        for part in query.split("&"):
            if part.startswith("rootType="):
                wanted = part.split("=", 1)[1].lower()

        def entry(cert, root_type, xsd):
            return {
                "rootCertificateId": "%032x" % cert.serial_number,
                "distinguishedName": cert.subject.rfc4514_string(),
                "caCertificate": b64_der(cert),
                "commonName": cert.subject.get_attributes_for_oid(NameOID.COMMON_NAME)[0].value,
                "validFrom": cert.not_valid_before_utc.strftime("%Y-%m-%dT%H:%M:%SZ") if hasattr(cert, "not_valid_before_utc") else cert.not_valid_before.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "validTo": cert.not_valid_after_utc.strftime("%Y-%m-%dT%H:%M:%SZ") if hasattr(cert, "not_valid_after_utc") else cert.not_valid_after.strftime("%Y-%m-%dT%H:%M:%SZ"),
                "organizationName": "EVerest Test",
                "rootType": root_type,
                "xsdMsgDefNamespace": xsd,
                "signatureAlgorithm": "secp256r1",
            }
        roots = [entry(self.state.root, "V2G", "urn:iso:15118:2:2013:MsgDef"),
                 entry(self.state.mo_root, "MO", "urn:iso:15118:2:2013:MsgDef")]
        if wanted:
            roots = [r for r in roots if r["rootType"].lower() == wanted]
        self._send(200, json.dumps({"RootCertificateCollection": {"rootCertificates": roots}}), "application/json")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--pki-dir", required=True, help="directory produced by generate_test_pki.sh")
    parser.add_argument("--out-dir", default=".", help="where server.pem/server.key/server_ca.pem are written")
    parser.add_argument("--client-id", default="test-client")
    parser.add_argument("--client-secret", default="test-secret")
    parser.add_argument("--audience", default="", help="require this audience in token requests")
    parser.add_argument("--operator-id", default="", help="only accept CNs of this EVSE Operator ID")
    parser.add_argument("--no-registration-check", dest="require_registration", action="store_false")
    parser.add_argument("--include-chain", action="store_true", help="return leaf + sub-CAs on enroll")
    parser.add_argument("--cacerts-with-root", action="store_true")
    parser.add_argument("--leaf-days", type=int, default=365)
    parser.add_argument("--reenroll-leaf-days", type=int, default=0, help="validity of re-enrolled leafs (0: same as --leaf-days)")
    parser.add_argument("--reject-reenroll", action="store_true", help="simulate a PKI without mTLS support")
    parser.add_argument("--reenroll-same-cn", action="store_true", help="reenroll must keep the CN of the client cert")
    parser.add_argument("--no-enforce-curve", dest="enforce_curve", action="store_false")
    parser.add_argument("--rcp-client-cert", choices=["accept", "reject"], default="accept",
                        help="whether the root pool accepts TLS client certificate authentication")
    parser.add_argument("--dump-requests", default="", help="write the request log as JSON to this file on exit")
    args = parser.parse_args()

    state = State(args)
    os.makedirs(args.out_dir, exist_ok=True)
    server_key, server_cert = make_self_signed("mock-opcp", san_hosts=[args.host, "localhost"])
    key_path = os.path.join(args.out_dir, "server.key")
    cert_path = os.path.join(args.out_dir, "server.pem")
    with open(key_path, "wb") as f:
        f.write(server_key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                         serialization.NoEncryption()))
    with open(cert_path, "wb") as f:
        f.write(server_cert.public_bytes(serialization.Encoding.PEM))
    with open(os.path.join(args.out_dir, "server_ca.pem"), "wb") as f:
        f.write(server_cert.public_bytes(serialization.Encoding.PEM))

    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(cert_path, key_path)
    context.verify_mode = ssl.CERT_OPTIONAL
    context.load_verify_locations(os.path.join(args.pki_dir, "root.pem"))

    Handler.state = state
    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    httpd.socket = context.wrap_socket(httpd.socket, server_side=True)
    print(f"mock OPCP listening on https://{args.host}:{httpd.server_address[1]} (ca {cert_path})", flush=True)

    def _terminate(*_):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, _terminate)
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if args.dump_requests:
            with open(args.dump_requests, "w") as f:
                json.dump(state.requests, f, indent=1)


if __name__ == "__main__":
    main()
