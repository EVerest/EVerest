#!/bin/sh

ninja -C "$EXT_MOUNT/build" \
    everestpy_install_wheel \
    everest-testing_install_wheel \
    everest-management-api-cli_install_wheel \
    iso15118_install_wheel
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Wheel Installation failed with return code $retVal"
    exit $retVal
fi

# Build the generated EVerest API client wheels when they were configured
# (EVEREST_BUILD_API_CLIENTS=ON). This validates that generation from the
# AsyncAPI specs still works and the wheels build - catching interface/spec
# changes that break the clients.
if ninja -C "$EXT_MOUNT/build" -t targets all 2>/dev/null | grep -q '^everest_api_clients:'; then
    ninja -C "$EXT_MOUNT/build" everest_api_clients
    retVal=$?
    if [ $retVal -ne 0 ]; then
        echo "EVerest API client generation/build failed with return code $retVal"
        exit $retVal
    fi

    # Install the generated client wheels + shell deps, then smoke-test that
    # the CLI starts with every client loaded. Proves the generated code and
    # the shell contract still work end to end.
    python3 -m pip install --break-system-packages \
        $EXT_MOUNT/wheels/package_*_api-*.whl \
        $EXT_MOUNT/wheels/spyclient_api-*.whl \
        'cmd2>=2.4,<3' 'paho-mqtt>=2.0'
    retVal=$?
    if [ $retVal -ne 0 ]; then
        echo "Installing generated API client wheels failed with return code $retVal"
        exit $retVal
    fi

    python3 "$EXT_MOUNT/source/applications/utils/everest-api-client/scripts/smoke_test_clients.py"
    retVal=$?
    if [ $retVal -ne 0 ]; then
        echo "EVerest API client smoke test failed with return code $retVal"
        exit $retVal
    fi
fi
