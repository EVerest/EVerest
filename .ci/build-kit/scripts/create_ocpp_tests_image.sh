#!/bin/sh

rsync -a "$EXT_MOUNT/source/tests" ./
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Failed to copy tests"
    exit $retVal
fi

# test_exi_generator.py reads the wire capture shared with libiso15118
rsync -a --relative "$EXT_MOUNT/source/./lib/everest/iso15118/test/data" ./
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Failed to copy lib/everest/iso15118/test/data"
    exit $retVal
fi

python3 -m pip install --break-system-packages \
    "$EXT_MOUNT"/wheels/everestpy-*.whl \
    "$EXT_MOUNT"/wheels/everest_testing-*.whl \
    pytest-html \
    pytest-rerunfailures
retVal=$?

if [ $retVal -ne 0 ]; then
    echo "Failed to pip-install"
    exit $retVal
fi

$(cd ./tests/ocpp_tests/test_sets/everest-aux/ && ./install_certs.sh "$EXT_MOUNT/dist" && ./install_configs.sh "$EXT_MOUNT/dist")
