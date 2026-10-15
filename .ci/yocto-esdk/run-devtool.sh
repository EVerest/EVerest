#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Copyright Pionix GmbH and Contributors to EVerest
#
# Builds everest-core from the source tree given as argument with devtool in the
# Yocto eSDK image and shows the CMake configure and compile output in collapsible
# groups of the GitHub Actions log.

src=${1:?usage: run-devtool.sh <everest source tree>}

# The environment setup does not work with errexit: its buildtools part exits
# silently with 127 when the host has no pkg-config, like the image.
. /opt/esdk/environment-setup-* > /dev/null
set -euo pipefail

# Debug info makes up most of the ~20 GB build directory of everest-core.
echo 'DEBUG_FLAGS:pn-everest-core = ""' >> /opt/esdk/conf/local.conf

devtool modify -n everest-core "$src"

# The group markers are written by bitbake into the task logs, so they enclose
# exactly the output of the task. A failing task skips its postfuncs.
cat > /opt/esdk/workspace/appends/everest-core_%.bbappend << 'EOF'
ci_group_begin() {
    echo "::group::${PN} do_${BB_CURRENTTASK}"
}
ci_group_end() {
    echo "::endgroup::"
}
do_configure[prefuncs] += "ci_group_begin"
do_configure[postfuncs] += "ci_group_end"
do_compile[prefuncs] += "ci_group_begin"
do_compile[postfuncs] += "ci_group_end"
EOF

# bitbake writes the output of a task only to its log, follow the CMake ones.
# externalsrc links oe-logs in the source tree to the log directory of the recipe
# when do_configure starts, tail -F waits for it.
logs=$src/oe-logs
tail -q -n +1 -F "$logs/log.do_configure" "$logs/log.do_compile" 2> /dev/null &
tail_pid=$!

stop_following() {
    local status=$?
    sleep 1
    kill "$tail_pid" 2> /dev/null || true
    wait "$tail_pid" 2> /dev/null || true
    if [ "$status" -ne 0 ]; then
        echo "::endgroup::"
        echo "::error title=Yocto eSDK build::devtool build everest-core failed"
        # The error report of bitbake races with the followed logs and can end up
        # in a collapsed group, so repeat the end of the failed task log.
        local failed_log
        failed_log=$(ls -tL "$logs"/log.do_* 2> /dev/null | head -n 1) || true
        if [ -n "$failed_log" ]; then
            echo "Last lines of $(basename "$failed_log"):"
            # The log contains group markers, print it without running them.
            local token
            token=$(cat /proc/sys/kernel/random/uuid)
            echo "::stop-commands::$token"
            tail -n 100 "$failed_log"
            echo "::$token::"
        fi
    fi
}
trap stop_following EXIT

devtool build everest-core
