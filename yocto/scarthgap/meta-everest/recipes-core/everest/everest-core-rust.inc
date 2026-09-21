# The "rust" PACKAGECONFIG. CMake drives cargo; this adds what a bitbake build lacks:
# the toolchain and cross linker, crates fetched ahead of time and the git dependency
# of RsPaymentTerminal. Needs the meta-rust-bin layer, see everest_rust.bbclass.

inherit everest_rust cargo-update-recipe-crates

# ev-cli's jsonschema is built on rpds-py, a Rust extension compiled with poky's own
# cargo. Native build tools are staged transitively, so without this poky's
# /usr/bin/cargo and /usr/bin/rustc would land in this sysroot next to meta-rust-bin's
# and bitbake aborts on the duplicate files. Build tools are never needed recursively;
# poky applies the same rule to autoconf, quilt and friends.
SSTATE_EXCLUDEDEPS_SYSROOT += ".*->cargo-native .*->rust-native"

# modules/Cargo.lock pins the crates of the Bazel build and of the cargo workspace
# CMake generates. `bitbake -c update_crates everest-core` turns it into
# everest-core-crates.inc.
CARGO_LOCK_SRC_DIR = "${S}/modules"
require everest-core-crates.inc

# The revision in modules/HardwareDrivers/Payment/RsPaymentTerminal/Cargo.toml
SRC_URI += "git://github.com/Everest/zvt.git;protocol=https;branch=main;name=zvt;destsuffix=zvt"
SRCREV_zvt = "c784c20401764978fd6287a55e50bc65d083d68c"

EXTRA_OECMAKE += " \
    -DEVEREST_RS_TARGET_TRIPLE=${EVEREST_RUST_TARGET} \
    -DEVEREST_RS_LINKER=${EVEREST_RUST_TARGET_LINKER} \
"

# Size over speed for the module binaries, matching the Bazel release configuration.
# panic=abort ends a panicking module process without unwinding, so Drop code does not run.
export CARGO_PROFILE_RELEASE_OPT_LEVEL = "z"
export CARGO_PROFILE_RELEASE_LTO = "fat"
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS = "1"
export CARGO_PROFILE_RELEASE_PANIC = "abort"

do_configure() {
    everest_rust_do_configure
    cat >> "${CARGO_HOME}/config.toml" <<CARGO_CONFIG

[patch."https://github.com/Everest/zvt.git"]
zvt = { path = "${WORKDIR}/zvt/zvt" }
zvt_feig_terminal = { path = "${WORKDIR}/zvt/zvt_feig_terminal" }
CARGO_CONFIG

    cmake_do_configure

    # CMake writes the cargo workspace to ${B}/rust_workspace while building; the lock
    # file makes cargo resolve to the crate versions that were fetched.
    install -D -m 0644 ${S}/modules/Cargo.lock ${B}/rust_workspace/Cargo.lock
}
