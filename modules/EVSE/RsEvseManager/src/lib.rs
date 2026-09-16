// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Rust EVSE manager: AC basic, AC with ISO 15118 high level communication,
//! and DC charging including bidirectional power transfer.
//!
//! Neither `core` nor `boundary` depends on `everestrs`. The domain logic and
//! the event loop that drives it are both compiled and tested standalone; only
//! the binary target binds to the generated framework bindings.

pub mod boundary;
pub mod core;

#[cfg(test)]
mod assert_hygiene;
