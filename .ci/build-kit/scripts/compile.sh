#!/bin/sh

# Reset the ccache statistics so the summary after the build shows this build's hit rate.
if command -v ccache > /dev/null; then
    ccache --zero-stats
fi

# Share the crate registry with the ccache directory the workflow already
# caches, so a cold build does not put every pull request at the mercy of
# crates.io being reachable.
export CARGO_HOME="$EXT_MOUNT/cache/cargo"

cmake \
    -B "$EXT_MOUNT/build" \
    -S "$EXT_MOUNT/source" \
    -G Ninja \
    -DEVC_ENABLE_CCACHE=1 \
    -DISO15118_2_GENERATE_AND_INSTALL_CERTIFICATES=OFF \
    -DCMAKE_INSTALL_PREFIX="$EXT_MOUNT/dist" \
    -DWHEEL_INSTALL_PREFIX="$EXT_MOUNT/wheels" \
    -DBUILD_TESTING=ON \
    -DEVEREST_ENABLE_COMPILE_WARNINGS=ON \
    -DENABLE_GRPC_GENERATOR=ON \
    -DGRPC_EDM=OFF \
    -DGRPC_GENERATOR_EDM=OFF \
    -DEVEREST_BUILD_MODULE_EEBUS=ON \
    -DEVEREST_ENABLE_RS_SUPPORT=ON
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Configuring failed with return code $retVal"
    exit $retVal
fi

ninja -C "$EXT_MOUNT/build"
retVal=$?

# The statistics are also written to the workspace, so CI can show them in the job summary.
if command -v ccache > /dev/null; then
    ccache --show-stats | tee "$EXT_MOUNT/ccache-stats.txt"
fi

if [ $retVal -ne 0 ]; then
    echo "Compiling failed with return code $retVal"
    exit $retVal
fi
