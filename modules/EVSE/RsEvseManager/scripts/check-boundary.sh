#!/bin/bash
# Compile src/main.rs.
#
# test-core.sh strips main.rs, so it cannot see boundary breakage, and
# `ctest -R RsEvseManager` does not close the hole either: the ninja target only
# creates a symlink and reports "no work to do" even after a touch, so it reruns
# a cached binary. This is the only gate that compiles the boundary.
#
# Expect ~98 warnings from out/generated.rs plus one pre-existing lib warning
# ("crate RsEvseManager should have a snake case name"). Anything under src/ is
# a real diagnostic.
#
# The cargo invocation and its two environment variables were extracted from
# `ninja -t commands` in the build directory; regenerate from there if the build
# layout changes.
#
# On a freshly configured tree this needs three things built before it will run
# at all, and none of them come from `cmake` alone. Two of them the generator
# writes, and the third is a static library `everestrs`' own build script links
# against; without it the run dies in that build script with "Cannot find
# library path ... specified in EVEREST_RS_LINK_DEPENDENCIES". About four
# minutes on a warm ccache:
#
#   ninja -C build -j3 rust_workspace/Cargo.toml everestrs-link-dependencies.txt \
#     lib/everest/framework/everestrs/libeverestrs_sys.a \
#     lib/everest/framework/lib/libframework.so.0.25.0 \
#     lib/everest/log/lib/libeverest_log.a
#
# The three library paths are exactly the contents of
# `build/everestrs-link-dependencies.txt`; read that file rather than trusting
# the version number above.
set -u
W="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$W/build/rust_workspace" || {
    echo "no build/rust_workspace under $W; configure the build first" >&2
    exit 1
}
export EVEREST_CORE_ROOT="$W"
export EVEREST_RS_LINK_DEPENDENCIES="$W/build/everestrs-link-dependencies.txt"
exec cargo check -p RsEvseManager --bins \
  --config 'target.x86_64-unknown-linux-gnu.linker = "/usr/bin/c++"' \
  --target x86_64-unknown-linux-gnu "$@"
