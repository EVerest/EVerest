// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The car side power meter's measurable floors, merged into the DC capability
//! report the vehicle is offered.
//!
//! A port of `module::apply_powermeter_limits` (`powermeter_limits.hpp`), which
//! `EvseManager::get_powersupply_capabilities_for_hlc` puts between the supply's
//! report and everything the ISO 15118 stack is told.
//!
//! Three properties decide the whole of this file.
//!
//! 1. **It is a floor, not a clamp in both directions.** Every statement here
//!    reads a `min` field and writes the same `min` field. No maximum is
//!    touched on any path, so the band the vehicle is offered can only shrink
//!    from below: a meter that cannot measure under six amperes takes the two
//!    ampere offer away and leaves the six ampere one. A meter whose floor is
//!    already below the supply's minimum moves nothing at all.
//!
//! 2. **The two directions cross.** The meter names its floors from its own
//!    point of view and the supply names its limits from the other one, so the
//!    meter's `min_import_current_A`, the direction in which the car draws,
//!    raises the supply's **export** minimum, and the meter's
//!    `min_export_current_A` raises the supply's **import** minimum. Filled by
//!    name at every statement below; a positional helper would compile with the
//!    two swapped and no test in this module could tell, because both halves
//!    have the same shape.
//!
//! 3. **A floor above the available maximum is clamped down to it.** That is
//!    the one statement here that lowers a figure, and it lowers the *minimum*,
//!    so the offer still never widens. The maximum it is clamped against is the
//!    one the caller supplies, which on the seam this port uses has external
//!    derating already applied, so a derate that cuts the ceiling below the
//!    meter's floor collapses the band rather than inverting it.
//!
//! Absent means the meter named no floor for that direction, which is not a
//! floor of zero: a zero floor is a meter that says it can measure down to
//! nothing and a zero minimum is what the supply's own report already carries.

use crate::core::event::{PowerSupplyCapabilities, PowermeterCapabilities};

/// The car side power meter's last report, and the one memory of it.
///
/// It sits on `Core` rather than inside the high level communication port,
/// which is where the C++ has it (`EvseManager::powermeter_capabilities`,
/// `EvseManager.hpp:310`) and which is now load bearing: the store, the change
/// comparison and the transcript line are all outside the
/// `hlc_enabled and charge_mode == "DC"` guard, so a basic AC port with a car
/// side meter records the floors and writes the same line. A basic AC port has
/// no high level communication port at all, so a memory kept there would have
/// been a memory that deployment did not have.
///
/// `DcLimits` holds no copy. It takes the floors as an argument at each of the
/// three merge seams, so there is one holder of this fact and the stack facing
/// derivation cannot disagree with the transcript about what the meter said.
#[derive(Debug, Default)]
pub struct CarSideMeter {
    reported: Option<PowermeterCapabilities>,
}

/// A report that moved, carrying what it moved from.
///
/// The previous floors are what the stack facing change gate compares against:
/// a floor below a minimum the supply already names moves the merged report by
/// nothing, and that push carries no capability forward.
#[derive(Clone, Copy, Debug)]
pub struct MeterFloorsChanged {
    pub previous: Option<PowermeterCapabilities>,
}

impl CarSideMeter {
    /// The report, and whether it moved.
    ///
    /// `None` is the C++ early return: a report identical to the one already
    /// held returns before the session log line and before the push, so an
    /// unchanged meter costs the transcript nothing however often it reports.
    pub fn note(&mut self, report: PowermeterCapabilities) -> Option<MeterFloorsChanged> {
        if self.reported == Some(report) {
            return None;
        }
        let previous = self.reported.replace(report);
        Some(MeterFloorsChanged { previous })
    }

    /// The floors as they stand, for the merge seams. `None` is a port with no
    /// car side meter, or one that has not reported yet, and the merge is then
    /// the identity.
    pub fn floors(&self) -> Option<PowermeterCapabilities> {
        self.reported
    }
}

/// `std::max`, which returns its first argument unless that argument is
/// strictly less than the second.
///
/// Written out rather than as `f64::max`, which differs on NaN: `f64::max`
/// returns the operand that is not NaN, where every comparison against NaN is
/// false and `std::max` therefore yields `a`. The same reason
/// `core::derate::min_of` spells its comparison out.
fn std_max(a: f64, b: f64) -> f64 {
    if a < b {
        b
    } else {
        a
    }
}

/// `max_optional(std::optional<T> const& a, T b)`
/// (`everest::lib::util::max_optional`), the overload whose return type is a
/// bare `T`.
///
/// An absent capability is **adopted** rather than left absent, so a car side
/// meter can introduce an import minimum on a supply that named none. That is
/// the asymmetry which makes the import half behave unlike the export half,
/// where the field is required and can only be raised.
fn max_adopting(capability: Option<f64>, floor: f64) -> f64 {
    match capability {
        Some(value) if value > floor => value,
        _ => floor,
    }
}

/// `std::max` against a nominal minimum **only when the supply reported one**.
///
/// Absent stays absent, deliberately and for the reason the C++ states at the
/// same statement: a nominal minimum fabricated here would bypass the
/// consumers' fallback to the regular minimum, which this function has already
/// raised.
fn raise_reported(nominal: Option<f64>, floor: f64) -> Option<f64> {
    nominal.map(|value| std_max(value, floor))
}

/// `module::apply_powermeter_limits(caps, meter_capabilities, log_warnings)`.
///
/// `None` is a port with no car side meter, or one that has not reported yet,
/// and is the identity: the C++ returns `caps` untouched before it reads
/// anything (`if (not meter_capabilities.has_value())`).
///
/// The warnings are not behind a flag here. The flag exists in the C++ so a
/// caller in a hot path can silence them, and no `EvseManager` call site
/// actually passes `false`; the seam this port merges on is reached from the
/// capability report, the meter report and the derate refresh, none of which
/// runs per measurement.
pub fn apply_powermeter_limits(
    mut caps: PowerSupplyCapabilities,
    meter: Option<PowermeterCapabilities>,
) -> PowerSupplyCapabilities {
    let Some(meter) = meter else {
        return caps;
    };

    // The meter's charging direction, which is the supply's export.
    if let Some(floor) = meter.min_import_current_a {
        caps.min_export_current_a = std_max(caps.min_export_current_a, floor);
        caps.nominal_min_export_current_a =
            raise_reported(caps.nominal_min_export_current_a, floor);

        if caps.min_export_current_a > caps.max_export_current_a {
            log::warn!(
                "Power meter minimum current in charging direction ({floor} A) exceeds the \
                 currently available power supply maximum ({} A, possibly reduced by an active \
                 external derating), clamping minimum to maximum",
                caps.max_export_current_a
            );
            caps.min_export_current_a = caps.max_export_current_a;
        }
        if let (Some(min), Some(max)) = (
            caps.nominal_min_export_current_a,
            caps.nominal_max_export_current_a,
        ) {
            if min > max {
                caps.nominal_min_export_current_a = Some(max);
            }
        }
    }

    // The meter's discharging direction, which is the supply's import.
    if let Some(floor) = meter.min_export_current_a {
        caps.min_import_current_a = Some(max_adopting(caps.min_import_current_a, floor));
        caps.nominal_min_import_current_a =
            raise_reported(caps.nominal_min_import_current_a, floor);

        // Read after the assignment above, so the minimum is always present
        // here; the maximum is what may be absent, and an absent import
        // ceiling clamps nothing.
        if let (Some(min), Some(max)) = (caps.min_import_current_a, caps.max_import_current_a) {
            if min > max {
                log::warn!(
                    "Power meter minimum current in discharge direction ({floor} A) exceeds the \
                     currently available power supply maximum ({max} A, possibly reduced by an \
                     active external derating), clamping minimum to maximum"
                );
                caps.min_import_current_a = Some(max);
            }
        }
        if let (Some(min), Some(max)) = (
            caps.nominal_min_import_current_a,
            caps.nominal_max_import_current_a,
        ) {
            if min > max {
                caps.nominal_min_import_current_a = Some(max);
            }
        }
    }

    caps
}

#[cfg(test)]
mod tests {
    use super::*;

    /// `make_psu_caps` from `PowermeterLimitsTest.cpp`: charging direction from
    /// five to two hundred amperes, discharge side left unnamed, so the two
    /// halves start out asymmetric exactly as the C++ fixture does.
    fn caps() -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional: true,
            max_export_voltage_v: 900.0,
            min_export_voltage_v: 150.0,
            max_export_current_a: 200.0,
            min_export_current_a: 5.0,
            max_export_power_w: 150_000.0,
            ..PowerSupplyCapabilities::sane_default()
        }
    }

    /// Filled by name, because both fields are `Option<f64>` and the whole
    /// point of the pair is that the two directions cross.
    fn meter(import: Option<f64>, export: Option<f64>) -> Option<PowermeterCapabilities> {
        Some(PowermeterCapabilities {
            min_import_current_a: import,
            min_export_current_a: export,
        })
    }

    #[test]
    fn no_meter_at_all_is_the_identity() {
        assert_eq!(apply_powermeter_limits(caps(), None), caps());
    }

    #[test]
    fn a_meter_that_names_no_floor_is_the_identity() {
        let reported = PowerSupplyCapabilities {
            nominal_min_export_current_a: Some(6.0),
            min_import_current_a: Some(4.0),
            nominal_min_import_current_a: Some(4.5),
            ..caps()
        };

        assert_eq!(
            apply_powermeter_limits(reported, meter(None, None)),
            reported
        );
    }

    /// The direction crossing, charging half: the meter's **import** floor
    /// raises the supply's **export** minimum.
    #[test]
    fn the_meters_charging_floor_raises_the_supplys_export_minimum() {
        let reported = PowerSupplyCapabilities {
            nominal_min_export_current_a: Some(6.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(Some(10.0), None));

        assert_eq!(merged.min_export_current_a, 10.0);
        assert_eq!(merged.nominal_min_export_current_a, Some(10.0));
    }

    /// The direction crossing, discharging half: the meter's **export** floor
    /// raises the supply's **import** minimum. Together with the test above
    /// this is what a swapped pair would fail; either alone would not.
    #[test]
    fn the_meters_discharging_floor_raises_the_supplys_import_minimum() {
        let reported = PowerSupplyCapabilities {
            max_import_current_a: Some(200.0),
            nominal_min_import_current_a: Some(8.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(None, Some(12.0)));

        assert_eq!(merged.min_import_current_a, Some(12.0));
        assert_eq!(merged.nominal_min_import_current_a, Some(12.0));
    }

    /// And the halves are independent: neither floor reaches the other
    /// direction's fields.
    #[test]
    fn each_floor_moves_only_its_own_direction() {
        let reported = PowerSupplyCapabilities {
            max_import_current_a: Some(200.0),
            nominal_min_export_current_a: Some(6.0),
            nominal_min_import_current_a: Some(8.0),
            ..caps()
        };

        let import_only = apply_powermeter_limits(reported, meter(Some(10.0), None));
        assert_eq!(import_only.min_import_current_a, reported.min_import_current_a);
        assert_eq!(
            import_only.nominal_min_import_current_a,
            reported.nominal_min_import_current_a
        );

        let export_only = apply_powermeter_limits(reported, meter(None, Some(12.0)));
        assert_eq!(export_only.min_export_current_a, reported.min_export_current_a);
        assert_eq!(
            export_only.nominal_min_export_current_a,
            reported.nominal_min_export_current_a
        );
    }

    /// A nominal minimum the supply never reported is not invented, so a
    /// consumer falls back to the regular minimum this function did raise.
    #[test]
    fn an_unreported_nominal_minimum_is_not_fabricated() {
        let merged = apply_powermeter_limits(caps(), meter(Some(10.0), None));

        assert_eq!(merged.min_export_current_a, 10.0);
        assert_eq!(merged.nominal_min_export_current_a, None);
    }

    /// The floor direction, stated as the property rather than as an example:
    /// a floor **below** the standing minimum moves nothing. This is what
    /// separates a floor from a clamp in both directions, and a clamp would
    /// lower the minimum to two here.
    #[test]
    fn a_floor_below_the_standing_minimum_lowers_nothing() {
        let reported = PowerSupplyCapabilities {
            nominal_min_export_current_a: Some(6.0),
            min_import_current_a: Some(20.0),
            max_import_current_a: Some(200.0),
            nominal_min_import_current_a: Some(18.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(Some(2.0), Some(12.0)));

        assert_eq!(merged.min_export_current_a, 5.0);
        assert_eq!(merged.nominal_min_export_current_a, Some(6.0));
        assert_eq!(merged.min_import_current_a, Some(20.0));
        assert_eq!(merged.nominal_min_import_current_a, Some(18.0));
    }

    /// An absent import minimum is **adopted** from the meter, which the
    /// export half cannot do because its field is required and already carries
    /// a number. `max_optional`'s asymmetry, and the one place a car side meter
    /// introduces a limit the supply never named.
    #[test]
    fn an_unnamed_import_minimum_is_adopted_from_the_meter() {
        assert_eq!(caps().min_import_current_a, None);

        let merged = apply_powermeter_limits(caps(), meter(None, Some(80.0)));

        assert_eq!(merged.min_import_current_a, Some(80.0));
        // And with no import ceiling reported there is nothing to clamp it
        // against, so the adopted figure stands whatever its size.
        assert_eq!(merged.nominal_min_import_current_a, None);
    }

    #[test]
    fn a_charging_floor_above_the_export_ceiling_is_clamped_down_to_it() {
        let reported = PowerSupplyCapabilities {
            max_export_current_a: 100.0,
            nominal_max_export_current_a: Some(90.0),
            nominal_min_export_current_a: Some(6.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(Some(150.0), None));

        assert_eq!(merged.min_export_current_a, 100.0);
        assert_eq!(merged.nominal_min_export_current_a, Some(90.0));
    }

    /// The nominal pair is clamped only when the supply named a nominal
    /// **maximum** too, so a raised nominal minimum can stand above the
    /// regular maximum. Reproduced rather than corrected: the C++ clamps the
    /// two pairs against each other and not across.
    #[test]
    fn a_raised_nominal_minimum_is_not_clamped_without_a_nominal_maximum() {
        let reported = PowerSupplyCapabilities {
            max_export_current_a: 100.0,
            nominal_min_export_current_a: Some(6.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(Some(150.0), None));

        assert_eq!(merged.min_export_current_a, 100.0);
        assert_eq!(merged.nominal_min_export_current_a, Some(150.0));
    }

    #[test]
    fn a_discharging_floor_above_the_import_ceiling_is_clamped_down_to_it() {
        let reported = PowerSupplyCapabilities {
            max_import_current_a: Some(50.0),
            nominal_min_import_current_a: Some(10.0),
            nominal_max_import_current_a: Some(45.0),
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(None, Some(80.0)));

        assert_eq!(merged.min_import_current_a, Some(50.0));
        assert_eq!(merged.nominal_min_import_current_a, Some(45.0));
    }

    /// A supply that can export nothing collapses the band to zero rather
    /// than offering a minimum above its maximum. The case an external derate
    /// reaches, since the ceiling this clamps against is the derated one.
    #[test]
    fn a_zero_export_ceiling_clamps_the_floor_to_zero() {
        let reported = PowerSupplyCapabilities {
            max_export_current_a: 0.0,
            ..caps()
        };

        let merged = apply_powermeter_limits(reported, meter(Some(12.0), None));

        assert_eq!(merged.min_export_current_a, 0.0);
    }

    /// No maximum and no other field moves on any path, which is what lets the
    /// merged report serve as the base for the maximum limit derivations too.
    /// Asserted by restoring the four minima and comparing whole structs, so a
    /// field added to the report later is covered without editing this test.
    #[test]
    fn nothing_but_the_four_minimum_currents_moves() {
        let reported = PowerSupplyCapabilities {
            max_import_voltage_v: Some(900.0),
            min_import_voltage_v: Some(150.0),
            max_import_current_a: Some(200.0),
            min_import_current_a: Some(4.0),
            max_import_power_w: Some(150_000.0),
            nominal_max_export_current_a: Some(190.0),
            nominal_max_import_current_a: Some(190.0),
            conversion_efficiency_import: Some(0.95),
            conversion_efficiency_export: Some(0.95),
            ..caps()
        };

        let mut merged = apply_powermeter_limits(reported, meter(Some(10.0), Some(12.0)));
        merged.min_export_current_a = reported.min_export_current_a;
        merged.nominal_min_export_current_a = reported.nominal_min_export_current_a;
        merged.min_import_current_a = reported.min_import_current_a;
        merged.nominal_min_import_current_a = reported.nominal_min_import_current_a;

        assert_eq!(merged, reported);
    }

    /// A floor exactly on the standing minimum is not a change, which the C++
    /// suite does not drive and which the seam's change gate depends on: an
    /// equal floor must not make the port retell the vehicle a limit set it
    /// already has.
    #[test]
    fn a_floor_equal_to_the_standing_minimum_moves_nothing() {
        let reported = PowerSupplyCapabilities {
            min_import_current_a: Some(20.0),
            max_import_current_a: Some(200.0),
            ..caps()
        };

        assert_eq!(
            apply_powermeter_limits(reported, meter(Some(5.0), Some(20.0))),
            reported
        );
    }
}
