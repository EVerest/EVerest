# Rust for EVerest recipes: cross-compiled with the prebuilt upstream toolchains of
# meta-rust-bin (https://github.com/rust-embedded/meta-rust-bin) and without network
# access. bitbake's crate:// fetcher unpacks crates into ${CARGO_HOME}/bitbake, which
# cargo reads as a directory source in place of crates.io. Recipes list their crates
# with cargo-update-recipe-crates and call everest_rust_do_configure from do_configure.
#
# The Rust that ships with scarthgap is 1.75, the EVerest crates need 1.82 or newer.
# meta-rust-bin picks its newest recipe unless pinned; rust-bin-cross follows cargo:
#   PREFERRED_VERSION_cargo-bin-cross-${TARGET_ARCH} = "1.86.0"

inherit ${@'rust_bin-common' if 'rust-bin-layer' in d.getVar('BBFILE_COLLECTIONS').split() else ''}

python () {
    if 'rust-bin-layer' not in d.getVar('BBFILE_COLLECTIONS').split():
        raise bb.parse.SkipRecipe("needs the meta-rust-bin layer for its Rust toolchain")
}

# cargo-bin-cross depends on the matching rust-bin-cross. Native recipes and x86_64
# machines build for the build architecture, provided as cargo-bin-native.
DEPENDS:append = " ${@'cargo-bin-native' if d.getVar('TARGET_ARCH') == d.getVar('BUILD_ARCH') else 'cargo-bin-cross-' + d.getVar('TARGET_ARCH')}"

EVEREST_RUST_TARGET ??= "${@rust_target(d, 'TARGET')}"
EVEREST_RUST_TARGET:class-native = "${@rust_target(d, 'BUILD')}"
EVEREST_RUST_BUILD = "${@rust_target(d, 'BUILD')}"

# rustc and the cc crate take a bare executable; CC, CXX and LDFLAGS carry arguments.
EVEREST_RUST_WRAPPER_DIR = "${WORKDIR}/rust-wrappers"
EVEREST_RUST_TARGET_LINKER = "${EVEREST_RUST_WRAPPER_DIR}/target-linker"
EVEREST_RUST_BUILD_LINKER = "${EVEREST_RUST_WRAPPER_DIR}/build-linker"

export CARGO_HOME = "${WORKDIR}/cargo_home"
export CARGO_NET_OFFLINE = "true"
export RUST_BACKTRACE = "1"
export PKG_CONFIG_ALLOW_CROSS = "1"
export RUSTFLAGS = "--remap-path-prefix=${WORKDIR}=${TARGET_DBGSRC_DIR}"

# Compilers for the cc crate: TARGET_* for the target, HOST_* for build scripts and
# proc macros. The target flags come from CFLAGS and CXXFLAGS, which bitbake exports.
export TARGET_CC = "${EVEREST_RUST_WRAPPER_DIR}/target-cc"
export TARGET_CXX = "${EVEREST_RUST_WRAPPER_DIR}/target-cxx"
export HOST_CC = "${EVEREST_RUST_WRAPPER_DIR}/build-cc"
export HOST_CXX = "${EVEREST_RUST_WRAPPER_DIR}/build-cxx"
export HOST_CFLAGS = "${BUILD_CFLAGS}"
export HOST_CXXFLAGS = "${BUILD_CXXFLAGS}"

everest_rust_wrapper() {
    wrapper="${EVEREST_RUST_WRAPPER_DIR}/$1"
    shift
    printf '#!/bin/sh\nexec %s "$@"\n' "$*" > "$wrapper"
    chmod +x "$wrapper"
}

everest_rust_do_configure() {
    mkdir -p "${EVEREST_RUST_WRAPPER_DIR}" "${CARGO_HOME}/bitbake"

    everest_rust_wrapper target-cc "${CC}"
    everest_rust_wrapper target-cxx "${CXX}"
    everest_rust_wrapper target-linker "${CXX} ${LDFLAGS}"
    everest_rust_wrapper build-cc "${BUILD_CC}"
    everest_rust_wrapper build-cxx "${BUILD_CXX}"
    everest_rust_wrapper build-linker "${BUILD_CXX} ${BUILD_LDFLAGS}"

    cat > "${CARGO_HOME}/config.toml" <<CARGO_CONFIG
[source.crates-io]
replace-with = "bitbake"
local-registry = "/nonexistent"

[source.bitbake]
directory = "${CARGO_HOME}/bitbake"

[target.${EVEREST_RUST_TARGET}]
linker = "${EVEREST_RUST_TARGET_LINKER}"
CARGO_CONFIG

    if [ "${EVEREST_RUST_TARGET}" != "${EVEREST_RUST_BUILD}" ]; then
        cat >> "${CARGO_HOME}/config.toml" <<CARGO_CONFIG

[target.${EVEREST_RUST_BUILD}]
linker = "${EVEREST_RUST_BUILD_LINKER}"
CARGO_CONFIG
    fi
}

# Cargo tells host and target apart by triple, so an x86_64 machine built on an
# x86_64 host shares one [target] section for both. Cargo's host-config separates them
# but is nightly-only; the channel override unlocks it on stable, as meta-rust-bin does.
everest_rust_host_config() {
    if [ "${EVEREST_RUST_TARGET}" = "${EVEREST_RUST_BUILD}" ] && [ "${HOST_SYS}" != "${BUILD_SYS}" ]; then
        export __CARGO_TEST_CHANNEL_OVERRIDE_DO_NOT_USE_THIS="nightly"
        export CARGO_UNSTABLE_TARGET_APPLIES_TO_HOST="true"
        export CARGO_UNSTABLE_HOST_CONFIG="true"
        export CARGO_TARGET_APPLIES_TO_HOST="false"
        export CARGO_HOST_LINKER="${EVEREST_RUST_BUILD_LINKER}"
    fi
}

do_compile:prepend() {
    everest_rust_host_config
}
