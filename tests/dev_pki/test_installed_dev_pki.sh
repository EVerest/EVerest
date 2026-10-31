#!/usr/bin/env bash
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Checks that an EVerest install prefix holds the ISO 15118 development PKI that the TLS
# and Plug&Charge configurations load through the EvseSecurity and Ev15118 defaults.
# A manual check, run after cmake --install; no test suite or CI job calls it.
#
# Usage: test_installed_dev_pki.sh <install prefix>

set -u

if [ $# -ne 1 ]; then
    echo "usage: $0 <install prefix>" >&2
    exit 2
fi

certs="$1/etc/everest/certs"

expected=(
    # EvseSecurity: V2G and MO roots, SECC leaf in client/cso
    ca/v2g/V2G_ROOT_CA.pem
    ca/mo/MO_ROOT_CA.pem
    client/cso/CPO_CERT_CHAIN.pem
    client/cso/SECC_LEAF.pem
    client/cso/SECC_LEAF.key
    client/cso/SECC_LEAF_PASSWORD.txt
    # Ev15118: vehicle TLS client, PnC contract and OEM provisioning credentials
    client/vehicle/VEHICLE_CERT_CHAIN.pem
    client/vehicle/VEHICLE_LEAF.key
    client/vehicle/VEHICLE_LEAF_PASSWORD.txt
    client/mo/MO_LEAF.pem
    client/mo/MO_LEAF.key
    client/mo/MO_LEAF_PASSWORD.txt
    ca/mo/INTERMEDIATE_MO_CA_CERTS.pem
    client/oem/OEM_LEAF.pem
    client/oem/OEM_LEAF.key
    client/oem/OEM_LEAF_PASSWORD.txt
)

missing=0
for file in "${expected[@]}"; do
    if [ ! -s "$certs/$file" ]; then
        echo "missing or empty: $certs/$file"
        missing=$((missing + 1))
    fi
done

if [ "$missing" -ne 0 ]; then
    echo "FAIL: $missing of ${#expected[@]} development PKI files missing"
    exit 1
fi
echo "PASS: all ${#expected[@]} development PKI files installed under $certs"
