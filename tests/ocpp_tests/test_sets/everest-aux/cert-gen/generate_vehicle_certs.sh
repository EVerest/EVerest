#!/usr/bin/env bash
# Generate the vehicle client certificate chain into an existing certs tree.
# The chain is signed by the committed everest-aux V2G root so that a SECC
# verifying the vehicle (TLS client) certificate against that root, as the
# ISO 15118-20 mutual-TLS handshake does, accepts the chain.
set -euo pipefail

usage() {
    echo "Usage: $0 <certs-directory>"
    exit 1
}

[ $# -eq 1 ] || usage

CERTS_DIR="$1"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

EC_CURVE=prime256v1
SHA=-sha256
CIPHER=-aes-128-cbc
PASSWORD=123456

CA_VEHICLE="$CERTS_DIR/ca/vehicle"
CLIENT_VEHICLE="$CERTS_DIR/client/vehicle"
mkdir -p "$CA_VEHICLE" "$CLIENT_VEHICLE"

# Committed V2G root (public cert plus its encrypted key) that a SECC loads as
# the trust anchor for verifying vehicle certificates.
V2G_ROOT_CERT="$CERTS_DIR/ca/v2g/V2G_ROOT_CA.pem"
V2G_ROOT_KEY="$CERTS_DIR/client/v2g/V2G_ROOT_CA.key"
if [ ! -f "$V2G_ROOT_CERT" ] || [ ! -f "$V2G_ROOT_KEY" ]; then
    echo "V2G root cert/key not found under $CERTS_DIR (run install_certs.sh first)" >&2
    exit 1
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# VehicleSubCA1 (signed by the committed V2G root)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleSubCA1Cert.cnf" -out "$TMP/VEHICLE_SUB_CA1.csr"
openssl x509 -req -in "$TMP/VEHICLE_SUB_CA1.csr" -extfile "$SCRIPT_DIR/vehicleSubCA1Cert.cnf" -extensions ext \
    -CA "$V2G_ROOT_CERT" -CAkey "$V2G_ROOT_KEY" -passin "pass:$PASSWORD" -set_serial 12360 \
    -out "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -days 1460

# VehicleSubCA2 (signed by VehicleSubCA1)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleSubCA2Cert.cnf" -out "$TMP/VEHICLE_SUB_CA2.csr"
openssl x509 -req -in "$TMP/VEHICLE_SUB_CA2.csr" -extfile "$SCRIPT_DIR/vehicleSubCA2Cert.cnf" -extensions ext \
    -CA "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -CAkey "$CLIENT_VEHICLE/VEHICLE_SUB_CA1.key" -passin "pass:$PASSWORD" -set_serial 12361 \
    -out "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -days 3650

# Vehicle leaf (signed by VehicleSubCA2)
openssl ecparam -genkey -name "$EC_CURVE" | openssl ec $CIPHER -passout "pass:$PASSWORD" -out "$CLIENT_VEHICLE/VEHICLE_LEAF.key"
openssl req -new -key "$CLIENT_VEHICLE/VEHICLE_LEAF.key" -passin "pass:$PASSWORD" \
    -config "$SCRIPT_DIR/vehicleLeafCert.cnf" -out "$TMP/VEHICLE_LEAF.csr"
openssl x509 -req -in "$TMP/VEHICLE_LEAF.csr" -extfile "$SCRIPT_DIR/vehicleLeafCert.cnf" -extensions ext \
    -CA "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -CAkey "$CLIENT_VEHICLE/VEHICLE_SUB_CA2.key" -passin "pass:$PASSWORD" -set_serial 12362 \
    -out "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" -days 1460

# Full chain, DER encodings and the key password file
cat "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" \
    > "$CLIENT_VEHICLE/VEHICLE_CERT_CHAIN.pem"
openssl x509 -in "$CA_VEHICLE/VEHICLE_SUB_CA1.pem" -outform DER -out "$CA_VEHICLE/VEHICLE_SUB_CA1.der"
openssl x509 -in "$CA_VEHICLE/VEHICLE_SUB_CA2.pem" -outform DER -out "$CA_VEHICLE/VEHICLE_SUB_CA2.der"
openssl x509 -in "$CLIENT_VEHICLE/VEHICLE_LEAF.pem" -outform DER -out "$CLIENT_VEHICLE/VEHICLE_LEAF.der"
echo "$PASSWORD" > "$CLIENT_VEHICLE/VEHICLE_LEAF_PASSWORD.txt"
