#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest

# Exit codes 1 and 2 are findings of the checker, so setup failures report 3 ("did not run").

REPORT_NAME=${REPORT_NAME:-"ev-cli-mod-update-report"}
VENV_DIR=/tmp/ev-cli-venv

python3 -m venv "$VENV_DIR" \
    && "$VENV_DIR/bin/pip" install --quiet "$EXT_MOUNT/source/applications/utils/ev-dev-tools"
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Installing ev-cli failed with return code $retVal"
    exit 3
fi

"$VENV_DIR/bin/python" "$EXT_MOUNT/source/applications/utils/scripts/check_mod_update.py" \
    --repo "$EXT_MOUNT/source" \
    --ev-cli "$VENV_DIR/bin/ev-cli" \
    --markdown "$EXT_MOUNT/$REPORT_NAME.md" \
    --json "$EXT_MOUNT/$REPORT_NAME.json"
