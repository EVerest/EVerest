#!/bin/sh
# Guards how CMake writes files that later stages read.
#
# Usage: cmake-file-generation.sh <source-root> [<build-root>]
#
# Check 1 is static, so its verdict does not depend on what /bin/sh is on the
# machine running it. That matters: with bash as /bin/sh a shelled-out
# `echo -e` looks correct, and the breakage only shows up where /bin/sh is
# dash, which is the platform EVerest ships on.
#
# Check 2 runs only when the Rust link-dependency file has been generated.
set -eu

source_root="${1:-.}"
build_root="${2:-}"
status=0

hits=$(grep -rnE '^[[:space:]]*(COMMAND[[:space:]]+)?echo([[:space:]]|$)' \
    --include='*.cmake' --include='CMakeLists.txt' "$source_root/cmake" || true)
if [ -n "$hits" ]; then
    echo "error: CMake command shells out to echo; use file(GENERATE) instead:" >&2
    echo "$hits" >&2
    status=1
fi

link_deps="$build_root/everestrs-link-dependencies.txt"
if [ -n "$build_root" ] && [ -f "$link_deps" ]; then
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if [ ! -e "$line" ]; then
            echo "error: $link_deps names a file that does not exist: $line" >&2
            status=1
        fi
    done < "$link_deps"
fi

[ "$status" -eq 0 ] && echo "ok: CMake generates its files without a shell"
exit "$status"
