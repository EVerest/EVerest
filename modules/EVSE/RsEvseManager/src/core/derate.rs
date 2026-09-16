// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! External derating of the DC power supply capability report.
//!
//! Something outside this module narrows what the supply may deliver, through
//! the `dc_external_derate` interface this module provides. The handler is one
//! line (`dc_external_derate/dc_external_derateImpl.cpp:15-17`) and forwards to
//! `EvseManager::set_external_derating` (`EvseManager.cpp:2701-2704`), which
//! stores the request and does nothing else.
//!
//! The C++ applies it on every **read** of `get_powersupply_capabilities()`
//! (`:2679-2699`), never on write, and three consequences follow that this port
//! keeps deliberately:
//!
//! 1. The capability report forwarded to the ISO 15118 stack is the **raw** one
//!    the supply sent. `update_powersupply_capabilities` compares and forwards
//!    its argument (`EvseManager.hpp:265-272`), and the only recurring caller
//!    passes the supply's own report straight through (`EvseManager.cpp:218`).
//!    So a derate never reaches `call_set_powersupply_capabilities`.
//! 2. A derate arriving therefore emits nothing at all. It changes what later
//!    readers compute and is otherwise silent, with no log line and no error.
//! 3. Because the request is stored untouched and derived per read, a derate
//!    that is later relaxed restores the full capability. Nothing is clamped
//!    into the stored report and lost.
//!
//! The port is a push model where the C++ is a pull model, so the read-time
//! derivation becomes a stored `Derate` that the capability consumers apply as
//! they read. See `docs/architecture.md` for the one divergence that follows.

use crate::core::event::PowerSupplyCapabilities;

/// `types::dc_external_derate::ExternalDerating`
/// (`types/dc_external_derate.yaml`).
///
/// Every field is optional on the wire and optional here, with no `required`
/// list on the type at all. An absent field is the external source declining to
/// cap that quantity, which is not the same as capping it at zero: a derate
/// naming only a power limit must leave the current limit alone.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct ExternalDerating {
    pub max_export_current_a: Option<f64>,
    pub max_import_current_a: Option<f64>,
    pub max_export_power_w: Option<f64>,
    pub max_import_power_w: Option<f64>,
}

/// `min_optional(std::optional<float> const&, std::optional<float> const&)`
/// (`EvseManager.cpp:56-67`).
///
/// Absent is not a cap, so the present value wins outright and two absent stay
/// absent. The asymmetry that matters is in the second arm: when the capability
/// is absent and the derate is not, the derate's value is **adopted**, so an
/// external source can introduce a limit the supply never named. That is the
/// C++ behavior; see `Derate::apply`.
fn min_of(a: Option<f64>, b: Option<f64>) -> Option<f64> {
    match (a, b) {
        // Written as an explicit comparison rather than `f64::min`, which
        // differs on NaN: `f64::min` returns the non-NaN operand where the C++
        // ternary is false for any NaN comparison and yields `a`.
        (Some(a), Some(b)) => Some(if b < a { b } else { a }),
        (Some(a), None) => Some(a),
        (None, b) => b,
    }
}

/// `min_optional(float, std::optional<float>)` (`EvseManager.cpp:69-76`).
///
/// The overload the required half of the capability report reaches. A value
/// that is always present can only be lowered, never cleared, so an absent
/// derate leaves it exactly as it was.
///
/// It agrees with `min_of` on every input reachable here, because the two
/// differ only in what they do with an **absent capability** and a required
/// field has none. So swapping this for `min_of` on the export pair is an
/// equivalent mutation that no test can catch; only the import pair, where the
/// capability really can be absent, distinguishes them. Both are kept because
/// both exist in the C++, and which one a field reaches is what tells a reader
/// that the two directions do not behave alike.
fn min_with(a: f64, b: Option<f64>) -> f64 {
    match b {
        Some(b) if b < a => b,
        _ => a,
    }
}

/// The stored derate request and the one measurement its derivation needs.
///
/// `present_voltage_v` is `ev_info.present_voltage`, which the C++ reads at
/// `EvseManager.cpp:2689`. It is written from the supply's own measurement
/// (`:722`, via the clamp at `:701`), so it is never negative once
/// measurements flow and is absent only before the first one.
///
/// The C++ reads that member without holding `ev_info_mutex`, while every other
/// reader in the file takes it. That is a data race in the C++ and not a
/// behavior to reproduce; here the value arrives on the reducer's own thread and
/// the question does not exist.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Derate {
    requested: ExternalDerating,
    present_voltage_v: Option<f64>,
}

impl Derate {
    /// `EvseManager::set_external_derating` (`EvseManager.cpp:2701-2704`).
    ///
    /// A wholesale replacement, not a merge: the C++ assigns the whole struct,
    /// so a request that omits a field **clears** any cap that field carried
    /// before. An external source relaxes a limit by sending a request without
    /// it.
    /// Reports whether the request actually moved, which is the gate
    /// `EvseManager::set_external_derating` opens with (`:2829-2833`): an
    /// identical request returns before it touches anything.
    pub fn note_request(&mut self, requested: ExternalDerating) -> bool {
        let changed = self.requested != requested;
        self.requested = requested;
        changed
    }

    /// The present output voltage, already clamped to zero the way `:701`
    /// clamps it before `:722` stores it.
    ///
    /// The clamp is **unobservable** and kept for the C++ shape alone: a
    /// negative reading stored unclamped would reach the `voltage <= 0.0` guard
    /// in `effective` and derive nothing, which is exactly what a stored zero
    /// does. A hand mutation deleting it survives the whole suite, and that is
    /// a fact about the guard below rather than a gap above it. It stays
    /// because `present_voltage_v` is meant to hold what
    /// `ev_info.present_voltage` holds, and a reader who later wants that
    /// number should get the clamped one the C++ stored.
    pub fn note_present_voltage(&mut self, voltage_v: f64) {
        self.present_voltage_v = Some(if voltage_v > 0.0 { voltage_v } else { 0.0 });
    }

    /// `get_dc_external_derate` (`EvseManager.cpp:79-107`).
    ///
    /// A current cap and a power cap constrain the same supply, so given a
    /// voltage either one implies the other. Without a usable voltage neither
    /// can be derived and the request stands as it was sent.
    ///
    /// Both directions derive power from current first and current from power
    /// second, which is the C++ order. The order is not observable: each branch
    /// writes only a field the request left absent, so neither can consume what
    /// the other produced. It is kept because the two halves read as one pass.
    fn effective(&self) -> ExternalDerating {
        let mut d = self.requested;

        let Some(voltage) = self.present_voltage_v else {
            return d;
        };
        // A zero voltage would make the current derivation a division by zero
        // and the power derivation a uniform zero cap, so neither runs.
        if voltage <= 0.0 {
            return d;
        }

        if d.max_export_power_w.is_none() {
            if let Some(current_a) = d.max_export_current_a {
                d.max_export_power_w = Some(current_a * voltage);
            }
        }
        if d.max_import_power_w.is_none() {
            if let Some(current_a) = d.max_import_current_a {
                d.max_import_power_w = Some(current_a * voltage);
            }
        }

        if d.max_export_current_a.is_none() {
            if let Some(power_w) = d.max_export_power_w {
                d.max_export_current_a = Some(power_w / voltage);
            }
        }
        if d.max_import_current_a.is_none() {
            if let Some(power_w) = d.max_import_power_w {
                d.max_import_current_a = Some(power_w / voltage);
            }
        }

        d
    }

    /// The four narrowings of `get_powersupply_capabilities`
    /// (`EvseManager.cpp:2692-2696`).
    ///
    /// The export pair is required on the capability report and the import pair
    /// is optional (`types/power_supply_DC.yaml`), so the four C++ calls reach
    /// **two different `min_optional` overloads** and the two directions do not
    /// behave alike:
    ///
    /// - an export limit can only be lowered, because there is always one to
    ///   lower;
    /// - an import limit the supply never named is **introduced** by the
    ///   derate, because absent means unknown rather than zero and the present
    ///   operand wins.
    ///
    /// So an external derate can hand a supply an import ceiling it never
    /// claimed. That is what the C++ does and it is reported rather than
    /// corrected here, because a rewrite that clamped instead would silently
    /// drop the only import cap a bidirectional site had.
    ///
    /// `bidirectional` is not touched. Nothing in the C++ derate path reads or
    /// writes it, so a derate cannot take a supply's import capability away
    /// wholesale, only bound it.
    pub fn apply(&self, mut caps: PowerSupplyCapabilities) -> PowerSupplyCapabilities {
        let d = self.effective();
        caps.max_export_current_a = min_with(caps.max_export_current_a, d.max_export_current_a);
        caps.max_import_current_a = min_of(caps.max_import_current_a, d.max_import_current_a);
        caps.max_export_power_w = min_with(caps.max_export_power_w, d.max_export_power_w);
        caps.max_import_power_w = min_of(caps.max_import_power_w, d.max_import_power_w);
        caps
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn caps() -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional: true,
            max_export_current_a: 400.0,
            max_export_power_w: 300_000.0,
            max_import_current_a: Some(300.0),
            max_import_power_w: Some(250_000.0),
            ..PowerSupplyCapabilities::sane_default()
        }
    }

    #[test]
    fn a_port_with_no_derate_reports_what_the_supply_reported() {
        assert_eq!(Derate::default().apply(caps()), caps());
    }

    #[test]
    fn a_derate_lowers_a_capability_it_names() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        assert_eq!(derate.apply(caps()).max_export_current_a, 100.0);
    }

    #[test]
    fn a_derate_above_the_capability_leaves_it_alone() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(600.0),
            ..ExternalDerating::default()
        });
        assert_eq!(derate.apply(caps()).max_export_current_a, 400.0);
    }

    /// The asymmetry `apply` documents: absent plus present adopts the present
    /// one, so the derate becomes the import ceiling rather than being ignored.
    #[test]
    fn a_derate_introduces_an_import_limit_the_supply_never_named() {
        let unnamed = PowerSupplyCapabilities {
            max_import_current_a: None,
            max_import_power_w: None,
            ..caps()
        };
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_import_current_a: Some(50.0),
            ..ExternalDerating::default()
        });
        assert_eq!(derate.apply(unnamed).max_import_current_a, Some(50.0));
    }

    /// The counterpart on the required half, which has no absent case at all.
    #[test]
    fn an_export_limit_is_never_cleared_by_an_absent_derate() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_import_current_a: Some(10.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(0.0);
        assert_eq!(derate.apply(caps()).max_export_current_a, 400.0);
    }

    #[test]
    fn a_derate_without_a_voltage_derives_nothing() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        // No `note_present_voltage`, so the power half stays as the supply had
        // it rather than becoming the product of a voltage nobody measured.
        assert_eq!(derate.apply(caps()).max_export_power_w, 300_000.0);
    }

    #[test]
    fn a_zero_voltage_derives_nothing() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(0.0);
        assert_eq!(derate.apply(caps()).max_export_power_w, 300_000.0);
    }

    /// A negative measurement is clamped to zero on the way in, the way `:701`
    /// clamps it, so it reaches the same guard a zero does.
    #[test]
    fn a_negative_voltage_derives_nothing() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(-400.0);
        assert_eq!(derate.apply(caps()).max_export_power_w, 300_000.0);
    }

    #[test]
    fn a_current_cap_derives_the_power_cap_at_the_present_voltage() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        let derated = derate.apply(caps());
        assert_eq!(derated.max_export_current_a, 100.0);
        assert_eq!(derated.max_export_power_w, 40_000.0);
    }

    #[test]
    fn a_power_cap_derives_the_current_cap_at_the_present_voltage() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_power_w: Some(40_000.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        let derated = derate.apply(caps());
        assert_eq!(derated.max_export_current_a, 100.0);
        assert_eq!(derated.max_export_power_w, 40_000.0);
    }

    /// Both named means neither is derived, so a request that disagrees with
    /// itself is applied as sent rather than being made consistent.
    #[test]
    fn a_request_naming_both_has_neither_derived() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            max_export_power_w: Some(1_000.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        let derated = derate.apply(caps());
        assert_eq!(derated.max_export_current_a, 100.0);
        assert_eq!(derated.max_export_power_w, 1_000.0);
    }

    /// The import direction derives on its own inputs, so a request that caps
    /// only the export direction leaves the import capability untouched.
    #[test]
    fn the_two_directions_derive_independently() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        let derated = derate.apply(caps());
        assert_eq!(derated.max_import_current_a, Some(300.0));
        assert_eq!(derated.max_import_power_w, Some(250_000.0));
    }

    #[test]
    fn an_import_current_cap_derives_the_import_power_cap() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_import_current_a: Some(10.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        let derated = derate.apply(caps());
        assert_eq!(derated.max_import_current_a, Some(10.0));
        assert_eq!(derated.max_import_power_w, Some(4_000.0));
    }

    /// A relaxation is a request that no longer names the field, and the store
    /// is a replacement, so the full capability comes back. This is what the
    /// read-time derivation buys and what a port that clamped the stored report
    /// would have lost.
    #[test]
    fn a_relaxed_derate_restores_the_full_capability() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        assert_eq!(derate.apply(caps()).max_export_current_a, 100.0);

        derate.note_request(ExternalDerating::default());
        assert_eq!(derate.apply(caps()), caps());
    }

    /// `bidirectional` is outside the derate's reach, so a supply stays
    /// bidirectional however hard its import direction is capped.
    #[test]
    fn a_derate_cannot_take_the_bidirectional_capability_away() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_import_current_a: Some(0.0),
            max_import_power_w: Some(0.0),
            ..ExternalDerating::default()
        });
        assert!(derate.apply(caps()).bidirectional);
    }

    /// The voltage is remembered, so a derate arriving after the measurement
    /// derives against it rather than against nothing.
    #[test]
    fn a_derate_arriving_after_the_measurement_still_derives() {
        let mut derate = Derate::default();
        derate.note_present_voltage(400.0);
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        assert_eq!(derate.apply(caps()).max_export_power_w, 40_000.0);
    }

    /// And the derivation follows the voltage, because the C++ reads the
    /// measurement at every read rather than latching it with the request.
    #[test]
    fn the_derived_cap_follows_a_later_voltage() {
        let mut derate = Derate::default();
        derate.note_request(ExternalDerating {
            max_export_current_a: Some(100.0),
            ..ExternalDerating::default()
        });
        derate.note_present_voltage(400.0);
        assert_eq!(derate.apply(caps()).max_export_power_w, 40_000.0);

        derate.note_present_voltage(200.0);
        assert_eq!(derate.apply(caps()).max_export_power_w, 20_000.0);
    }
}
