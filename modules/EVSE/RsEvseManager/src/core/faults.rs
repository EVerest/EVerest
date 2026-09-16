// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Error handling: which faults block charging, and the `Inoperative` error.
//!
//! A port of `modules/EVSE/EvseManager/ErrorHandling.cpp`. The C++ version reads
//! the active error set back out of each requirement's error state monitor on
//! every callback, from whatever thread the callback arrived on, and serializes
//! the whole read-compare-clear-raise sequence under one monitor handle. Here
//! `Faults` owns the cause set outright, so ordering is the single writer's and
//! there is nothing to lock.
//!
//! `Faults` performs no I/O and reads no clock. It answers in signals; mapping
//! those onto framework calls belongs to the boundary.

use std::collections::BTreeSet;

pub use super::event::Severity;
use super::event::{ErrorEvent, ErrorSource};

/// The error type raised on this module's own `evse_manager` interface when the
/// active fault set blocks charging.
pub const INOPERATIVE: &str = "evse_manager/Inoperative";

/// Raised when a plug in produced no authorization, gated on `raise_mrec9`
/// (`ErrorHandling.cpp:337-343`).
pub const MREC9_AUTHORIZATION_TIMEOUT: &str = "evse_manager/MREC9AuthorizationTimeout";

/// Raised when the powermeter refuses to open the billing transaction, gated on
/// `fail_on_powermeter_errors` (`Charger.cpp:1427-1432`). A customer who cannot
/// be billed is not charged, so the fault set answers this with `Inoperative`
/// and the error shutdown class.
pub const POWERMETER_TRANSACTION_START_FAILED: &str =
    "evse_manager/PowermeterTransactionStartFailed";

/// Raised when a vehicle draws more than the current the port signalled for
/// longer than `soft_over_current_timeout_ms`
/// (`ErrorHandling.cpp:128-135`).
///
/// `Severity::High` there, because IEC 61851-23 table CC.10 classes it as an
/// emergency shutdown. It appears in no `IgnoreErrors` row, so the fault set
/// answers it with `Inoperative` and the session comes down; see
/// `core::soft_oc`.
pub const MREC4_OVER_CURRENT_FAILURE: &str = "evse_manager/MREC4OverCurrentFailure";

/// Raised when the isolation monitor reports a resistance below the connector's
/// threshold, or a voltage to earth out of range, while the session is charging
/// (`ErrorHandling::raise_isolation_resistance_fault`). The two are the same
/// type under different sub types, `Resistance` and `VoltageToEarth`.
///
/// `Severity::Medium` there, because IEC 61851-23 table CC.10 classes it as an
/// error shutdown rather than an emergency one.
pub const MREC22_RESISTANCE: &str = "evse_manager/MREC22ResistanceFault";

/// Raised by the software over voltage watchdog
/// (`ErrorHandling::raise_over_voltage_error`). One type for both limits; the
/// severity is what separates the emergency shutdown from the error shutdown.
pub const MREC5_OVER_VOLTAGE: &str = "evse_manager/MREC5OverVoltage";

/// Raised when the instruments measuring the DC voltage disagree by more than
/// the configured spread for the configured duration
/// (`ErrorHandling::raise_voltage_plausibility_fault`). `Severity::High` there,
/// because a measurement the instruments cannot agree on is not a measurement.
pub const VOLTAGE_PLAUSIBILITY: &str = "evse_manager/VoltagePlausibilityFault";

/// The description the C++ attaches to it (`Charger.cpp:1671`).
pub const MREC9_DESCRIPTION: &str = "No authorization was provided within timeout.";

/// The vendor id reported when no cause supplies one, or when the
/// `inoperative_error_use_vendor_id` setting is off.
pub const DEFAULT_VENDOR_ID: &str = "EVerest";

/// The ignore table, hung off the inbound `ErrorSource` rather than off a second
/// enum that mirrors it. A duplicate source enum meant every inbound error had
/// to be translated, and the three sources the inbound enum was missing could
/// not be translated at all.
impl ErrorSource {
    /// Error types that this source reports but that do not block charging.
    ///
    /// Ported from the `IgnoreErrors` table at `ErrorHandling.cpp:36-49`. The
    /// entries are full error types, so an entry only ever matches the source it
    /// is listed under. `Inoperative` is listed for `Evse` because it is the
    /// result of this very check and must not become a cause of itself.
    fn ignored(&self) -> &'static [&'static str] {
        match self {
            ErrorSource::Evse => &[INOPERATIVE],
            ErrorSource::Bsp => &[
                "evse_board_support/MREC3HighTemperature",
                "evse_board_support/MREC18CableOverTempDerate",
                "evse_board_support/VendorWarning",
            ],
            ErrorSource::ConnectorLock => &["connector_lock/VendorWarning"],
            ErrorSource::AcRcd => &["ac_rcd/VendorWarning"],
            ErrorSource::IsolationMonitor => &["isolation_monitor/VendorWarning"],
            ErrorSource::PowerSupplyDc => &["power_supply_DC/VendorWarning"],
            ErrorSource::Powermeter => &["generic/VendorWarning"],
            ErrorSource::Slac => &["generic/VendorWarning"],
            ErrorSource::Hlc => &["generic/VendorWarning"],
            ErrorSource::OverVoltageMonitor => &["over_voltage_monitor/VendorWarning"],
        }
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Cause {
    pub source: ErrorSource,
    pub error_type: String,
    pub sub_type: String,
    pub description: String,
    pub vendor_id: String,
    pub severity: Severity,
}

impl Cause {
    fn blocks_charging(&self) -> bool {
        !self
            .source
            .ignored()
            .iter()
            .any(|ignored| *ignored == self.error_type)
    }

    fn key(&self) -> (String, String) {
        (self.error_type.clone(), self.sub_type.clone())
    }

    /// The human readable form of one cause, ported from `generate_description`
    /// at `ErrorHandling.cpp:7-30`: the error type with its interface prefix
    /// removed, with the sub type appended. A type carrying no prefix has no
    /// meaningful short form, so its own description is used instead.
    fn short_description(&self) -> String {
        let Some(slash) = self.error_type.find('/') else {
            return self.description.clone();
        };

        let mut result = if slash + 1 == self.error_type.len() {
            self.error_type[..slash].to_string()
        } else {
            self.error_type[slash + 1..]
                .trim_end_matches('/')
                .to_string()
        };
        if !self.sub_type.is_empty() {
            result.push('/');
            result.push_str(&self.sub_type);
        }
        result
    }
}

/// What a fault set change asks the rest of the module to do.
///
/// `RaiseInoperative` always means the `evse_manager/Inoperative` error with an
/// empty sub type and high severity; only its description, message and vendor id
/// vary.
/// A changed cause set arrives as `ClearInoperative` then `RaiseInoperative`,
/// because the framework has no in place update. That pair deliberately carries
/// no `AllErrorsPreventingChargingCleared`, which would let the charger restart
/// while the fault is still present.
#[derive(Clone, Debug, PartialEq)]
pub enum FaultSignal {
    ClearInoperative,
    RaiseInoperative {
        description: String,
        /// The primary cause's fully qualified type, which is what
        /// `raise_inoperative_error` puts in the error's `message`. It keeps the
        /// interface prefix that `short_description` strips, so it is not
        /// derivable from `description` and travels on its own.
        message: String,
        vendor_id: String,
    },
    ForceErrorShutdown,
    ForceEmergencyShutdown,
    AllErrorsPreventingChargingCleared,
    AllErrorsCleared,
}

#[derive(Debug, Default)]
pub struct Faults {
    /// Every active cause, blocking or not, in arrival order. Absent optional
    /// requirements need no handling: a source that is not wired never reports,
    /// so it contributes nothing. This replaces the `if (r_X.size() > 0)` guards
    /// at `ErrorHandling.cpp:79-125` and `:209-235`.
    active: Vec<Cause>,
    /// The cause set behind the `Inoperative` error currently raised, keyed on
    /// type and sub type so that change detection is independent of the order in
    /// which the causes were detected.
    raised_causes: BTreeSet<(String, String)>,
    raised_emergency: bool,
    inoperative_raised: bool,
    use_vendor_id: bool,
}

impl Faults {
    pub fn new(inoperative_error_use_vendor_id: bool) -> Self {
        Self {
            use_vendor_id: inoperative_error_use_vendor_id,
            ..Default::default()
        }
    }

    /// Record a fault as active, replacing any earlier report of the same
    /// source, type and sub type in place.
    pub fn raise(&mut self, cause: Cause) -> Vec<FaultSignal> {
        match self.position_of(cause.source, &cause.error_type, &cause.sub_type) {
            Some(at) => self.active[at] = cause,
            None => self.active.push(cause),
        }
        self.process()
    }

    /// Record a fault as no longer active. Clearing something that was never
    /// active is not an error, it just changes nothing.
    pub fn clear(
        &mut self,
        source: ErrorSource,
        error_type: &str,
        sub_type: &str,
    ) -> Vec<FaultSignal> {
        match self.position_of(source, error_type, sub_type) {
            Some(at) => {
                self.active.remove(at);
                self.process()
            }
            None => Vec::new(),
        }
    }

    /// Clear every cause this module raised on its own interface, and re-assert
    /// the shutdown class if anything still blocks charging.
    ///
    /// Ported from `Charger::clear_errors_on_unplug` (`Charger.cpp:2285-2296`).
    /// The C++ names its nine clears one by one, so a tenth raise added later
    /// latches until the process restarts. Here the cause set is the list. A
    /// cause carries `ErrorSource::Evse` only when this module raised it, and
    /// `Core::raise_own_error` is the only writer of that source, so a raise
    /// added later is cleared by being raisable rather than by a ninth line
    /// here. Peer causes are left alone: the peer still holds the fault and
    /// clears it itself.
    ///
    /// Returns the causes that were cleared, so the caller can clear each on the
    /// interface it raised them on, and the signals the shrunken set produced.
    pub fn clear_own(&mut self) -> (Vec<Cause>, Vec<FaultSignal>) {
        let mut cleared = Vec::new();
        let mut kept = Vec::new();
        for cause in std::mem::take(&mut self.active) {
            if cause.source == ErrorSource::Evse {
                cleared.push(cause);
            } else {
                kept.push(cause);
            }
        }
        self.active = kept;

        // Every `clear_*` in `ErrorHandling.cpp` guards `process_error` on the
        // error actually being active (`ErrorHandling.cpp:346-350`), so an
        // unplug with nothing of ours raised re-examines nothing.
        let mut signals = if cleared.is_empty() {
            Vec::new()
        } else {
            self.process()
        };

        // `Charger.cpp:2296` resets `last_shutdown_type`, the edge the C++ fires
        // its fatal error actuation on. The unplug drives the port back to idle,
        // which undoes that actuation, so a fault outliving the unplug has to
        // re-assert it. Here the edge is the raise bookkeeping above, and
        // re-asserting is re-emitting the class the current causes carry.
        let already = signals.iter().any(|signal| {
            matches!(
                signal,
                FaultSignal::ForceErrorShutdown | FaultSignal::ForceEmergencyShutdown
            )
        });
        if self.inoperative_raised && !already {
            signals.push(if self.raised_emergency {
                FaultSignal::ForceEmergencyShutdown
            } else {
                FaultSignal::ForceErrorShutdown
            });
        }
        (cleared, signals)
    }

    /// Route an inbound error event to `raise` or `clear`.
    ///
    /// Raise and clear are the same event variant carrying a flag, so no path can
    /// deliver one while dropping the other.
    pub fn apply_error_event(&mut self, event: &ErrorEvent) -> Vec<FaultSignal> {
        if event.raised {
            self.raise(Cause {
                source: event.source,
                error_type: event.error_type.clone(),
                sub_type: event.sub_type.clone(),
                description: String::new(),
                vendor_id: event.vendor_id.clone(),
                severity: event.severity,
            })
        } else {
            self.clear(event.source, &event.error_type, &event.sub_type)
        }
    }

    pub fn prevents_charging(&self) -> bool {
        self.active.iter().any(Cause::blocks_charging)
    }

    /// The active causes that block charging, in source order and then in
    /// arrival order within a source. The first is the primary cause, which
    /// supplies the vendor id.
    pub fn inoperative_causes(&self) -> Vec<&Cause> {
        let mut causes: Vec<&Cause> = self
            .active
            .iter()
            .filter(|cause| cause.blocks_charging())
            .collect();
        causes.sort_by_key(|cause| cause.source);
        causes
    }

    fn position_of(&self, source: ErrorSource, error_type: &str, sub_type: &str) -> Option<usize> {
        self.active.iter().position(|cause| {
            cause.source == source && cause.error_type == error_type && cause.sub_type == sub_type
        })
    }

    /// Decide whether the current fault set blocks charging, then report whether
    /// anything at all is left. Ported from `process_error` at
    /// `ErrorHandling.cpp:160-194`.
    fn process(&mut self) -> Vec<FaultSignal> {
        let blocking: Vec<Cause> = self.inoperative_causes().into_iter().cloned().collect();
        let mut signals = if blocking.is_empty() {
            self.clear_inoperative()
        } else {
            self.raise_inoperative(&blocking)
        };

        // The all errors cleared signal exists for OCPP 1.6. It fires when
        // nothing is active any more, including faults that never blocked
        // charging. The raised Inoperative error counts as active itself, which
        // is what keeps this quiet across a cause set refresh.
        if self.active.is_empty() && !self.inoperative_raised {
            signals.push(FaultSignal::AllErrorsCleared);
        }
        signals
    }

    /// Ported from `raise_inoperative_error` at `ErrorHandling.cpp:241-309`.
    fn raise_inoperative(&mut self, causes: &[Cause]) -> Vec<FaultSignal> {
        let current: BTreeSet<(String, String)> = causes.iter().map(Cause::key).collect();
        if self.inoperative_raised && current == self.raised_causes {
            return Vec::new();
        }

        let was_raised = self.inoperative_raised;
        let was_emergency = self.raised_emergency;
        let mut signals = Vec::new();
        if was_raised {
            signals.push(FaultSignal::ClearInoperative);
        }

        // The primary cause supplies the vendor id and the message; the
        // description lists every blocking cause. Consumers with shorter limits,
        // such as an OCPP 1.6 StatusNotification, truncate it themselves.
        let primary = &causes[0];
        let description = causes
            .iter()
            .map(Cause::short_description)
            .collect::<Vec<String>>()
            .join(", ");
        let vendor_id = if self.use_vendor_id && !primary.vendor_id.is_empty() {
            primary.vendor_id.clone()
        } else {
            DEFAULT_VENDOR_ID.to_string()
        };
        signals.push(FaultSignal::RaiseInoperative {
            description,
            message: primary.error_type.clone(),
            vendor_id,
        });

        let emergency = causes.iter().any(|cause| cause.severity == Severity::High);
        self.raised_causes = current;
        self.raised_emergency = emergency;
        self.inoperative_raised = true;

        // Emergency shutdown when any blocking cause is high severity, error
        // shutdown otherwise. Signal only on a genuine transition, the first
        // raise or a change of shutdown class. A description refresh must not
        // re-signal: downstream that cancels an active reservation again on
        // every refresh, and the charger stays shut down without a repeat.
        if !was_raised || emergency != was_emergency {
            signals.push(if emergency {
                FaultSignal::ForceEmergencyShutdown
            } else {
                FaultSignal::ForceErrorShutdown
            });
        }
        signals
    }

    /// Ported from `clear_inoperative_error` at `ErrorHandling.cpp:312-319`.
    fn clear_inoperative(&mut self) -> Vec<FaultSignal> {
        if !self.inoperative_raised {
            return Vec::new();
        }
        self.inoperative_raised = false;
        self.raised_emergency = false;
        self.raised_causes.clear();
        vec![
            FaultSignal::ClearInoperative,
            FaultSignal::AllErrorsPreventingChargingCleared,
        ]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn cause(source: ErrorSource, error_type: &str, severity: Severity) -> Cause {
        Cause {
            source,
            error_type: error_type.into(),
            sub_type: String::new(),
            description: String::new(),
            vendor_id: String::new(),
            severity,
        }
    }

    fn blocking(source: ErrorSource, error_type: &str) -> Cause {
        cause(source, error_type, Severity::Medium)
    }

    fn faults() -> Faults {
        Faults::new(false)
    }

    fn raised(signals: &[FaultSignal]) -> Option<(&str, &str)> {
        signals.iter().find_map(|s| match s {
            FaultSignal::RaiseInoperative {
                description,
                vendor_id,
                ..
            } => Some((description.as_str(), vendor_id.as_str())),
            _ => None,
        })
    }

    /// The `message` of the raise, which no other accessor here exposes.
    fn raised_message(signals: &[FaultSignal]) -> Option<&str> {
        signals.iter().find_map(|s| match s {
            FaultSignal::RaiseInoperative { message, .. } => Some(message.as_str()),
            _ => None,
        })
    }

    #[test]
    fn a_blocking_error_raises_inoperative_and_forces_error_shutdown() {
        let mut f = faults();
        let signals = f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        assert!(f.prevents_charging());
        assert!(raised(&signals).is_some(), "{signals:?}");
        assert!(
            signals.contains(&FaultSignal::ForceErrorShutdown),
            "{signals:?}"
        );
        assert!(!signals.contains(&FaultSignal::AllErrorsCleared));
    }

    #[test]
    fn a_high_severity_cause_forces_emergency_shutdown_instead() {
        let mut f = faults();
        let signals = f.raise(cause(
            ErrorSource::Bsp,
            "evse_board_support/MREC8EmergencyStop",
            Severity::High,
        ));
        assert!(
            signals.contains(&FaultSignal::ForceEmergencyShutdown),
            "{signals:?}"
        );
        assert!(!signals.contains(&FaultSignal::ForceErrorShutdown));
    }

    #[test]
    fn the_same_cause_set_arriving_in_a_different_order_is_not_a_change() {
        // Two faults, then the same two re-reported in the opposite order. The
        // cause set is keyed on type and sub type, so nothing is re-raised.
        let mut a = faults();
        a.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        a.raise(blocking(ErrorSource::AcRcd, "ac_rcd/AC"));

        let repeat_first = a.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        assert!(repeat_first.is_empty(), "{repeat_first:?}");
        let repeat_second = a.raise(blocking(ErrorSource::AcRcd, "ac_rcd/AC"));
        assert!(repeat_second.is_empty(), "{repeat_second:?}");
    }

    #[test]
    fn a_changed_cause_set_reraises_inoperative_without_a_cleared_signal() {
        // The framework has no in place update, so a new cause clears and
        // re-raises. Emitting AllErrorsPreventingChargingCleared here would let
        // the charger restart mid fault.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let signals = f.raise(blocking(ErrorSource::AcRcd, "ac_rcd/AC"));

        assert_eq!(
            signals.first(),
            Some(&FaultSignal::ClearInoperative),
            "{signals:?}"
        );
        assert!(raised(&signals).is_some(), "{signals:?}");
        assert!(!signals.contains(&FaultSignal::AllErrorsPreventingChargingCleared));
    }

    #[test]
    fn a_pure_description_refresh_does_not_resignal_shutdown() {
        // Same severity class, different cause set. Re-signalling here cancels an
        // active reservation again on every refresh.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let signals = f.raise(blocking(ErrorSource::AcRcd, "ac_rcd/AC"));
        assert!(
            !signals.contains(&FaultSignal::ForceErrorShutdown),
            "{signals:?}"
        );
        assert!(
            !signals.contains(&FaultSignal::ForceEmergencyShutdown),
            "{signals:?}"
        );
    }

    #[test]
    fn crossing_from_error_to_emergency_severity_resignals() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let signals = f.raise(cause(
            ErrorSource::Bsp,
            "evse_board_support/MREC8EmergencyStop",
            Severity::High,
        ));
        assert!(
            signals.contains(&FaultSignal::ForceEmergencyShutdown),
            "{signals:?}"
        );
    }

    #[test]
    fn each_source_ignores_its_own_non_blocking_errors() {
        for (source, error_type) in [
            (ErrorSource::Evse, "evse_manager/Inoperative"),
            (ErrorSource::Bsp, "evse_board_support/MREC3HighTemperature"),
            (
                ErrorSource::Bsp,
                "evse_board_support/MREC18CableOverTempDerate",
            ),
            (ErrorSource::Bsp, "evse_board_support/VendorWarning"),
            (ErrorSource::ConnectorLock, "connector_lock/VendorWarning"),
            (ErrorSource::AcRcd, "ac_rcd/VendorWarning"),
            (
                ErrorSource::IsolationMonitor,
                "isolation_monitor/VendorWarning",
            ),
            (ErrorSource::PowerSupplyDc, "power_supply_DC/VendorWarning"),
            (ErrorSource::Powermeter, "generic/VendorWarning"),
            (ErrorSource::Slac, "generic/VendorWarning"),
            (ErrorSource::Hlc, "generic/VendorWarning"),
            (
                ErrorSource::OverVoltageMonitor,
                "over_voltage_monitor/VendorWarning",
            ),
        ] {
            let mut f = faults();
            let signals = f.raise(blocking(source, error_type));
            assert!(
                !f.prevents_charging(),
                "{source:?} {error_type} must not block charging"
            );
            assert!(
                raised(&signals).is_none(),
                "{source:?} {error_type}: {signals:?}"
            );
        }
    }

    #[test]
    fn an_ignore_entry_applies_only_to_its_own_source() {
        // MREC3HighTemperature is non blocking on the board support driver only.
        let mut f = faults();
        f.raise(blocking(
            ErrorSource::Powermeter,
            "evse_board_support/MREC3HighTemperature",
        ));
        assert!(f.prevents_charging());
    }

    #[test]
    fn the_vendor_id_falls_back_to_everest_when_the_setting_is_off() {
        let mut f = Faults::new(false);
        let mut c = blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault");
        c.vendor_id = "acme".into();
        let signals = f.raise(c);
        assert_eq!(
            raised(&signals).map(|(_, v)| v),
            Some("EVerest"),
            "{signals:?}"
        );
    }

    #[test]
    fn the_primary_cause_vendor_id_is_used_when_the_setting_is_on() {
        let mut f = Faults::new(true);
        let mut c = blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault");
        c.vendor_id = "acme".into();
        let signals = f.raise(c);
        assert_eq!(
            raised(&signals).map(|(_, v)| v),
            Some("acme"),
            "{signals:?}"
        );
    }

    #[test]
    fn an_empty_primary_vendor_id_falls_back_to_everest() {
        let mut f = Faults::new(true);
        let signals = f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        assert_eq!(
            raised(&signals).map(|(_, v)| v),
            Some("EVerest"),
            "{signals:?}"
        );
    }

    #[test]
    fn the_inoperative_message_is_the_primary_cause_fully_qualified_type() {
        // The message and the description are two different renderings of the
        // same cause and an OCPP 1.6 StatusNotification puts them in different
        // fields, so the message keeps the interface prefix that
        // `short_description` strips. Filling it from the description would
        // send `MREC2GroundFailure` where the C++ sends
        // `evse_board_support/MREC2GroundFailure`.
        let mut f = faults();
        let signals = f.raise(blocking(
            ErrorSource::Bsp,
            "evse_board_support/MREC2GroundFailure",
        ));
        assert_eq!(
            raised_message(&signals),
            Some("evse_board_support/MREC2GroundFailure"),
            "{signals:?}"
        );
        assert_eq!(
            raised(&signals).map(|(d, _)| d),
            Some("MREC2GroundFailure"),
            "the description still strips the prefix: {signals:?}"
        );
    }

    #[test]
    fn the_message_names_the_primary_cause_alone_while_the_description_lists_all() {
        // Two causes, so a message built from the joined description would be
        // visibly wrong rather than accidentally equal.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let signals = f.raise(blocking(ErrorSource::AcRcd, "ac_rcd/AC"));
        assert_eq!(
            raised_message(&signals),
            Some("evse_board_support/DiodeFault"),
            "{signals:?}"
        );
        assert_eq!(
            raised(&signals).map(|(d, _)| d),
            Some("DiodeFault, AC"),
            "{signals:?}"
        );
    }

    #[test]
    fn the_message_carries_no_sub_type_while_the_description_does() {
        // `raise_inoperative_error` passes `primary.type`, which is the type
        // alone; the sub type reaches the wire through the description, which
        // is what an OCPP 1.6 `vendorErrorCode` is rendered from.
        let mut f = faults();
        let mut cause = blocking(ErrorSource::Bsp, "evse_board_support/VendorError");
        cause.sub_type = "some_subtype".into();
        let signals = f.raise(cause);
        assert_eq!(
            raised_message(&signals),
            Some("evse_board_support/VendorError"),
            "{signals:?}"
        );
        assert_eq!(
            raised(&signals).map(|(d, _)| d),
            Some("VendorError/some_subtype"),
            "{signals:?}"
        );
    }

    #[test]
    fn a_changed_cause_set_re_raises_with_the_new_primary_message() {
        // The refresh pair is clear then raise, and the raise is rebuilt from
        // the current primary rather than reusing what was raised before.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let cleared = f.clear(ErrorSource::Bsp, "evse_board_support/DiodeFault", "");
        assert_eq!(raised_message(&cleared), None, "{cleared:?}");
        let signals = f.raise(blocking(
            ErrorSource::ConnectorLock,
            "connector_lock/ConnectorLockUnexpectedClose",
        ));
        assert_eq!(
            raised_message(&signals),
            Some("connector_lock/ConnectorLockUnexpectedClose"),
            "{signals:?}"
        );
    }

    #[test]
    fn the_inoperative_description_lists_every_active_cause() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let mut second = blocking(ErrorSource::AcRcd, "ac_rcd/AC");
        second.sub_type = "DC".into();
        let signals = f.raise(second);
        assert_eq!(
            raised(&signals).map(|(d, _)| d),
            Some("DiodeFault, AC/DC"),
            "{signals:?}"
        );
    }

    #[test]
    fn a_type_without_a_slash_falls_back_to_the_error_description() {
        let mut f = faults();
        let mut c = blocking(ErrorSource::Bsp, "DiodeFault");
        c.description = "the diode is gone".into();
        let signals = f.raise(c);
        assert_eq!(
            raised(&signals).map(|(d, _)| d),
            Some("the diode is gone"),
            "{signals:?}"
        );
    }

    #[test]
    fn clearing_the_last_blocking_cause_clears_inoperative_and_signals_cleared() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let signals = f.clear(ErrorSource::Bsp, "evse_board_support/DiodeFault", "");
        assert!(!f.prevents_charging());
        assert!(
            signals.contains(&FaultSignal::ClearInoperative),
            "{signals:?}"
        );
        assert!(
            signals.contains(&FaultSignal::AllErrorsPreventingChargingCleared),
            "{signals:?}"
        );
        assert!(
            signals.contains(&FaultSignal::AllErrorsCleared),
            "{signals:?}"
        );
    }

    #[test]
    fn a_non_blocking_error_still_holds_back_the_all_errors_cleared_signal() {
        let mut f = faults();
        let signals = f.raise(blocking(
            ErrorSource::Bsp,
            "evse_board_support/VendorWarning",
        ));
        assert!(
            !signals.contains(&FaultSignal::AllErrorsCleared),
            "{signals:?}"
        );

        let cleared = f.clear(ErrorSource::Bsp, "evse_board_support/VendorWarning", "");
        assert!(
            cleared.contains(&FaultSignal::AllErrorsCleared),
            "{cleared:?}"
        );
    }

    #[test]
    fn the_all_errors_cleared_signal_waits_for_the_over_voltage_monitor() {
        // Intentional divergence 1. ErrorHandling.cpp:185-189 omits
        // r_over_voltage_monitor from the active error count, so a still active
        // over voltage monitor warning cannot hold the signal back. The warning
        // has to be non blocking for the omission to be observable: a blocking
        // one keeps Inoperative active, which the count does include.
        let mut f = faults();
        f.raise(blocking(
            ErrorSource::IsolationMonitor,
            "isolation_monitor/DeviceFault",
        ));
        f.raise(blocking(
            ErrorSource::OverVoltageMonitor,
            "over_voltage_monitor/VendorWarning",
        ));

        let after_imd = f.clear(
            ErrorSource::IsolationMonitor,
            "isolation_monitor/DeviceFault",
            "",
        );
        assert!(
            !after_imd.contains(&FaultSignal::AllErrorsCleared),
            "the over voltage monitor warning is still active: {after_imd:?}"
        );

        let after_ovm = f.clear(
            ErrorSource::OverVoltageMonitor,
            "over_voltage_monitor/VendorWarning",
            "",
        );
        assert!(
            after_ovm.contains(&FaultSignal::AllErrorsCleared),
            "{after_ovm:?}"
        );
    }

    #[test]
    fn absent_optional_requirements_contribute_no_causes() {
        // Sources are never registered, so a wiring without an RCD, an isolation
        // monitor or high level communication simply never reports from them.
        let f = faults();
        assert!(!f.prevents_charging());
        assert!(f.inoperative_causes().is_empty());
    }

    #[test]
    fn clearing_an_unknown_cause_signals_nothing() {
        let mut f = faults();
        assert!(f
            .clear(ErrorSource::Bsp, "evse_board_support/DiodeFault", "")
            .is_empty());
    }

    #[test]
    fn an_error_event_routes_to_raise_or_clear_by_its_raised_flag() {
        let mut f = faults();
        let event = |raised| ErrorEvent {
            source: ErrorSource::Bsp,
            error_type: "evse_board_support/DiodeFault".into(),
            sub_type: String::new(),
            vendor_id: String::new(),
            severity: Severity::Medium,
            raised,
        };

        f.apply_error_event(&event(true));
        assert!(f.prevents_charging());
        f.apply_error_event(&event(false));
        assert!(!f.prevents_charging());
    }

    #[test]
    fn causes_are_reported_in_source_order() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Hlc, "generic/CommunicationFault"));
        f.raise(blocking(ErrorSource::Bsp, "evse_board_support/DiodeFault"));
        let types: Vec<&str> = f
            .inoperative_causes()
            .iter()
            .map(|c| c.error_type.as_str())
            .collect();
        assert_eq!(
            types,
            vec![
                "evse_board_support/DiodeFault",
                "generic/CommunicationFault"
            ]
        );
    }

    #[test]
    fn clearing_own_causes_leaves_every_peer_cause_in_place() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Evse, "evse_manager/Internal"));
        f.raise(blocking(
            ErrorSource::IsolationMonitor,
            "isolation_monitor/DeviceFault",
        ));

        let (cleared, _) = f.clear_own();

        let types: Vec<&str> = cleared.iter().map(|c| c.error_type.as_str()).collect();
        assert_eq!(types, vec!["evse_manager/Internal"]);
        assert!(
            f.prevents_charging(),
            "the isolation monitor still holds its fault"
        );
        let remaining: Vec<&str> = f
            .inoperative_causes()
            .iter()
            .map(|c| c.error_type.as_str())
            .collect();
        assert_eq!(remaining, vec!["isolation_monitor/DeviceFault"]);
    }

    #[test]
    fn a_cause_raised_under_a_sub_type_is_cleared_under_that_sub_type() {
        // The module raises MREC22ResistanceFault twice, once per sub type. A
        // clear that dropped the sub type could not tell the two apart.
        let mut f = faults();
        let mut resistance = blocking(ErrorSource::Evse, "evse_manager/MREC22ResistanceFault");
        resistance.sub_type = "Resistance".into();
        let mut earth = resistance.clone();
        earth.sub_type = "VoltageToEarth".into();
        f.raise(resistance);
        f.raise(earth);

        let (cleared, _) = f.clear_own();

        let sub_types: Vec<&str> = cleared.iter().map(|c| c.sub_type.as_str()).collect();
        assert_eq!(sub_types, vec!["Resistance", "VoltageToEarth"]);
    }

    #[test]
    fn clearing_the_last_own_cause_clears_inoperative_and_asserts_no_shutdown() {
        let mut f = faults();
        f.raise(blocking(ErrorSource::Evse, "evse_manager/Internal"));

        let (cleared, signals) = f.clear_own();

        assert_eq!(cleared.len(), 1);
        assert!(!f.prevents_charging());
        assert!(
            signals.contains(&FaultSignal::ClearInoperative),
            "{signals:?}"
        );
        assert!(
            !signals.contains(&FaultSignal::ForceErrorShutdown)
                && !signals.contains(&FaultSignal::ForceEmergencyShutdown),
            "nothing blocks charging any more, so nothing is re-asserted: {signals:?}"
        );
    }

    #[test]
    fn a_surviving_cause_reasserts_its_own_shutdown_class() {
        // The unplug puts the port back in service, so the class the surviving
        // causes carry has to be asserted again. High severity survives as an
        // emergency, not as an ordinary error shutdown.
        for (severity, expected) in [
            (Severity::Medium, FaultSignal::ForceErrorShutdown),
            (Severity::High, FaultSignal::ForceEmergencyShutdown),
        ] {
            let mut f = faults();
            f.raise(cause(
                ErrorSource::IsolationMonitor,
                "isolation_monitor/DeviceFault",
                severity,
            ));

            let (cleared, signals) = f.clear_own();

            assert!(cleared.is_empty(), "{cleared:?}");
            assert!(signals.contains(&expected), "{severity:?}: {signals:?}");
        }
    }

    #[test]
    fn a_surviving_cause_is_asserted_once_even_when_the_cause_set_shrank() {
        // Dropping the own cause re-raises Inoperative over a smaller set, which
        // is itself a change. The re-assert must not double up on it.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Evse, "evse_manager/Internal"));
        f.raise(cause(
            ErrorSource::IsolationMonitor,
            "isolation_monitor/DeviceFault",
            Severity::High,
        ));

        let (_, signals) = f.clear_own();

        let asserts = signals
            .iter()
            .filter(|signal| {
                matches!(
                    signal,
                    FaultSignal::ForceErrorShutdown | FaultSignal::ForceEmergencyShutdown
                )
            })
            .count();
        assert_eq!(asserts, 1, "{signals:?}");
    }

    #[test]
    fn a_shutdown_class_that_changes_on_the_unplug_is_asserted_once() {
        // Dropping the own high severity cause leaves a medium peer cause, so
        // the set re-examination emits the new class by itself. The re-assert
        // must notice and not stack a second one on top.
        let mut f = faults();
        f.raise(cause(
            ErrorSource::Evse,
            "evse_manager/Internal",
            Severity::High,
        ));
        f.raise(cause(
            ErrorSource::IsolationMonitor,
            "isolation_monitor/DeviceFault",
            Severity::Medium,
        ));

        let (_, signals) = f.clear_own();

        let asserts: Vec<&FaultSignal> = signals
            .iter()
            .filter(|signal| {
                matches!(
                    signal,
                    FaultSignal::ForceErrorShutdown | FaultSignal::ForceEmergencyShutdown
                )
            })
            .collect();
        assert_eq!(
            asserts,
            vec![&FaultSignal::ForceErrorShutdown],
            "{signals:?}"
        );
    }

    #[test]
    fn clearing_own_causes_when_there_are_none_signals_nothing() {
        let mut f = faults();
        let (cleared, signals) = f.clear_own();
        assert!(cleared.is_empty());
        assert!(signals.is_empty(), "{signals:?}");
    }

    #[test]
    fn a_non_blocking_own_cause_is_cleared_too() {
        // Nothing about the clear route is conditional on blocking charging; the
        // C++ clears its own errors off the interface either way.
        let mut f = faults();
        f.raise(blocking(ErrorSource::Evse, INOPERATIVE));

        let (cleared, _) = f.clear_own();

        assert_eq!(cleared.len(), 1, "{cleared:?}");
    }
}
