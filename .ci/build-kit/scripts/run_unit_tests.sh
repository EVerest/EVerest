#!/bin/sh

# Run unit tests in parallel and stop a test that hangs after 10 minutes.
(cd "$EXT_MOUNT/build" && ctest -j "$(nproc)" --timeout 600 --output-on-failure)
retVal=$?

# Copy the LastTest.log file to the mounted directory in any case
cp "$EXT_MOUNT/build/Testing/Temporary/LastTest.log" "$EXT_MOUNT/ctest-report"

if [ $retVal -ne 0 ]; then
    echo "Unit tests failed with return code $retVal"
    exit $retVal
fi

set -e
