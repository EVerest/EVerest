// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Session identity.
//!
//! Ports `utils::generate_session_id` (`modules/EVSE/EvseManager/utils.hpp:31`),
//! which formats one boost v4 uuid three ways and reads no clock: all three
//! `session_id_type` choices need nothing but sixteen random bytes.
//!
//! `core` performs no I/O and holds no random source, so those bytes are not
//! drawn here. The boundary hands in a `RandomBytes` source and every mint
//! draws sixteen fresh bytes from it, which is what the C++ does: each
//! `everest::helpers` form calls the generator again (`helpers.cpp:86-103`).
//! Stretching one seed would not do. A session id is published on the bus and
//! reaches the CSMS, so one observed id must not reveal the next one.
//!
//! The minted value is also the powermeter transaction id
//! (`Charger.cpp:1410`), so the transaction lifecycle owner reads
//! `Session::id` rather than minting a second identity.

use super::config::SessionIdType;

/// Standard base64, the alphabet `openssl::base64_encode` writes.
const BASE64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/// No unpredictable bytes could be produced, so no identity can be minted.
///
/// A distinct type rather than a bool: the one thing a caller must not do with
/// this answer is substitute an id of its own, and a named refusal says so at
/// every call site.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EntropyExhausted;

/// Sixteen fresh random bytes, drawn wherever the I/O is allowed.
///
/// The boundary owns the policy for a failed draw, because `core` can neither
/// log a device error nor reopen a device. What it does not own is the remedy
/// for having no bytes left at all: a session id reaches the bus as the
/// transaction identity, so the only answer that keeps a guessable id off the
/// wire is to produce none. It must never fill with a value it has returned
/// before, and it must never answer `Ok` with anything it did not draw.
pub trait RandomBytes: Send {
    fn fill(&mut self, out: &mut [u8; 16]) -> Result<(), EntropyExhausted>;
}

/// Mints session ids from a boundary supplied byte source.
pub struct SessionIds {
    kind: SessionIdType,
    source: Box<dyn RandomBytes>,
}

impl SessionIds {
    pub fn new(kind: SessionIdType, source: Box<dyn RandomBytes>) -> Self {
        Self { kind, source }
    }

    /// A fresh identity. `Charger.cpp:1381` calls the C++ equivalent once per
    /// session start.
    ///
    /// Fallible, and the failure is the whole answer: there is no counter, no
    /// stretched seed and no previous id to fall back to, because every one of
    /// those puts a guessable transaction identity on the bus. The caller
    /// refuses the session start instead.
    pub fn mint(&mut self) -> Result<String, EntropyExhausted> {
        let mut bytes = [0u8; 16];
        self.source.fill(&mut bytes)?;
        // boost's `random_generator` sets these, so the base64 forms carry them
        // too: they encode the same uuid bytes.
        bytes[6] = (bytes[6] & 0x0f) | 0x40;
        bytes[8] = (bytes[8] & 0x3f) | 0x80;

        Ok(match self.kind {
            SessionIdType::Uuid => hyphenated(&bytes),
            // 22 characters, padding stripped.
            SessionIdType::UuidBase64 => base64(&bytes),
            // 16 characters. Twelve bytes divide by three, so nothing is padded.
            SessionIdType::ShortBase64 => base64(&bytes[..12]),
        })
    }
}

/// A deterministic source, for tests only. splitmix64: a counter run through a
/// strong finalizer, so a seed fixes the whole sequence and nothing repeats
/// within its period.
#[cfg(test)]
pub struct SeededBytes {
    state: u64,
    stream: u64,
}

#[cfg(test)]
impl SeededBytes {
    pub fn new(seed: [u64; 2]) -> Box<Self> {
        Box::new(Self {
            state: seed[0],
            stream: seed[1],
        })
    }

    fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut z = self.state ^ self.stream;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        z ^ (z >> 31)
    }
}

#[cfg(test)]
impl RandomBytes for SeededBytes {
    fn fill(&mut self, out: &mut [u8; 16]) -> Result<(), EntropyExhausted> {
        out[..8].copy_from_slice(&self.next().to_be_bytes());
        out[8..].copy_from_slice(&self.next().to_be_bytes());
        Ok(())
    }
}

/// A source that can answer with nothing unpredictable, for tests only. This is
/// what an unreadable device over a spent reserve looks like from inside
/// `core`, driven here rather than by exhausting the machine's real entropy.
#[cfg(test)]
pub struct ExhaustedBytes;

#[cfg(test)]
impl RandomBytes for ExhaustedBytes {
    fn fill(&mut self, _out: &mut [u8; 16]) -> Result<(), EntropyExhausted> {
        Err(EntropyExhausted)
    }
}

fn hyphenated(bytes: &[u8; 16]) -> String {
    let mut out = String::with_capacity(36);
    for (index, byte) in bytes.iter().enumerate() {
        if matches!(index, 4 | 6 | 8 | 10) {
            out.push('-');
        }
        out.push_str(&format!("{byte:02x}"));
    }
    out
}

fn base64(bytes: &[u8]) -> String {
    let mut out = String::with_capacity(bytes.len().div_ceil(3) * 4);
    for chunk in bytes.chunks(3) {
        let mut block = [0u8; 3];
        block[..chunk.len()].copy_from_slice(chunk);
        let packed = u32::from(block[0]) << 16 | u32::from(block[1]) << 8 | u32::from(block[2]);
        // One output character per six input bits, dropping those that encode
        // nothing but the zero fill. That is the padding strip.
        for shift in 0..=chunk.len() {
            out.push(BASE64[(packed >> (18 - 6 * shift)) as usize & 0x3f] as char);
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::SessionIdType;

    /// Hands out the bytes it is given, in order, and counts the draws. A mint
    /// that ignores its source cannot satisfy the byte exact expectations below.
    struct ScriptedBytes {
        blocks: Vec<[u8; 16]>,
        draws: usize,
    }

    impl ScriptedBytes {
        fn new(blocks: Vec<[u8; 16]>) -> Box<Self> {
            Box::new(Self { blocks, draws: 0 })
        }
    }

    impl RandomBytes for ScriptedBytes {
        fn fill(&mut self, out: &mut [u8; 16]) -> Result<(), EntropyExhausted> {
            *out = self.blocks[self.draws];
            self.draws += 1;
            Ok(())
        }
    }

    /// The draw count, read back after the source has been moved into
    /// `SessionIds`. A `Box<dyn RandomBytes>` cannot be inspected, so the count
    /// is shared.
    struct CountingBytes {
        draws: std::sync::Arc<std::sync::atomic::AtomicUsize>,
    }

    impl RandomBytes for CountingBytes {
        fn fill(&mut self, out: &mut [u8; 16]) -> Result<(), EntropyExhausted> {
            let n = self
                .draws
                .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            // Distinct per draw, so a mint that draws once and stretches shows
            // up as equal ids rather than as a passing test.
            out.fill(n as u8);
            Ok(())
        }
    }

    #[test]
    fn a_source_that_cannot_answer_refuses_the_mint() {
        for kind in [
            SessionIdType::Uuid,
            SessionIdType::UuidBase64,
            SessionIdType::ShortBase64,
        ] {
            let mut ids = SessionIds::new(kind, Box::new(ExhaustedBytes));
            assert_eq!(ids.mint(), Err(EntropyExhausted), "{kind:?}");
        }
    }

    #[test]
    fn a_refused_mint_invents_no_substitute_on_a_later_attempt() {
        // The refusal must not be remembered as an id, nor turn into a counter
        // the next call serves from: a repeated attempt refuses again.
        let mut ids = SessionIds::new(SessionIdType::Uuid, Box::new(ExhaustedBytes));
        assert!(ids.mint().is_err());
        assert!(ids.mint().is_err());
    }

    #[test]
    fn a_uuid_id_has_the_shape_boost_produces() {
        let mut ids = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([1, 2]));
        let id = ids.mint().expect("a healthy source mints");
        assert_eq!(id.len(), 36, "{id}");
        let groups: Vec<&str> = id.split('-').collect();
        assert_eq!(
            groups.iter().map(|g| g.len()).collect::<Vec<_>>(),
            vec![8, 4, 4, 4, 12],
            "{id}"
        );
        assert!(
            id.chars().all(|c| c.is_ascii_hexdigit() || c == '-'),
            "{id}"
        );
        assert_eq!(groups[2].as_bytes()[0], b'4', "version 4: {id}");
        assert!(
            matches!(groups[3].as_bytes()[0], b'8' | b'9' | b'a' | b'b'),
            "RFC 4122 variant: {id}"
        );
    }

    #[test]
    fn a_base64_uuid_id_is_twenty_two_unpadded_characters() {
        let mut ids = SessionIds::new(SessionIdType::UuidBase64, SeededBytes::new([1, 2]));
        let id = ids.mint().expect("a healthy source mints");
        assert_eq!(id.len(), 22, "{id}");
        assert!(!id.contains('='), "padding is stripped: {id}");
    }

    #[test]
    fn a_short_base64_id_is_sixteen_characters() {
        let mut ids = SessionIds::new(SessionIdType::ShortBase64, SeededBytes::new([1, 2]));
        let id = ids.mint().expect("a healthy source mints");
        assert_eq!(id.len(), 16, "{id}");
        assert!(!id.contains('='), "twelve bytes need no padding: {id}");
    }

    #[test]
    fn every_mint_is_a_new_identity() {
        let mut ids = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([7, 11]));
        let minted: Vec<String> = (0..1000)
            .map(|_| ids.mint().expect("a healthy source mints"))
            .collect();
        let mut unique = minted.clone();
        unique.sort();
        unique.dedup();
        assert_eq!(unique.len(), minted.len(), "no id repeats");
    }

    #[test]
    fn the_injected_source_is_the_only_entropy_the_core_reads() {
        // Two deterministic sources with the same seed agree, which is what lets
        // a session sequence be asserted without a clock or a random source in
        // `core`.
        let mut a = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([42, 43]));
        let mut b = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([42, 43]));
        for _ in 0..8 {
            assert_eq!(a.mint(), b.mint());
        }
    }

    #[test]
    fn a_different_seed_yields_a_different_sequence() {
        let mut a = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([42, 43]));
        let mut b = SessionIds::new(SessionIdType::Uuid, SeededBytes::new([42, 44]));
        assert_ne!(a.mint(), b.mint());
    }

    #[test]
    fn base64_output_uses_the_standard_alphabet() {
        let mut ids = SessionIds::new(
            SessionIdType::UuidBase64,
            SeededBytes::new([0xdead, 0xbeef]),
        );
        for _ in 0..100 {
            let id = ids.mint().expect("a healthy source mints");
            assert!(
                id.chars()
                    .all(|c| c.is_ascii_alphanumeric() || c == '+' || c == '/'),
                "{id}"
            );
        }
    }

    #[test]
    fn a_mint_is_the_bytes_the_source_handed_over() {
        let first = [
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
            0x0e, 0x0f,
        ];
        let second = [0xffu8; 16];
        let mut ids = SessionIds::new(SessionIdType::Uuid, ScriptedBytes::new(vec![first, second]));
        // Only the version nibble and the variant bits are overwritten.
        assert_eq!(
            ids.mint().as_deref(),
            Ok("00010203-0405-4607-8809-0a0b0c0d0e0f")
        );
        assert_eq!(
            ids.mint().as_deref(),
            Ok("ffffffff-ffff-4fff-bfff-ffffffffffff")
        );
    }

    #[test]
    fn each_mint_draws_from_the_source_again() {
        let draws = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
        let mut ids = SessionIds::new(
            SessionIdType::Uuid,
            Box::new(CountingBytes {
                draws: std::sync::Arc::clone(&draws),
            }),
        );
        let a = ids.mint();
        let b = ids.mint();
        assert_eq!(
            draws.load(std::sync::atomic::Ordering::Relaxed),
            2,
            "one draw per mint, not one per source"
        );
        assert_ne!(a, b, "{a:?}");
    }

    #[test]
    fn the_base64_forms_draw_per_mint_too() {
        for kind in [SessionIdType::UuidBase64, SessionIdType::ShortBase64] {
            let draws = std::sync::Arc::new(std::sync::atomic::AtomicUsize::new(0));
            let mut ids = SessionIds::new(
                kind,
                Box::new(CountingBytes {
                    draws: std::sync::Arc::clone(&draws),
                }),
            );
            let a = ids.mint();
            let b = ids.mint();
            assert_eq!(
                draws.load(std::sync::atomic::Ordering::Relaxed),
                2,
                "{kind:?}"
            );
            assert_ne!(a, b, "{kind:?}: {a:?}");
        }
    }
}
