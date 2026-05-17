#!/bin/sh

<<<<<<< HEAD
ninja -C "$EXT_MOUNT/build" install
=======
ninja -C "$EXT_MOUNT/build" install/strip
>>>>>>> blank
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Installation failed with return code $retVal"
    exit $retVal
fi
