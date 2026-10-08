SUMMARY = "cxxbridge, the C++ code generator of the cxx crate"
HOMEPAGE = "https://cxx.rs"
LICENSE = "MIT | Apache-2.0"
LIC_FILES_CHKSUM = " \
    file://LICENSE-MIT;md5=b377b220f43d747efdec40d69fcaa69d \
    file://LICENSE-APACHE;md5=22a53954e4e0ec258dfce4391e905dac \
"

# everest-core generates the everestrs bindings with this tool at build time. The
# version must match CXXBRIDGE_VERSION in lib/everest/framework/everestrs/CMakeLists.txt,
# otherwise CMake tries to cargo install it, which fails without network access.

SRC_URI = "crate://crates.io/cxxbridge-cmd/${PV};name=cxxbridge-cmd"
SRC_URI[cxxbridge-cmd.sha256sum] = "d0956799fa8678d4c50eed028f2de1c0552ae183c76e976cf7ca8c4e36a7c328"
S = "${CARGO_VENDORING_DIRECTORY}/cxxbridge-cmd-${PV}"

# `bitbake -c update_crates cxxbridge-cmd-native` regenerates this from the crate's Cargo.lock
require cxxbridge-cmd-crates.inc

inherit everest_rust cargo cargo-update-recipe-crates native
