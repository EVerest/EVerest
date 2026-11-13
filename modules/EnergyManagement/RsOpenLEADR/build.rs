// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
use everestrs_build::Builder;

pub fn main() {
    Builder::new(
        "manifest.yaml",
        vec![std::env::var("EVEREST_CORE_ROOT").unwrap()],
    )
    .generate()
    .unwrap();

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=manifest.yaml");
}
