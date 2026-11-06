#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
set -eu

if ! grep -qxF "module data from the overlay" share/everest/modules/RsIgnore/module-data.txt; then
    echo "share/everest/modules/RsIgnore/module-data.txt lacks the overlay content"
    exit 1
fi

log=$(bin/manager_impl --prefix . --config etc/everest/config.yaml --check 2>&1)
echo "$log"

expect_log() {
    if ! printf '%s\n' "$log" | grep -qF "$1"; then
        echo "manager log lacks: $1"
        exit 1
    fi
}

# The Format of the overlay's default_logging.cfg, before the colored severity.
expect_log "overlay-logging ["
expect_log "Loading user-config file at:"
