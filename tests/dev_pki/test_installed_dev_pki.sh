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

list="$(dirname "$0")/../../cmake/assets/iso15118-pki/expected-files.txt"
mapfile -t expected < <(grep '^[^#]' "$list")
if [ ${#expected[@]} -eq 0 ]; then
    echo "FAIL: no files listed in $list"
    exit 1
fi

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
