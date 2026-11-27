#!/bin/sh

cmake \
    -B "$EXT_MOUNT/build-non-edm" \
    -S "$EXT_MOUNT/source" \
    -G Ninja \
    -DDISABLE_EDM=ON \
    -DCMAKE_PREFIX_PATH="$EXT_MOUNT/sysroot" \
    -DCMAKE_INSTALL_PREFIX="$EXT_MOUNT/dist-non-edm" \
    -Deverest-core_USE_PYTHON_VENV=OFF \
    -DEV_CLI="$EXT_MOUNT/sysroot/venv/bin/ev-cli" \
    -DEVEREST_EXCLUDE_DEPENDENCIES="Josev;libnfc-nci" \
    -DEVEREST_BUILD_MODULE_EEBUS=OFF \
    -DISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES=OFF \
    -DEVEREST_ENABLE_COMPILE_WARNINGS=ON
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Configuring failed with return code $retVal"
    exit $retVal
fi

ninja -C "$EXT_MOUNT/build-non-edm" install
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Compiling failed with return code $retVal"
    exit $retVal
fi
