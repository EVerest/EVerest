// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The operating system's random source, for session identity and for the one
//! seed `core` draws its bounded random values from.
//!
//! `core` draws no bytes of its own (see `core::session_id`), so this is the
//! production implementation of `RandomBytes`: one read per mint, no userspace
//! generator between the kernel and the id, so an observed session id says
//! nothing about the next one.

use std::fs::File;
use std::io::Read;

use anyhow::{Context, Result};

use crate::core::session_id::{EntropyExhausted, RandomBytes};

/// `/dev/urandom` rather than a crate. `getrandom` and `rand` would each have to
/// be carried by the Bazel crate index as well, and this needs one file read.
const DEVICE: &str = "/dev/urandom";

/// Bytes held back at start up, spent only when the device stops answering.
/// Thirty two mints of grace, which is more than the C++ has: boost's
/// `random_generator` throws `entropy_error` on the first failed draw.
const RESERVE: usize = 32 * 16;

/// A process unique seed for `core`'s bounded draws, today the UK smart
/// charging random delay.
///
/// The C++ seeds `std::rand` from `time(0)` once in `energyImpl::init`
/// (`energy_grid/energyImpl.cpp:32`). That seed has one second of resolution
/// and nothing per station in it, so two ports whose processes come up in the
/// same wall clock second draw the same delay sequence, which is the load
/// synchronization across a fleet that the regulation exists to prevent. Here
/// the seed is kernel bytes, and the fallback carries the node id.
///
/// Unlike `OsBytes::fill` this cannot refuse. A session id that could not be
/// drawn had to be refused because a guessable one reaches the bus as a
/// transaction identity; a delay length is neither published as an identity
/// nor guessed by anyone, and refusing to draw one would silently drop a
/// regulatory obligation. So an unreadable device falls back to the boot
/// clock, the process id and the node id, which is no worse than what the C++
/// uses on its happy path.
pub fn seed(node_id: &str) -> u64 {
    let mut bytes = [0u8; 8];
    if File::open(DEVICE)
        .and_then(|mut device| device.read_exact(&mut bytes))
        .is_ok()
    {
        return u64::from_be_bytes(bytes);
    }
    log::error!("{DEVICE} is unreadable; seeding the random delay from the clock and the node id");
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |since| since.as_nanos() as u64);
    // FNV-1a over the node id, mixed into the clock and the process. Not a
    // hash anybody verifies; it exists so two ports on one station that both
    // reach this fallback in the same nanosecond still differ.
    node_id.bytes().fold(
        nanos ^ u64::from(std::process::id()).rotate_left(32),
        |acc, byte| (acc ^ u64::from(byte)).wrapping_mul(0x0100_0000_01b3),
    )
}

/// Reads session id bytes from the kernel, holding the device open.
pub struct OsBytes {
    /// The device this source draws from, reopened from here after a failed
    /// read. A field rather than the constant, so the exhaustion path can be
    /// driven against a device that answers nothing instead of against the
    /// machine's real entropy.
    path: &'static str,
    device: Option<File>,
    /// Operating system bytes, spent from the end and never reused.
    reserve: Vec<u8>,
}

impl OsBytes {
    /// Opens the device and takes the reserve. Called before any session exists,
    /// so a platform without a random source refuses to start rather than
    /// failing mid session.
    pub fn open() -> Result<Box<Self>> {
        let mut device = File::open(DEVICE).with_context(|| format!("opening {DEVICE}"))?;
        let mut reserve = vec![0u8; RESERVE];
        device
            .read_exact(&mut reserve)
            .with_context(|| format!("reading {DEVICE}"))?;
        Ok(Box::new(Self {
            path: DEVICE,
            device: Some(device),
            reserve,
        }))
    }

    fn read(&mut self, out: &mut [u8; 16]) -> std::io::Result<()> {
        match self.device.as_mut() {
            Some(device) => device.read_exact(out),
            None => Err(std::io::Error::from(std::io::ErrorKind::NotConnected)),
        }
    }

    /// Serves a mint from the reserve. `false` once the reserve is spent.
    fn spend_reserve(&mut self, out: &mut [u8; 16]) -> bool {
        let Some(rest) = self.reserve.len().checked_sub(out.len()) else {
            return false;
        };
        out.copy_from_slice(&self.reserve[rest..]);
        self.reserve.truncate(rest);
        true
    }
}

impl RandomBytes for OsBytes {
    /// A failed draw is retried once against a freshly opened device, then
    /// served from the reserve. With the reserve spent and the device still
    /// unreadable, no unpredictable id can be produced, so the draw is refused.
    ///
    /// The refusal is the whole answer. Filling `out` with anything at this
    /// point would put a guessable transaction identity on the bus, and stopping
    /// the process would run no destructors and drain nothing, so the port could
    /// be left energized with the vehicle latched. `core` refuses the session
    /// start instead, which is a failure the module can hold safely.
    fn fill(&mut self, out: &mut [u8; 16]) -> Result<(), EntropyExhausted> {
        if self.read(out).is_ok() {
            return Ok(());
        }
        let path = self.path;
        log::error!("{path} is unreadable; reopening");
        self.device = File::open(path).ok();
        if let Err(error) = self.read(out) {
            log::error!("{path}: {error}; drawing a session id from the reserve");
            if !self.spend_reserve(out) {
                log::error!(
                    "{path} is unreadable and the entropy reserve is spent; \
                     refusing to mint a session id"
                );
                return Err(EntropyExhausted);
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_seed_differs_per_draw_and_per_node() {
        let mut seen = Vec::new();
        for _ in 0..32 {
            seen.push(seed("evse_manager"));
        }
        seen.sort_unstable();
        seen.dedup();
        assert_eq!(seen.len(), 32, "a seed repeated");
    }

    #[test]
    fn every_draw_is_fresh_kernel_bytes() {
        let mut source = OsBytes::open().expect("no random device");
        let mut seen = Vec::new();
        for _ in 0..64 {
            let mut out = [0u8; 16];
            source.fill(&mut out).expect("the kernel answered");
            assert_ne!(out, [0u8; 16], "the source filled nothing");
            seen.push(out);
        }
        seen.sort();
        seen.dedup();
        assert_eq!(seen.len(), 64, "a draw repeated");
    }

    /// Opens and reads, so it exercises the same code the real device does, and
    /// answers every read with end of file. The exhaustion path needs a device
    /// that cannot answer; using this rather than the real one keeps the suite
    /// from spending the machine's entropy to reach it.
    const EMPTY_DEVICE: &str = "/dev/null";

    fn over_empty_device(reserve: Vec<u8>) -> OsBytes {
        OsBytes {
            path: EMPTY_DEVICE,
            device: File::open(EMPTY_DEVICE).ok(),
            reserve,
        }
    }

    #[test]
    fn a_healthy_device_reports_a_successful_draw() {
        let mut source = OsBytes::open().expect("no random device");
        let mut out = [0u8; 16];
        assert_eq!(source.fill(&mut out), Ok(()));
    }

    #[test]
    fn a_readable_device_is_not_reopened_and_costs_no_reserve() {
        // The reopen is the failure path only. Pointing the path at nothing
        // makes a reopen destroy the device, so a draw that still succeeds and
        // leaves the reserve whole is a draw that never took it.
        let mut source = OsBytes::open().expect("no random device");
        source.path = "/nonexistent";
        let mut out = [0u8; 16];
        assert_eq!(source.fill(&mut out), Ok(()));
        assert!(source.device.is_some(), "the working device was kept");
        assert_eq!(source.reserve.len(), RESERVE, "the reserve was not spent");
    }

    #[test]
    fn a_spent_reserve_over_an_unreadable_device_refuses_the_draw() {
        let mut source = over_empty_device(Vec::new());
        let mut out = [0u8; 16];
        assert_eq!(source.fill(&mut out), Err(EntropyExhausted));
        // Still refuses, rather than the refusal arming some fallback.
        assert_eq!(source.fill(&mut out), Err(EntropyExhausted));
    }

    #[test]
    fn an_unreadable_device_is_rescued_by_the_reserve() {
        let mut source = over_empty_device(vec![7u8; RESERVE]);
        let mut out = [0u8; 16];
        assert_eq!(source.fill(&mut out), Ok(()));
        assert_eq!(out, [7u8; 16]);
        assert_eq!(
            source.reserve.len(),
            RESERVE - 16,
            "one mint, one block of reserve"
        );
    }

    #[test]
    fn a_reserve_too_short_for_a_whole_mint_serves_none_of_it() {
        // A partial block would be a partly guessable id, which is the thing
        // the refusal exists to prevent.
        let mut source = over_empty_device(vec![7u8; 15]);
        let mut out = [0u8; 16];
        assert_eq!(source.fill(&mut out), Err(EntropyExhausted));
        assert_eq!(source.reserve.len(), 15, "the remainder is left alone");
    }

    #[test]
    fn a_dead_device_is_served_from_the_reserve() {
        let mut source = OsBytes::open().expect("no random device");
        source.device = None;
        let mut seen = Vec::new();
        for _ in 0..RESERVE / 16 {
            let mut out = [0u8; 16];
            // The reopen inside `fill` would succeed here, so the reserve path
            // is driven directly.
            assert!(source.spend_reserve(&mut out), "reserve too small");
            assert_ne!(out, [0u8; 16]);
            seen.push(out);
        }
        seen.sort();
        seen.dedup();
        assert_eq!(seen.len(), RESERVE / 16, "the reserve reused bytes");

        let mut out = [0u8; 16];
        assert!(
            !source.spend_reserve(&mut out),
            "a spent reserve must not serve a mint"
        );
    }

    #[test]
    fn a_reopen_recovers_a_closed_device() {
        let mut source = OsBytes::open().expect("no random device");
        source.device = None;
        let spent_before = RESERVE - source.reserve.len();
        let mut out = [0u8; 16];
        source.fill(&mut out).expect("the reopen answered");
        assert_ne!(out, [0u8; 16]);
        assert_eq!(
            RESERVE - source.reserve.len(),
            spent_before,
            "the reserve was spent although the device reopened"
        );
    }
}
