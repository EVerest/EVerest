#!/usr/bin/env bash

usage() {
    echo "Usage: $0 <EVerest-installation-directory>"
    exit 1
}

if [ $# -ne 1 ] ; then
    usage
else
    EVEREST_CERTS_PATH="$1/etc/everest/certs"
    rm -rf "$EVEREST_CERTS_PATH"
    mkdir "$EVEREST_CERTS_PATH"

    cp -r certs/ca "$EVEREST_CERTS_PATH"
    cp -r certs/client "$EVEREST_CERTS_PATH"

    # Vehicle client chain is generated rather than committed (see cert-gen/).
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    "$SCRIPT_DIR/cert-gen/generate_vehicle_certs.sh" "$EVEREST_CERTS_PATH"
fi
