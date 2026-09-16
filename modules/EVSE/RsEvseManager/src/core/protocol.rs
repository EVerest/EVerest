// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! `selected_protocol`: which protocol the port is currently talking, as the
//! `evse_manager` interface reports it.
//!
//! `EvseManager::selected_protocol` is a plain `std::string` member with five
//! writers spread over two files and exactly one publish site. That shape is
//! what this module exists to replace: the writers are here as named
//! transitions on one value, and the publish site is a predicate over the
//! session event that reaches the wire.
//!
//! It is not a pass-through. The other seven variables on that group are the
//! record their producer handed the module; this one is state the module keeps,
//! and every one of its five writers is a decision about what the port is doing.

use super::hlc::setup::PresentedMode;
use super::session::SessionEvent;

/// `"IEC61851-1"`, the basic AC pilot protocol, spelled as the C++ spells it.
///
/// Two writers name it and a consumer compares the string, so it is one
/// constant rather than two literals.
const BASIC_AC: &str = "IEC61851-1";

/// `"Unknown"`, which is `EvseManager.hpp`'s member initializer and what the
/// end of a transaction and the end of a session both return the value to.
const UNKNOWN: &str = "Unknown";

/// The protocol the port reports.
///
/// Three states rather than a `String`, so that "nothing negotiated" and
/// "basic AC" cannot be confused with a stack that negotiated a protocol whose
/// name happens to match: only `Negotiated` carries text, and it carries the
/// stack's own text verbatim rather than a narrowing of it. `EvseV2G` and
/// `Evse15118D20` each publish their own spelling and the C++ stores whichever
/// arrives, so any name this port invented for those would be a name no
/// deployment sends.
///
/// Every construction below is spelled `SelectedProtocol::<Variant>` rather
/// than `Self::<Variant>`, and the patterns keep `Self::`.
/// `scripts/reachability.py` reads Rust as text and censuses producers by the
/// enum's name, so a variant only ever written as `Self::` reads as one nothing
/// constructs. The alternative is leaving all three off the audit, which is the
/// class of blindness that hid both `SessionPhase` defects.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub enum SelectedProtocol {
    /// No protocol is in use. The value at boot, for every charge mode: the
    /// only `setup_*` function that writes this field is `setup_AC_mode`, and
    /// `EvseManager::ready` reaches neither it nor any other writer.
    #[default]
    Unknown,
    /// Basic AC signalling.
    BasicAc,
    /// What the high level communication stack reported, carried whole.
    Negotiated(String),
}

impl SelectedProtocol {
    /// The value as the interface carries it.
    pub fn wire(&self) -> String {
        match self {
            Self::Unknown => UNKNOWN.to_owned(),
            Self::BasicAc => BASIC_AC.to_owned(),
            Self::Negotiated(protocol) => protocol.clone(),
        }
    }

    /// The three writes the session lifecycle makes, in one match.
    ///
    /// Each is a separate C++ lambda in `evse/evse_managerImpl.cpp`, and each
    /// writes before it publishes anything:
    ///
    /// - the session started connection sets `BasicAc`, unconditionally and for
    ///   every charge mode, so a session opens as basic AC and the stack
    ///   overwrites it when it negotiates;
    /// - the transaction finished connection sets `Unknown`;
    /// - the simple event connection sets `Unknown` on `SessionFinished`.
    ///
    /// The first two events are not ones the value is published after, so only
    /// the third is visible on the wire in the same announcement that made it.
    /// The other two are visible on the next announcement, which is what the
    /// C++ does.
    pub fn on_session_event(&mut self, event: SessionEvent) {
        match event {
            SessionEvent::SessionStarted => *self = SelectedProtocol::BasicAc,
            SessionEvent::TransactionFinished | SessionEvent::SessionFinished => {
                *self = SelectedProtocol::Unknown
            }
            _ => {}
        }
    }

    /// The stack negotiated a protocol (`subscribe_selected_protocol`,
    /// `EvseManager.cpp:1070-1071`).
    ///
    /// The C++ handler is one assignment and publishes nothing, so a
    /// negotiation reaches the wire on the next announcement rather than at
    /// once. Kept, because publishing here would report a protocol change at a
    /// moment the C++ reports nothing and a consumer polling the variable would
    /// see a different sequence.
    pub fn note_negotiated(&mut self, protocol: String) {
        *self = SelectedProtocol::Negotiated(protocol);
    }

    /// The `ac_with_soc` flip back to basic AC (`EvseManager::setup_AC_mode`'s
    /// `else`, reached only from `switch_AC_mode` and the `subscribe_dlink_error`
    /// arm, both of which pass `false`).
    ///
    /// This is the whole of that function's AC announcement, which is why
    /// `docs/architecture.md` recorded it as the one thing the AC half of the
    /// mode switch says. The DC half (`setup_fake_DC_mode`) does not touch the
    /// field, so a flip to fake DC leaves whatever the session had.
    pub fn note_mode_announced(&mut self, mode: PresentedMode) {
        match mode {
            PresentedMode::Ac => *self = SelectedProtocol::BasicAc,
            PresentedMode::Dc => {}
        }
    }
}

/// Whether the C++ republishes `selected_protocol` after announcing this event.
///
/// There is one `publish_selected_protocol` call in the C++ and it sits at the
/// end of the `signal_simple_event` connection, so the answer is exactly
/// "`Charger` raised this event through `signal_simple_event`". That is a
/// property of which signal each event travels on and nothing else, which is
/// why it is written out here rather than derived from anything about the event:
/// `ChargingPausedEvse` reads as simple, has an arm in that very lambda, and is
/// never raised through it because it has a signal of its own.
///
/// Exhaustive, so a twentieth event cannot be added without choosing.
/// `tests::the_simple_events_are_the_ones_the_cpp_signals` pins the list
/// against the `signal_simple_event` call sites.
pub fn published_after(event: SessionEvent) -> bool {
    match event {
        // `Charger.cpp:1774`, `:1793`, `:214`, `:1823`, `:1826`, `:1888`,
        // `:282`, `:715`, `:803`, `:917`, `:593`, `:1052`, `:1473`, `:1800`.
        SessionEvent::Authorized
        | SessionEvent::Deauthorized
        | SessionEvent::Enabled
        | SessionEvent::Disabled
        | SessionEvent::AuthRequired
        | SessionEvent::PrepareCharging
        | SessionEvent::ChargingStarted
        | SessionEvent::ChargingPausedEv
        | SessionEvent::SwitchingPhases
        | SessionEvent::StoppingCharging
        | SessionEvent::ChargingFinished
        | SessionEvent::SessionFinished
        | SessionEvent::PluginTimeout => true,

        // Each of these has its own `Charger` signal, so the C++ lambda that
        // publishes the protocol never runs for it:
        // `signal_session_started_event`, `signal_transaction_started_event`,
        // `signal_transaction_finished_event`, `signal_charging_paused_evse_event`,
        // `signal_session_resumed_event`, and `EvseManager::signalReservationEvent`
        // for the two reservation events.
        SessionEvent::SessionStarted
        | SessionEvent::TransactionStarted
        | SessionEvent::TransactionFinished
        | SessionEvent::ChargingPausedEvse
        | SessionEvent::SessionResumed
        | SessionEvent::ReservationStart
        | SessionEvent::ReservationEnd => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Every event this module publishes, so both halves of `published_after`
    /// are driven and neither list can shrink unnoticed.
    const EVERY_EVENT: &[SessionEvent] = &[
        SessionEvent::Authorized,
        SessionEvent::Deauthorized,
        SessionEvent::Enabled,
        SessionEvent::Disabled,
        SessionEvent::SessionStarted,
        SessionEvent::SessionResumed,
        SessionEvent::AuthRequired,
        SessionEvent::TransactionStarted,
        SessionEvent::PrepareCharging,
        SessionEvent::ChargingStarted,
        SessionEvent::ChargingPausedEv,
        SessionEvent::ChargingPausedEvse,
        SessionEvent::SwitchingPhases,
        SessionEvent::StoppingCharging,
        SessionEvent::ChargingFinished,
        SessionEvent::TransactionFinished,
        SessionEvent::SessionFinished,
        SessionEvent::ReservationStart,
        SessionEvent::ReservationEnd,
        SessionEvent::PluginTimeout,
    ];

    /// The thirteen events `Charger` raises through `signal_simple_event`, read
    /// off its call sites, and the seven it does not.
    ///
    /// `ChargingFinished` (`Charger.cpp:1548`) is the thirteenth. It had no
    /// variant here at all, which made this port publish one fewer protocol
    /// announcement than the C++ does; that was a missing session event rather
    /// than a missing protocol write, and porting the event is what closed it.
    #[test]
    fn the_simple_events_are_the_ones_the_cpp_signals() {
        let published: Vec<SessionEvent> = EVERY_EVENT
            .iter()
            .copied()
            .filter(|event| published_after(*event))
            .collect();
        assert_eq!(
            published,
            vec![
                SessionEvent::Authorized,
                SessionEvent::Deauthorized,
                SessionEvent::Enabled,
                SessionEvent::Disabled,
                SessionEvent::AuthRequired,
                SessionEvent::PrepareCharging,
                SessionEvent::ChargingStarted,
                SessionEvent::ChargingPausedEv,
                SessionEvent::SwitchingPhases,
                SessionEvent::StoppingCharging,
                SessionEvent::ChargingFinished,
                SessionEvent::SessionFinished,
                SessionEvent::PluginTimeout,
            ]
        );
    }

    /// The value at boot, for every charge mode. `EvseManager::ready` reaches
    /// no writer of this field, so a port that never opens a session and never
    /// negotiates reports `Unknown` for its whole life.
    #[test]
    fn a_port_that_has_done_nothing_reports_unknown() {
        assert_eq!(SelectedProtocol::default().wire(), "Unknown");
    }

    /// A session start says basic AC whatever the deployment is, because the
    /// C++ assignment is the first statement of the session started lambda and
    /// is not gated on charge mode or on whether a stack is wired.
    #[test]
    fn a_session_start_reports_basic_ac() {
        let mut protocol = SelectedProtocol::default();
        protocol.on_session_event(SessionEvent::SessionStarted);
        assert_eq!(protocol.wire(), "IEC61851-1");
    }

    /// The stack's own spelling reaches the wire unchanged. Two different
    /// values, because a port that mapped them onto an enum of its own would
    /// pass this with one.
    #[test]
    fn a_negotiated_protocol_is_carried_verbatim() {
        let mut protocol = SelectedProtocol::default();
        protocol.note_negotiated("ISO15118-2".to_owned());
        assert_eq!(protocol.wire(), "ISO15118-2");
        protocol.note_negotiated("urn:iso:std:iso:15118:-20:AC".to_owned());
        assert_eq!(protocol.wire(), "urn:iso:std:iso:15118:-20:AC");
    }

    /// A negotiated protocol survives everything in the session but its two
    /// endings, which is what makes the value worth publishing at all: it is
    /// reported through the whole charge loop.
    #[test]
    fn a_negotiated_protocol_survives_the_charge_loop() {
        let mut protocol = SelectedProtocol::default();
        protocol.on_session_event(SessionEvent::SessionStarted);
        protocol.note_negotiated("ISO15118-2".to_owned());
        for event in [
            SessionEvent::AuthRequired,
            SessionEvent::Authorized,
            SessionEvent::TransactionStarted,
            SessionEvent::PrepareCharging,
            SessionEvent::ChargingStarted,
            SessionEvent::ChargingPausedEv,
            SessionEvent::ChargingPausedEvse,
            SessionEvent::SwitchingPhases,
            SessionEvent::StoppingCharging,
        ] {
            protocol.on_session_event(event);
            assert_eq!(protocol.wire(), "ISO15118-2", "{event:?}");
        }
    }

    /// Both endings clear it, and they are separate C++ lambdas rather than one
    /// with two triggers, so both are driven.
    #[test]
    fn both_endings_return_the_protocol_to_unknown() {
        for ending in [
            SessionEvent::TransactionFinished,
            SessionEvent::SessionFinished,
        ] {
            let mut protocol = SelectedProtocol::default();
            protocol.note_negotiated("ISO15118-2".to_owned());
            protocol.on_session_event(ending);
            assert_eq!(protocol.wire(), "Unknown", "{ending:?}");
        }
    }

    /// The `ac_with_soc` flip: back to AC says basic AC, and the flip to fake
    /// DC says nothing at all. The asymmetry is the C++'s, and a port that
    /// wrote the field in both directions would report basic AC while the
    /// vehicle was being shown a DC port.
    #[test]
    fn only_the_ac_half_of_the_mode_flip_writes_the_protocol() {
        let mut protocol = SelectedProtocol::default();
        protocol.note_negotiated("ISO15118-2".to_owned());
        protocol.note_mode_announced(PresentedMode::Dc);
        assert_eq!(protocol.wire(), "ISO15118-2");
        protocol.note_mode_announced(PresentedMode::Ac);
        assert_eq!(protocol.wire(), "IEC61851-1");
    }
}
