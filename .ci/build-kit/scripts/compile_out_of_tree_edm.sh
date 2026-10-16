#!/bin/sh

# EDM uses the project's parent directory as workspace, so the sibling everest-core is built in
# place, and both projects find the everest-cmake that everest-core pins in ../everest-cmake.
WS="$EXT_MOUNT/ws"
BUILD="$EXT_MOUNT/build-oot-edm"
DEPS="$EXT_MOUNT/oot-deps"

EVC_REF=$(sed -n '/everest-cmake.git/{n;s/.*GIT_TAG *\([0-9a-f]\{40\}\).*/\1/p}' "$WS/everest-core/CMakeLists.txt")
if [ -z "$EVC_REF" ]; then
    echo "Could not read the everest-cmake commit from everest-core's CMakeLists.txt"
    exit 1
fi
git init --quiet "$WS/everest-cmake" \
    && git -C "$WS/everest-cmake" fetch --quiet --depth 1 https://github.com/EVerest/everest-cmake.git "$EVC_REF" \
    && git -C "$WS/everest-cmake" checkout --quiet FETCH_HEAD
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Fetching everest-cmake $EVC_REF failed with return code $retVal"
    exit $retVal
fi

# everest-chargebyte links libgpiodcxx and libsocketcan by name, so they must be on the default paths.
export CPATH="$DEPS/include" LIBRARY_PATH="$DEPS/lib" PKG_CONFIG_PATH="$DEPS/lib/pkgconfig"

# CbSystem does not implement configure_network of the system interface yet.
cmake \
    -B "$BUILD" \
    -S "$WS/everest-chargebyte" \
    -G Ninja \
    -DCMAKE_INSTALL_PREFIX="$EXT_MOUNT/dist-oot-edm" \
    -DWHEEL_INSTALL_PREFIX="$EXT_MOUNT/wheels-oot-edm" \
    -DISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES=OFF \
    -DGRPC_EDM=OFF \
    -DGRPC_GENERATOR_EDM=OFF \
    -DEVEREST_BUILD_MODULE_EEBUS=OFF \
    -DEVEREST_EXCLUDE_MODULES=CbSystem
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Configuring failed with return code $retVal"
    exit $retVal
fi

if ! grep -q "CPM_everest-core_SOURCE \"$WS/everest-core\"" "$BUILD/dependencies.cmake"; then
    echo "EDM did not use the everest-core checkout in $WS:"
    grep 'CPM_everest-core_SOURCE' "$BUILD/dependencies.cmake"
    exit 1
fi

ninja -C "$BUILD" install
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Compiling failed with return code $retVal"
    exit $retVal
fi
