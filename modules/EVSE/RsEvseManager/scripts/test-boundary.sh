#!/bin/bash
# Run the tests that live in src/main.rs.
#
# test-core.sh strips main.rs, so until this existed no gate ran these at all.
# Adding it immediately caught a boundary arm that had been telling every
# plugged-in vehicle to stop charging. The drop-line ledger tests
# (dropped_hlc_facts, ported_hlc_facts) also live here.
#
# The test binary links the framework, so it needs an installed tree under
# build/dist/lib64 and exits 127 with "cannot open shared object file" without
# one. A full `ninja -C build install` does not get there: it stops in
# modules/EnergyManagement/EEBUS/grpc_libs on a protobuf gencode and runtime
# version mismatch in checked-in generated sources, which has nothing to do
# with this module. Install the four targets this binary actually needs
# instead, which takes a couple of minutes on a warm ccache:
#
#   ninja -C build -j8 lib/install \
#     _deps/libfmt-build/install _deps/date-build/install
#
# `LD_LIBRARY_PATH=build/dist/lib64 ldd <the test binary>` names anything still
# missing; the binary path is in this script's own cargo output.
set -u
W="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd "$W/build/rust_workspace" || {
    echo "no build/rust_workspace under $W; configure the build first" >&2
    exit 1
}
export EVEREST_CORE_ROOT="$W"
export EVEREST_RS_LINK_DEPENDENCIES="$W/build/everestrs-link-dependencies.txt"
export LD_LIBRARY_PATH="$W/build/dist/lib64"
exec cargo test -p RsEvseManager --bins \
  --config 'target.x86_64-unknown-linux-gnu.linker = "/usr/bin/c++"' \
  --target x86_64-unknown-linux-gnu "$@"
