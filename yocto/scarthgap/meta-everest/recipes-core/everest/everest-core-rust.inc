# The "rust" PACKAGECONFIG. CMake drives cargo; this adds what a bitbake build lacks:
# the Rust toolchain and cross linker, crates fetched ahead of time and the git
# dependency of RsPaymentTerminal. Requires a current Rust, see everest_rust.bbclass.

inherit everest_rust cargo_common cargo-update-recipe-crates

# modules/Cargo.lock pins the crates of the Bazel build and of the cargo workspace
# CMake generates. `bitbake -c update_crates everest-core` turns it into
# everest-core-crates.inc.
CARGO_LOCK_SRC_DIR = "${S}/modules"
require everest-core-crates.inc

# The revision in modules/HardwareDrivers/Payment/RsPaymentTerminal/Cargo.toml. Without a
# name parameter cargo_common leaves the patching below to us; its own would point cargo
# at the repository root instead of the crate directories.
SRC_URI += "git://github.com/Everest/zvt.git;protocol=https;branch=main;destsuffix=zvt"
SRCREV = "c784c20401764978fd6287a55e50bc65d083d68c"

EXTRA_OECMAKE += " \
    -DEVEREST_RS_TARGET_TRIPLE=${RUST_HOST_SYS} \
    -DEVEREST_RS_LINKER=${RUST_TARGET_CCLD} \
"

# cargo runs under CMake, so what cargo_common only prepares as variables is exported here.
export RUSTFLAGS
export CARGO_NET_OFFLINE = "true"
# cargo_common points cargo at ${B}/target; CMake installs from its own workspace.
export CARGO_TARGET_DIR = "${B}/rust_workspace/target"

# Size over speed for the module binaries, matching the Bazel release configuration.
# panic=abort ends a panicking module process without unwinding, so Drop code does not run.
export CARGO_PROFILE_RELEASE_OPT_LEVEL = "z"
export CARGO_PROFILE_RELEASE_LTO = "fat"
export CARGO_PROFILE_RELEASE_CODEGEN_UNITS = "1"
export CARGO_PROFILE_RELEASE_PANIC = "abort"

do_configure() {
    # cargo ignores config.toml when a file named config sits next to it
    rm -f "${CARGO_HOME}/config" "${CARGO_HOME}/config.toml"
    cargo_common_do_configure
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
