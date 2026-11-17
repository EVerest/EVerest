#!/bin/sh

python3 "$EXT_MOUNT/source/applications/utils/scripts/setup_sysroot.py" \
    --src "$EXT_MOUNT/source" \
    --prefix "$EXT_MOUNT/sysroot"
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Building the sysroot failed with return code $retVal"
    exit $retVal
fi
