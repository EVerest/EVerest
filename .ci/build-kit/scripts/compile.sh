#!/bin/sh

mkdir -p "$EXT_MOUNT/build"
if [ ! -f "$EXT_MOUNT/build/venv/pyvenv.cfg" ]; then
    python3 -m venv "$EXT_MOUNT/build/venv"
fi
if ! "$EXT_MOUNT/build/venv/bin/python" -m pip show grpcio >/dev/null 2>&1 || \
    ! "$EXT_MOUNT/build/venv/bin/python" -c \
        'import importlib.metadata, sys; sys.exit(importlib.metadata.version("grpcio-tools") != "1.70.0")' \
        >/dev/null 2>&1; then
    "$EXT_MOUNT/build/venv/bin/python" -m pip install setuptools grpcio grpcio-tools==1.70.0
    retVal=$?
    if [ $retVal -ne 0 ]; then
        echo "Installing Python gRPC generator dependencies failed with return code $retVal"
        exit $retVal
    fi
fi

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
    -DEVEREST_BUILD_MODULE_EEBUS=ON
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Configuring failed with return code $retVal"
    exit $retVal
fi

ninja -C "$EXT_MOUNT/build"
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Compiling failed with return code $retVal"
    exit $retVal
fi
