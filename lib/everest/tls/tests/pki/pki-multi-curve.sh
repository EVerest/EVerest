#!/usr/bin/env bash
#
# pki-multi-curve.sh
#
# Generate a three-tier test PKI (root CA -> sub CA -> leaf) for a given
# NIST elliptic curve. Produces both a server chain and a client chain so
# mTLS handshake tests can exercise signature-algorithm pinning across
# curves.
#
# Usage:
#   pki-multi-curve.sh <P-256|P-384|P-521>
#
# Output is written to ./pki-<CURVE>/ in the current working directory.
# The OpenSSL configuration file (openssl-pki.conf) is located next to
# this script in the source tree.

set -e

usage() {
    echo "Usage: $0 <P-256|P-384|P-521>" >&2
}

if [ "$#" -ne 1 ]; then
    usage
    exit 1
fi

CURVE="$1"

case "${CURVE}" in
    P-256|P-384|P-521)
        ;;
    *)
        echo "Error: unsupported curve '${CURVE}'" >&2
        usage
        exit 1
        ;;
esac

# Locate openssl-pki.conf relative to this script, so the script works
# regardless of the caller's cwd (CMake runs it from CMAKE_CURRENT_BINARY_DIR).
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cfg="${SCRIPT_DIR}/openssl-pki.conf"

if [ ! -f "${cfg}" ]; then
    echo "Error: openssl-pki.conf not found next to script at ${cfg}" >&2
    exit 1
fi

out_dir="pki-${CURVE}"
mkdir -p "${out_dir}"

server_root_priv="${out_dir}/server_root_priv.pem"
server_ca_priv="${out_dir}/server_ca_priv.pem"
server_priv="${out_dir}/server_priv.pem"

server_root_cert="${out_dir}/server_root_cert.pem"
server_ca_cert="${out_dir}/server_ca_cert.pem"
server_cert="${out_dir}/server_cert.pem"
server_chain="${out_dir}/server_chain.pem"

client_root_priv="${out_dir}/client_root_priv.pem"
client_ca_priv="${out_dir}/client_ca_priv.pem"
client_priv="${out_dir}/client_priv.pem"

client_root_cert="${out_dir}/client_root_cert.pem"
client_ca_cert="${out_dir}/client_ca_cert.pem"
client_cert="${out_dir}/client_cert.pem"
client_chain="${out_dir}/client_chain.pem"

# Generate keys with the requested curve
for i in "${server_root_priv}" "${server_ca_priv}" "${server_priv}" \
         "${client_root_priv}" "${client_ca_priv}" "${client_priv}"
do
    openssl genpkey -algorithm EC -pkeyopt "ec_paramgen_curve:${CURVE}" -out "$i"
    chmod 644 "$i"
done

export OPENSSL_CONF="${cfg}"

echo "Generate server_root (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_server_root -extensions v3_server_root \
    -key "${server_root_priv}" -out "${server_root_cert}"

echo "Generate server_ca (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_server_ca -extensions v3_server_ca \
    -key "${server_ca_priv}" -CA "${server_root_cert}" \
    -CAkey "${server_root_priv}" -out "${server_ca_cert}"

echo "Generate server (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_server -extensions v3_server \
    -key "${server_priv}" -CA "${server_ca_cert}" \
    -CAkey "${server_ca_priv}" -out "${server_cert}"

cat "${server_cert}" "${server_ca_cert}" > "${server_chain}"

echo "Generate client_root (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_client_root -extensions v3_client_root \
    -key "${client_root_priv}" -out "${client_root_cert}"

echo "Generate client_ca (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_client_ca -extensions v3_client_ca \
    -key "${client_ca_priv}" -CA "${client_root_cert}" \
    -CAkey "${client_root_priv}" -out "${client_ca_cert}"

echo "Generate client (${CURVE})"
openssl req \
    -config "${cfg}" -x509 -section req_client -extensions v3_client \
    -key "${client_priv}" -CA "${client_ca_cert}" \
    -CAkey "${client_ca_priv}" -out "${client_cert}"

cat "${client_cert}" "${client_ca_cert}" > "${client_chain}"
