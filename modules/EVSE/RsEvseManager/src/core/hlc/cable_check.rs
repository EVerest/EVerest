// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The cable check port: what the vehicle is told about isolation.
//!
//! Two facts travel from the cable check sequence to the ISO 15118 stack, and
//! they are separate commands on separate wires: the isolation status
//! (`call_update_isolation_status`) and the completion verdict
//! (`call_cable_check_finished`). This module owns the first one's derivation
//! and names both, so a reader arrives at one place to see which combinations
//! the sequence can produce.
//!
//! The sequence itself lives in `core::path::dc`, because it is a power path
//! stage machine and not a wire decision.

use crate::core::hlc::ConnectorKind;

/// Isolation resistance below this is a fault. `EvseManager.hpp:417`,
/// `CABLECHECK_INSULATION_FAULT_RESISTANCE_OHM`.
pub const INSULATION_FAULT_RESISTANCE_OHM: f64 = 100_000.0;

/// The same threshold for an MCS connector. `EvseManager.hpp:418`,
/// `CABLECHECK_MCS_INSULATION_FAULT_RESISTANCE_OHM`. Higher, because the
/// connector carries more voltage.
pub const MCS_INSULATION_FAULT_RESISTANCE_OHM: f64 = 125_000.0;

/// `types::iso15118::IsolationStatus`, narrowed to the three values this module
/// originates.
///
/// The wire type declares five. `Invalid` and `Warning` are absent because no
/// site in `modules/EVSE/EvseManager/` sends either: the 2023 edition of
/// IEC 61851-23 removed the warning level the 2014 edition had, which
/// `EvseManager.cpp:2246-2249` records in a comment, and `Invalid` has no
/// producer at all. Carrying them here would add two values nothing can build.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IsolationStatus {
    /// A measured resistance in range (`EvseManager.cpp:2015`), or the self
    /// test alone standing in for a measurement (`:2260`).
    Valid,
    /// A measured resistance below the connector's threshold (`:2011`).
    Fault,
    /// No isolation monitor is wired, so isolation was never checked (`:2025`).
    NoImd,
}

/// `EvseManager::check_isolation_resistance_in_range` (`EvseManager.cpp:2003-2018`).
///
/// The C++ returns a `bool` and emits the status as a side effect. Here the
/// status is the return value and the caller derives the boolean from it, so
/// the two cannot disagree: there is no way to report a fault status and
/// continue, or to pass the range check while telling the vehicle `Fault`.
///
/// The comparison is strict, so a resistance exactly at the threshold is in
/// range. That is the C++ comparison (`resistance < insulation_fault_resistance_ohm`).
pub fn isolation_verdict(resistance_ohm: f64, connector: ConnectorKind) -> IsolationStatus {
    let threshold_ohm = if connector.is_mcs() {
        MCS_INSULATION_FAULT_RESISTANCE_OHM
    } else {
        INSULATION_FAULT_RESISTANCE_OHM
    };
    if resistance_ohm < threshold_ohm {
        IsolationStatus::Fault
    } else {
        IsolationStatus::Valid
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn resistance_below_the_threshold_is_a_fault() {
        assert_eq!(
            isolation_verdict(99_999.0, ConnectorKind::Other),
            IsolationStatus::Fault
        );
    }

    #[test]
    fn resistance_at_or_above_the_threshold_is_valid() {
        assert_eq!(
            isolation_verdict(100_000.0, ConnectorKind::Other),
            IsolationStatus::Valid
        );
    }

    #[test]
    fn an_mcs_connector_raises_the_threshold() {
        assert_eq!(
            isolation_verdict(110_000.0, ConnectorKind::Other),
            IsolationStatus::Valid
        );
        assert_eq!(
            isolation_verdict(110_000.0, ConnectorKind::Mcs),
            IsolationStatus::Fault
        );
    }
}
