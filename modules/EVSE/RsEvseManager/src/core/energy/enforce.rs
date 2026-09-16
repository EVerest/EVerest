// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The answer from the energy manager, narrowed into physical limits.

use super::flow_request::{IntegerWithSource, NumberWithSource};
use super::EnergyTree;
use crate::core::hlc::dc_limits::{
    evse_maximum_limits, evse_minimum_limits, MaximumLimits, MinimumLimits,
};
use crate::core::hlc::SelectedService;
use crate::core::path::iec::AcState;
use crate::core::session::Limits;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Instant;

const ALMOST_EQUAL: f64 = 0.1;
const ACTUAL_VOLTAGE_CHANGE_V: f64 = 1.0;
static UNINITIALIZED_PHASE_WARNING_SHOWN: AtomicBool = AtomicBool::new(false);

#[derive(Clone, Debug, Default, PartialEq)]
pub struct LimitsRes {
    pub total_power_w: Option<NumberWithSource>,
    pub ac_max_current_a: Option<NumberWithSource>,
    pub ac_max_phase_count: Option<IntegerWithSource>,
}

#[derive(Clone, Debug, PartialEq)]
pub struct EnforcedLimits {
    pub uuid: String,
    pub valid_for_s: i64,
    pub schedule: Vec<ScheduleResEntry>,
    pub limits_root_side: LimitsRes,
}

/// `types::energy_price_information::PricePerkWh`.
///
/// Carried only so the republish is the whole of what arrived. The C++ hands
/// its subscribers the same struct it was given, mutating nothing but
/// `ac_max_current_A` (`energyImpl.cpp:504-515`), so any field dropped on the
/// way through this port is one the energy manager's price signal loses
/// between the optimizer and everything downstream of this node.
#[derive(Clone, Debug, PartialEq)]
pub struct PricePerKwh {
    pub timestamp: String,
    pub value: f64,
    pub currency: String,
}

/// One entry of the enforced schedule. Nothing in `core` reads it: it exists to
/// be handed back out again, for the reason given on `PricePerKwh`.
#[derive(Clone, Debug, PartialEq)]
pub struct ScheduleResEntry {
    pub timestamp: String,
    pub limits_to_root: LimitsRes,
    pub price_per_kwh: Option<PricePerKwh>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DcEnforcement {
    pub maximum: MaximumLimits,
    pub minimum: MinimumLimits,
    pub exporting_to_grid: bool,
    pub reapply_target: bool,
}

/// How an accepted phase change reaches the board.
///
/// `Charger::switch_three_phases_while_charging` (`Charger.cpp:1524-1545`) has
/// four outcomes and this covers the two that accept. The two refusals are the
/// `None` this sits inside: an HLC session (`:1528-1530`) and a board that
/// cannot switch (`energyImpl.cpp:413`).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PhaseSwitch {
    /// `Charger.cpp:1542`. Every state but the two below tells the board at
    /// once, because there is no live offer to withdraw first.
    Direct(bool),
    /// `Charger.cpp:1532-1541`. A charging session, or one already inside a
    /// break, takes the pilot break first: the power path drops the offer,
    /// waits `switch_3ph1ph_delay_s`, and only then moves the relays.
    ThroughBreak(bool),
}

#[derive(Clone, Debug, PartialEq)]
pub struct Enforcement {
    pub published: EnforcedLimits,
    pub public_limits: Limits,
    /// What `signal_max_current` carries: the magnitude with the original sign
    /// put back for a discharge (`Charger.cpp:1397`). It is the figure the
    /// module **announces**, not the one the board is given - `Iec` stores the
    /// magnitude, so the overcurrent threshold and the duty cycle are derived
    /// from that.
    pub hardware_current_a: f64,
    pub switch_to_three_phases: Option<PhaseSwitch>,
    pub dc: Option<DcEnforcement>,
    /// The UK random delay countdown to publish, absent while the feature is
    /// disabled. See `core::energy::random_delay`.
    pub countdown: Option<super::random_delay::CountDown>,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Context {
    pub selected_service: Option<SelectedService>,
    pub allow_bpt_with_iso2: bool,
    pub sae_bidi_active: bool,
    pub target_voltage_v: f64,
    /// `shared_context.hlc_charging_active`, which is the whole of what makes
    /// `Charger::switch_three_phases_while_charging` refuse
    /// (`Charger.cpp:1528-1530`).
    pub hlc_charging_active: bool,
    /// `shared_context.current_state`. The phase switch takes a different route
    /// out of `EvseState::Charging` than out of every other state
    /// (`Charger.cpp:1532-1543`), and only one of the two is ported.
    pub charger_state: AcState,
    /// `mod->timepoint_ready_for_charging` (`EvseManager.cpp:1507`), absent
    /// until the module has announced readiness. The random delay is the only
    /// reader: it treats a vehicle already at the connector shortly after that
    /// instant as a restart mid session (`energyImpl.cpp:373-380`).
    pub ready_since: Option<Instant>,
    /// The instant this answer arrived.
    ///
    /// Carried here rather than as a trailing argument because the random
    /// delay is the only part of this handler that reads a clock at all, and
    /// `Context` is already the set of facts the handler reads from outside
    /// `EnergyTree`.
    pub now: Instant,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) struct DcInputs {
    watt: f64,
    ampere: f64,
    target_voltage_v: f64,
    actual_voltage_v: f64,
    supply: crate::core::event::PowerSupplyCapabilities,
}

impl EnergyTree {
    /// `energyImpl::handle_enforce_limits` (`energyImpl.cpp:385-684`).
    pub fn enforce(&mut self, mut value: EnforcedLimits, context: Context) -> Option<Enforcement> {
        if value.uuid != self.uuid.as_str() {
            return None;
        }
        if self.active_phases == 0 {
            if !UNINITIALIZED_PHASE_WARNING_SHOWN.swap(true, Ordering::Relaxed) {
                log::warn!(
                    "number of active phases is uninitialized; waiting for board capabilities"
                );
            }
            return None;
        }

        // `energyImpl.cpp:412-431`. Three outcomes, and only one of them
        // moves the phase count. The board may not support switching at all;
        // `Charger::switch_three_phases_while_charging` refuses outright for an
        // HLC session (`Charger.cpp:1528-1530`), which is the `false` return
        // the C++ logs as ignored; otherwise the switch is accepted and
        // `ac_nr_phases_active` follows it. The watt to current conversion
        // below divides by the moved value, so an ignored request keeps
        // dividing by the phase count the board is actually on.
        let active_phases = self.active_phases;
        let mut switch_to_three_phases = None;
        if let Some(requested) = value
            .limits_root_side
            .ac_max_phase_count
            .as_ref()
            .map(|count| count.value)
            .filter(|count| *count != active_phases)
        {
            if !self.hw.supports_changing_phases_during_charging {
                log::error!(
                    "energy manager requests switching #ph from {active_phases} to {requested}, \
                     but switching phases during charging is not supported by HW"
                );
            } else if context.hlc_charging_active {
                log::warn!(
                    "energy manager requests switching #ph from {active_phases} to \
                     {requested}, ignored"
                );
            } else {
                // `Charger.cpp:1531-1542` splits on the charger state, and both
                // halves return true, so `ac_nr_phases_active` follows the
                // request either way and the watt to current conversion below
                // divides by the new count from this pass on. What differs is
                // when the relays actually move.
                switch_to_three_phases = Some(
                    if matches!(
                        context.charger_state,
                        AcState::Charging | AcState::SwitchPhases
                    ) {
                        PhaseSwitch::ThroughBreak(requested == 3)
                    } else {
                        PhaseSwitch::Direct(requested == 3)
                    },
                );
                self.active_phases = requested;
                log::info!("3ph/1ph: switching #ph from {active_phases} to {requested}");
            }
        }

        let mut current_a = value
            .limits_root_side
            .ac_max_current_a
            .as_ref()
            .map_or(0.0, |limit| limit.value);
        if let Some(power) = &value.limits_root_side.total_power_w {
            let from_power = power.value
                / self.config.ac_nominal_voltage_v
                / f64::from(self.active_phases as i32);
            if (current_a >= 0.0 && current_a > from_power)
                || (current_a < 0.0 && current_a < from_power)
            {
                current_a = from_power;
            }
        }

        // `energyImpl.cpp:445-501`. The figure above is what the energy
        // manager asked for; the delay decides what is applied in its place.
        // It runs here because everything below takes the applied figure: the
        // cable rating cap writes it into what this node republishes, the
        // charger takes it, and on a DC port the leave side limits are derived
        // from the capped republished value. Applying the delay after any of
        // the three would withhold nothing.
        let delayed = self.random_delay.apply(
            current_a,
            context.charger_state,
            context.ready_since,
            context.now,
        );
        let current_a = delayed.limit_a;

        // `energyImpl.cpp:504-513`. The cable rating narrows what this node
        // republishes for its own subscribers and nothing else: the figure
        // handed to the charger below is the uncapped one, and `Iec` applies
        // the rating itself when it reads its stored limit back, which is
        // where `get_max_current_internal` (`Charger.cpp:2047-2057`) applies
        // it. Capping here as well would apply it twice, in the wrong place,
        // and on a signed figure `std::min` does not narrow.
        if let (Some(limit), Some(rating_a)) = (
            value.limits_root_side.ac_max_current_a.as_mut(),
            self.pp_ampacity,
        ) {
            limit.value = current_a.min(rating_a);
        }

        let selected_ac_bpt = context.selected_service == Some(SelectedService::AcBpt);
        let hardware_current_a = if current_a >= 0.0 || selected_ac_bpt {
            current_a
        } else {
            0.0
        };

        // `energyImpl.cpp:530` signals the phase count, and the subscriber
        // that turns the signal into a published limit only accepts one
        // between one and three (`evse_managerImpl.cpp:52-57`). Outside that
        // range it publishes nothing at all, so the last accepted count stays
        // standing while `ac_nr_phases_active` moves on without it. Nothing
        // bounds `ac_max_phase_count` on the wire: `LimitsRes` names no range
        // and `IntegerWithSource::value` is a plain integer, so the guard is
        // reachable rather than defensive.
        if (1..=3).contains(&self.active_phases) {
            self.published_phases = self.active_phases;
        }

        let public_limits = Limits {
            max_current_a: hardware_current_a,
            nr_of_phases_available: self.published_phases,
        };

        let dc = match (
            value.limits_root_side.total_power_w.as_ref(),
            value.limits_root_side.ac_max_current_a.as_ref(),
        ) {
            (Some(watt), Some(ampere))
                if self.config.charge_mode == crate::core::config::ChargeMode::Dc =>
            {
                let inputs = DcInputs {
                    watt: watt.value,
                    ampere: ampere.value,
                    target_voltage_v: context.target_voltage_v,
                    actual_voltage_v: self.actual_voltage_v,
                    supply: self.supply,
                };
                if self
                    .last_dc_inputs
                    .is_some_and(|last| unchanged(last, inputs))
                {
                    None
                } else {
                    self.last_dc_inputs = Some(inputs);
                    Some(dc_limits(inputs, context))
                }
            }
            _ => None,
        };

        self.last_setpoint = if self.config.charge_mode == crate::core::config::ChargeMode::Dc {
            value
                .limits_root_side
                .total_power_w
                .as_ref()
                .map(|limit| super::flow_request::SetpointValue::TotalPower(limit.value))
        } else {
            Some(super::flow_request::SetpointValue::AcCurrent(
                hardware_current_a,
            ))
        };

        Some(Enforcement {
            published: value,
            public_limits,
            hardware_current_a,
            switch_to_three_phases,
            dc,
            countdown: delayed.countdown,
        })
    }
}

fn unchanged(a: DcInputs, b: DcInputs) -> bool {
    almost_eq(a.watt, b.watt)
        && almost_eq(a.ampere, b.ampere)
        && almost_eq(a.target_voltage_v, b.target_voltage_v)
        && (a.actual_voltage_v - b.actual_voltage_v).abs() <= ACTUAL_VOLTAGE_CHANGE_V
        && capabilities_almost_eq(a.supply, b.supply)
}

/// `almost_eq` (`energyImpl.cpp:324-326`), which is written there as a half
/// open window `a > b - 0.1 and a < b + 0.1`. The same set of pairs for
/// every finite input, and `random_delay` compares limits with it too.
pub(super) fn almost_eq(a: f64, b: f64) -> bool {
    (a - b).abs() < ALMOST_EQUAL
}

fn option_almost_eq(a: Option<f64>, b: Option<f64>) -> bool {
    match (a, b) {
        (Some(a), Some(b)) => almost_eq(a, b),
        (None, None) => true,
        _ => false,
    }
}

/// `almost_eq` over the capability report (`energyImpl.cpp:338-356`), in the
/// same order and over the same fifteen fields.
///
/// The twelve `nominal_*` fields on the report are deliberately not compared:
/// the C++ overload does not name them either, so a report whose nominal
/// figures alone moved is not a change here. Every field the C++ does name is
/// named, because a term dropped from this conjunction is a capability change
/// that never reaches the vehicle.
fn capabilities_almost_eq(
    a: crate::core::event::PowerSupplyCapabilities,
    b: crate::core::event::PowerSupplyCapabilities,
) -> bool {
    a.bidirectional == b.bidirectional
        && almost_eq(
            a.current_regulation_tolerance_a,
            b.current_regulation_tolerance_a,
        )
        && almost_eq(a.peak_current_ripple_a, b.peak_current_ripple_a)
        && almost_eq(a.max_export_voltage_v, b.max_export_voltage_v)
        && almost_eq(a.min_export_voltage_v, b.min_export_voltage_v)
        && almost_eq(a.max_export_current_a, b.max_export_current_a)
        && almost_eq(a.min_export_current_a, b.min_export_current_a)
        && almost_eq(a.max_export_power_w, b.max_export_power_w)
        && option_almost_eq(a.max_import_voltage_v, b.max_import_voltage_v)
        && option_almost_eq(a.min_import_voltage_v, b.min_import_voltage_v)
        && option_almost_eq(a.max_import_current_a, b.max_import_current_a)
        && option_almost_eq(a.min_import_current_a, b.min_import_current_a)
        && option_almost_eq(a.max_import_power_w, b.max_import_power_w)
        && option_almost_eq(
            a.conversion_efficiency_import,
            b.conversion_efficiency_import,
        )
        && option_almost_eq(
            a.conversion_efficiency_export,
            b.conversion_efficiency_export,
        )
}

fn dc_limits(inputs: DcInputs, context: Context) -> DcEnforcement {
    let supply = inputs.supply;
    let total_current = if inputs.target_voltage_v > 10.0 {
        inputs.watt
            / if inputs.actual_voltage_v > 10.0 {
                inputs.actual_voltage_v
            } else {
                inputs.target_voltage_v
            }
    } else {
        supply.max_export_current_a
    };

    // The supply's ceiling is the base and the allowance below cuts it down,
    // which is the order `energyImpl.cpp:566-625` uses. The base comes from the
    // one derivation in `hlc::dc_limits` rather than a second copy here, so the
    // two cannot drift: this handler is the only site that narrows it.
    let mut maximum = evse_maximum_limits(&supply);
    let minimum = evse_minimum_limits(&supply);

    if total_current >= 0.0 {
        maximum.maximum_current_a = total_current.min(supply.max_export_current_a);
    } else {
        maximum.maximum_discharge_current_a = Some(
            total_current
                .abs()
                .min(supply.max_import_current_a.unwrap_or(total_current.abs())),
        );
    }
    if inputs.watt >= 0.0 {
        maximum.maximum_power_w = inputs.watt.min(supply.max_export_power_w);
    } else {
        maximum.maximum_discharge_power_w = Some(
            inputs
                .watt
                .abs()
                .min(supply.max_import_power_w.unwrap_or(inputs.watt.abs())),
        );
    }

    let has_export = inputs.watt < 0.0
        && total_current < 0.0
        && maximum.maximum_discharge_power_w.is_some()
        && maximum.maximum_discharge_current_a.is_some();
    let mut exporting_to_grid = false;
    if has_export {
        if context.allow_bpt_with_iso2 {
            maximum.maximum_power_w = maximum.maximum_discharge_power_w.unwrap_or(0.0);
            maximum.maximum_current_a = maximum.maximum_discharge_current_a.unwrap_or(0.0);
            exporting_to_grid = true;
        } else if context.sae_bidi_active {
            maximum.maximum_power_w = -maximum.maximum_discharge_power_w.unwrap_or(0.0);
            maximum.maximum_current_a = -maximum.maximum_discharge_current_a.unwrap_or(0.0);
            exporting_to_grid = true;
        } else if context.selected_service == Some(SelectedService::DcBpt) {
            exporting_to_grid = true;
        } else {
            maximum.maximum_power_w = 0.0;
            maximum.maximum_current_a = 0.0;
            maximum.maximum_discharge_power_w = Some(0.0);
            maximum.maximum_discharge_current_a = Some(0.0);
        }
    }

    DcEnforcement {
        maximum,
        minimum,
        exporting_to_grid,
        reapply_target: inputs.target_voltage_v > 0.0,
    }
}

#[cfg(test)]
mod tests {
    use std::time::Duration;

    use super::super::flow_request::IntegerWithSource;
    use super::*;
    use crate::core::config::ChargeMode;
    use crate::core::energy::EnergyConfig;
    use crate::core::event::{HardwareCapabilities, PowerSupplyCapabilities};

    fn tree(mode: ChargeMode) -> EnergyTree {
        let mut tree = EnergyTree::new(
            super::super::NodeUuid::from_module_id("evse"),
            EnergyConfig {
                charge_mode: mode,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: true,
            },
            super::super::random_delay::boot_defaults(),
        );
        tree.note_capabilities(HardwareCapabilities {
            max_current_a_import: 32.0,
            max_current_a_export: 32.0,
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            supports_changing_phases_during_charging: true,
            supports_cp_state_e: false,
            ..HardwareCapabilities::default()
        });
        tree
    }

    fn context(service: Option<SelectedService>) -> Context {
        Context {
            selected_service: service,
            allow_bpt_with_iso2: false,
            sae_bidi_active: false,
            target_voltage_v: 400.0,
            hlc_charging_active: false,
            charger_state: AcState::PrepareCharging,
            ready_since: None,
            now: Instant::now(),
        }
    }

    fn input(uuid: &str, ampere: Option<f64>, watt: Option<f64>) -> EnforcedLimits {
        EnforcedLimits {
            uuid: uuid.to_owned(),
            valid_for_s: 60,
            schedule: Vec::new(),
            limits_root_side: LimitsRes {
                total_power_w: watt.map(|value| NumberWithSource::new(value, "power")),
                ac_max_current_a: ampere.map(|value| NumberWithSource::new(value, "current")),
                ac_max_phase_count: None,
            },
        }
    }

    #[test]
    fn another_nodes_answer_is_ignored_whole() {
        assert_eq!(
            tree(ChargeMode::Ac).enforce(input("other", Some(16.0), None), context(None)),
            None
        );
    }

    #[test]
    fn the_more_restrictive_positive_and_negative_limit_wins() {
        let mut positive = tree(ChargeMode::Ac);
        let result = positive
            .enforce(input("evse", Some(20.0), Some(6_900.0)), context(None))
            .unwrap();
        assert_eq!(result.hardware_current_a, 10.0);

        let mut negative = tree(ChargeMode::Ac);
        let result = negative
            .enforce(
                input("evse", Some(-20.0), Some(-6_900.0)),
                context(Some(SelectedService::AcBpt)),
            )
            .unwrap();
        assert_eq!(result.hardware_current_a, -10.0);
    }

    #[test]
    fn a_negative_ac_limit_needs_the_selected_ac_bpt_service() {
        let mut ordinary = tree(ChargeMode::Ac);
        assert_eq!(
            ordinary
                .enforce(input("evse", Some(-16.0), None), context(None))
                .unwrap()
                .hardware_current_a,
            0.0
        );
        let mut bpt = tree(ChargeMode::Ac);
        assert_eq!(
            bpt.enforce(
                input("evse", Some(-16.0), None),
                context(Some(SelectedService::AcBpt)),
            )
            .unwrap()
            .hardware_current_a,
            -16.0
        );
    }

    /// The UK random delay sits between the requested limit and every
    /// consumer of it (`energyImpl.cpp:445-501`), so the held figure is what
    /// the cable rating narrows at `:508` and what the charger takes at
    /// `:519`. A port that applied the delay after the cap, or that handed the
    /// charger the pre delay figure, would hold nothing at all.
    ///
    /// Discriminating: the cap is 13 A, the request is 6 A and the held limit
    /// is 20 A, so the republished current is 13 with the delay in the right
    /// place and 6 with it in the wrong one.
    #[test]
    fn a_held_limit_is_what_the_cable_cap_and_the_charger_both_see() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_pp_ampacity(13.0);
        tree.random_delay_mut().enable();
        let t0 = Instant::now();
        let settled = Context {
            now: t0,
            ..context(None)
        };

        // Settle at 20 A: the first change draws a delay, which is waited out.
        let started = tree
            .enforce(input("evse", Some(20.0), None), settled)
            .unwrap();
        let drawn = u64::try_from(started.countdown.unwrap().countdown_s).expect("a delay");
        let after = Context {
            now: t0 + Duration::from_secs(drawn),
            ..context(None)
        };
        tree.enforce(input("evse", Some(20.0), None), after);
        let steady = tree
            .enforce(input("evse", Some(20.0), None), after)
            .unwrap();
        assert_eq!(steady.hardware_current_a, 20.0);
        assert_eq!(steady.countdown.unwrap().countdown_s, 0);

        // Now the energy manager asks for 6 A and the delay withholds it.
        let held = tree.enforce(input("evse", Some(6.0), None), after).unwrap();
        assert!(held.countdown.unwrap().countdown_s > 0);
        assert_eq!(held.hardware_current_a, 20.0, "the charger got the request");
        assert_eq!(
            held.published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            13.0,
            "the cable cap narrowed the request instead of the held limit"
        );
        assert_eq!(held.countdown.unwrap().current_limit_after_delay_a, 6.0);
        assert_eq!(held.countdown.unwrap().current_limit_during_delay_a, 20.0);
    }

    /// A held limit does not reach the republished limits at all unless the
    /// cable reports a rating. `energyImpl.cpp:504-511` rewrites
    /// `ac_max_current_A` only inside `if (pp_rating)`, so on a port whose
    /// board reports none the struct goes back out carrying the figure the
    /// energy manager sent while the charger is given the held one. A
    /// consumer of `enforced_limits`, OCPP among them, is therefore told the
    /// port is at the new limit during the whole delay.
    ///
    /// Preserved rather than fixed: making the republish agree with the
    /// charger changes what an external consumer is told, which is a decision
    /// for the C++ first. Pinned so the disagreement is deliberate.
    #[test]
    fn without_a_cable_rating_the_republished_limit_is_the_one_being_withheld() {
        let mut tree = tree(ChargeMode::Ac);
        tree.random_delay_mut().enable();
        let t0 = Instant::now();
        let at = Context {
            now: t0,
            ..context(None)
        };
        let held = tree.enforce(input("evse", Some(16.0), None), at).unwrap();
        assert!(held.countdown.unwrap().countdown_s > 0);
        assert_eq!(held.hardware_current_a, 0.0, "the charger was not held");
        assert_eq!(
            held.published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            16.0,
            "the republish was narrowed with no cable rating to narrow it by"
        );
    }

    /// The delay withholds the ampere figure and nothing else, and every DC
    /// leave side limit is derived from `total_power_W` and the supply's
    /// capabilities (`energyImpl.cpp:564-628`). `ac_max_current_A` reaches the
    /// DC block only through the change detector at `:549`. So a DC session
    /// under a running random delay is offered the full new power at once:
    /// the delay restrains the AC pilot current, which is not what carries DC
    /// power.
    ///
    /// Pinned as a statement about the C++, not as an endorsement. See
    /// `docs/architecture.md`.
    #[test]
    fn a_dc_session_is_not_restrained_by_a_running_random_delay() {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(crate::core::event::PowerSupplyCapabilities {
            max_export_current_a: 400.0,
            max_export_power_w: 300_000.0,
            ..PowerSupplyCapabilities::sane_default()
        });
        tree.random_delay_mut().enable();
        let at = Context {
            now: Instant::now(),
            ..context(None)
        };
        let held = tree
            .enforce(input("evse", Some(100.0), Some(50_000.0)), at)
            .unwrap();
        assert!(held.countdown.unwrap().countdown_s > 0, "no delay ran");
        assert_eq!(
            held.hardware_current_a, 0.0,
            "the pilot current was not held"
        );
        let dc = held.dc.expect("a DC port derives its leave side limits");
        assert_eq!(
            dc.maximum.maximum_power_w, 50_000.0,
            "the delay reached the vehicle's power limit"
        );
    }

    /// The delay holds the ampere figure, which on an AC port is derived from
    /// the watt limit too (`energyImpl.cpp:434-443`, before the snapshot at
    /// `:445`). So a change that arrives only as watts is delayed as well.
    #[test]
    fn a_change_that_arrives_only_as_watts_is_delayed_too() {
        let mut tree = tree(ChargeMode::Ac);
        tree.random_delay_mut().enable();
        let t0 = Instant::now();
        let at = Context {
            now: t0,
            ..context(None)
        };
        // 20 A asked for, but 6 900 W over the board's three phases at 230 V
        // caps it to 10 A.
        let started = tree
            .enforce(input("evse", Some(20.0), Some(6_900.0)), at)
            .unwrap();
        assert!(started.countdown.unwrap().countdown_s > 0);
        assert_eq!(
            started.countdown.unwrap().current_limit_after_delay_a,
            10.0,
            "the countdown named the ampere limit and not the watt derived one"
        );
    }

    /// `energyImpl.cpp:504-513` narrows what it republishes by the cable
    /// rating and hands the charger the figure it started with. Pinned
    /// because capping both is the obvious reading and it double applies a
    /// limit `Charger::get_max_current_internal` already owns.
    #[test]
    fn the_cable_cap_changes_only_the_republished_current() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_pp_ampacity(13.0);
        let result = tree
            .enforce(input("evse", Some(20.0), None), context(None))
            .unwrap();
        assert_eq!(result.hardware_current_a, 20.0);
        assert_eq!(
            result
                .published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            13.0
        );
    }

    /// No cable rating at all leaves the republished figure alone rather than
    /// capping it at zero: `read_pp_ampacity` reports nothing for a zero
    /// reading and the C++ skips the whole rewrite when it does.
    #[test]
    fn no_cable_rating_leaves_the_republished_current_alone() {
        let mut tree = tree(ChargeMode::Ac);
        let result = tree
            .enforce(input("evse", Some(20.0), None), context(None))
            .unwrap();
        assert_eq!(
            result
                .published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            20.0
        );
        assert_eq!(result.hardware_current_a, 20.0);
    }

    /// A watt limit that beats the ampere limit is what gets republished, and
    /// the cable rating narrows that figure rather than the one it replaced.
    #[test]
    fn the_cable_cap_narrows_the_watt_derived_current() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_pp_ampacity(13.0);
        let result = tree
            .enforce(input("evse", Some(20.0), Some(10_350.0)), context(None))
            .unwrap();
        // 10350 W / 230 V / 3 phases is 15 A, which beats the 20 A ampere
        // limit and still sits above the 13 A cable.
        assert_eq!(result.hardware_current_a, 15.0);
        assert_eq!(
            result
                .published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            13.0
        );
    }

    /// `std::min` does not narrow a discharge request, so the republished
    /// figure keeps its sign and its magnitude however small the cable is.
    #[test]
    fn the_cable_cap_does_not_narrow_a_discharge_request() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_pp_ampacity(13.0);
        let result = tree
            .enforce(
                input("evse", Some(-20.0), None),
                context(Some(SelectedService::AcBpt)),
            )
            .unwrap();
        assert_eq!(
            result
                .published
                .limits_root_side
                .ac_max_current_a
                .unwrap()
                .value,
            -20.0
        );
        assert_eq!(result.hardware_current_a, -20.0);
    }

    /// The whole reason `uuid` is a `NodeUuid` on the tree and not a field the
    /// builder fills: the request this node publishes and the answer it
    /// accepts have to name the same node, and the enforce gate
    /// (`energyImpl.cpp:386`) is silent when they do not. A port that gave the
    /// request a fresh identity per publish would drop every enforced limit
    /// with nothing logged and every test still passing, so the round trip is
    /// pinned here rather than left to the type.
    #[test]
    fn the_published_request_and_the_accepted_answer_name_the_same_node() {
        let mut tree = tree(ChargeMode::Ac);
        let publish = super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional: false,
            priority: false,
        };

        let first = tree.flow_request(publish).uuid.as_str().to_owned();
        let second = tree.flow_request(publish).uuid.as_str().to_owned();
        assert_eq!(
            first, second,
            "the identity must not move between publishes"
        );

        let mut request = input("evse", Some(16.0), None);
        request.uuid = first;
        assert!(
            tree.enforce(request, context(None)).is_some(),
            "the answer to the published request has to be accepted"
        );
    }

    /// `evse_managerImpl.cpp:52-57` publishes the phase count only while it is
    /// between one and three, so a count outside the range leaves the last
    /// accepted one published while the working count moves on. Driven because
    /// nothing on the wire bounds the request.
    #[test]
    fn an_out_of_range_phase_count_is_worked_with_but_not_published() {
        let mut tree = tree(ChargeMode::Ac);
        let mut request = input("evse", Some(20.0), Some(9_200.0));
        request.limits_root_side.ac_max_phase_count =
            Some(IntegerWithSource::new(4, "energy_manager"));

        let result = tree.enforce(request, context(None)).unwrap();
        // The switch was accepted, so the conversion divides by four.
        assert_eq!(
            result.switch_to_three_phases,
            Some(PhaseSwitch::Direct(false))
        );
        assert_eq!(result.hardware_current_a, 10.0);
        // The published count stays at the last one inside the range.
        assert_eq!(result.public_limits.nr_of_phases_available, 3);
    }

    /// A bidirectional session publishes its request before the first enforced
    /// limit arrives, and there is no setpoint to name yet. An empty list is
    /// the honest answer; a zero would be a request to sit at zero amperes.
    #[test]
    fn a_bidirectional_session_names_no_setpoint_before_the_first_limit() {
        let tree = tree(ChargeMode::Ac);
        assert!(tree
            .flow_request(super::super::Publish {
                charger_state: crate::core::path::iec::AcState::Charging,
                bidirectional: true,
                priority: false,
            })
            .schedule_setpoints
            .is_empty());
    }

    /// The DC setpoint is the enforced total power, so an allowance that names
    /// no power names no setpoint either, even after an enforced limit has been
    /// accepted.
    #[test]
    fn a_dc_allowance_without_a_power_figure_names_no_setpoint() {
        let mut tree = dc_tree();
        tree.enforce(input("evse", Some(100.0), None), context(None));
        assert!(tree
            .flow_request(super::super::Publish {
                charger_state: crate::core::path::iec::AcState::Charging,
                bidirectional: true,
                priority: false,
            })
            .schedule_setpoints
            .is_empty());
    }

    /// `energyImpl.cpp:412-431` has three outcomes and only one of them moves
    /// the phase count. `Charger::switch_three_phases_while_charging` refuses
    /// for an HLC session (`Charger.cpp:1528-1530`) and the C++ logs that as
    /// ignored, so the count stays where the board has it.
    #[test]
    fn a_phase_change_is_refused_during_an_hlc_session() {
        let mut request = input("evse", Some(20.0), None);
        request.limits_root_side.ac_max_phase_count =
            Some(IntegerWithSource::new(1, "energy_manager"));

        let mut accepted = tree(ChargeMode::Ac);
        let result = accepted.enforce(request.clone(), context(None)).unwrap();
        assert_eq!(
            result.switch_to_three_phases,
            Some(PhaseSwitch::Direct(false))
        );
        assert_eq!(result.public_limits.nr_of_phases_available, 1);

        let mut refused = tree(ChargeMode::Ac);
        let result = refused
            .enforce(
                request,
                Context {
                    hlc_charging_active: true,
                    ..context(None)
                },
            )
            .unwrap();
        assert_eq!(result.switch_to_three_phases, None);
        assert_eq!(result.public_limits.nr_of_phases_available, 3);
    }

    /// `Charger.cpp:1531-1542` splits on the state: a charging session, or one
    /// already inside a break, takes the pilot break, and every other state
    /// takes the direct board call. Both accept, so the published count follows
    /// the request either way.
    #[test]
    fn a_charging_session_takes_the_break_and_every_other_state_the_direct_call() {
        let mut request = input("evse", Some(20.0), None);
        request.limits_root_side.ac_max_phase_count =
            Some(IntegerWithSource::new(1, "energy_manager"));

        for state in [AcState::Charging, AcState::SwitchPhases] {
            let mut tree = tree(ChargeMode::Ac);
            let result = tree
                .enforce(
                    request.clone(),
                    Context {
                        charger_state: state,
                        ..context(None)
                    },
                )
                .unwrap();
            assert_eq!(
                result.switch_to_three_phases,
                Some(PhaseSwitch::ThroughBreak(false)),
                "{state:?} owes the vehicle a break"
            );
            // `Charger.cpp:1534` records the value and the call returns true,
            // so `ac_nr_phases_active` follows it before the relays move.
            assert_eq!(
                result.public_limits.nr_of_phases_available, 1,
                "{state:?} did not follow the request"
            );
        }

        for state in [
            AcState::PrepareCharging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::WaitingForAuthentication,
        ] {
            let mut tree = tree(ChargeMode::Ac);
            let result = tree
                .enforce(
                    request.clone(),
                    Context {
                        charger_state: state,
                        ..context(None)
                    },
                )
                .unwrap();
            assert_eq!(
                result.switch_to_three_phases,
                Some(PhaseSwitch::Direct(false)),
                "{state:?} takes the direct board call"
            );
        }
    }

    /// A board that cannot switch while charging gets no command either, and
    /// the phase count stays put on that route too.
    #[test]
    fn a_phase_change_needs_a_board_that_supports_switching() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_capabilities(HardwareCapabilities {
            max_current_a_import: 32.0,
            min_phase_count_import: 1,
            max_phase_count_import: 3,
            supports_changing_phases_during_charging: false,
            supports_cp_state_e: false,
            ..HardwareCapabilities::default()
        });
        let mut request = input("evse", Some(20.0), None);
        request.limits_root_side.ac_max_phase_count =
            Some(IntegerWithSource::new(1, "energy_manager"));

        let result = tree.enforce(request, context(None)).unwrap();
        assert_eq!(result.switch_to_three_phases, None);
        assert_eq!(result.public_limits.nr_of_phases_available, 3);
    }

    /// The phase count the watt to current conversion divides by is the one
    /// the switch left behind, not the one the request asked for. A refused
    /// switch therefore keeps dividing by the board's own count, which is the
    /// only reason the two cases below differ.
    #[test]
    fn the_watt_conversion_divides_by_the_phase_count_the_switch_left() {
        let mut request = input("evse", Some(20.0), Some(6_900.0));
        request.limits_root_side.ac_max_phase_count =
            Some(IntegerWithSource::new(1, "energy_manager"));

        // Switched to one phase, 6900 W is 30 A, so the 20 A ampere limit is
        // the more restrictive of the two and survives.
        let mut accepted = tree(ChargeMode::Ac);
        assert_eq!(
            accepted
                .enforce(request.clone(), context(None))
                .unwrap()
                .hardware_current_a,
            20.0
        );

        // Left on three phases, the same 6900 W is 10 A and wins instead.
        let mut refused = tree(ChargeMode::Ac);
        assert_eq!(
            refused
                .enforce(
                    request,
                    Context {
                        hlc_charging_active: true,
                        ..context(None)
                    },
                )
                .unwrap()
                .hardware_current_a,
            10.0
        );
    }

    fn supply() -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional: true,
            max_export_voltage_v: 900.0,
            min_export_voltage_v: 100.0,
            max_export_current_a: 200.0,
            min_export_current_a: 2.0,
            max_export_power_w: 100_000.0,
            max_import_current_a: Some(150.0),
            min_import_current_a: Some(3.0),
            max_import_power_w: Some(80_000.0),
            min_import_voltage_v: Some(100.0),
            ..PowerSupplyCapabilities::sane_default()
        }
    }

    #[test]
    fn dc_limits_are_narrowed_by_the_energy_allowance() {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(supply());
        tree.note_supply_voltage(400.0);
        let dc = tree
            .enforce(input("evse", Some(200.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 50.0);
        assert_eq!(dc.maximum.maximum_power_w, 20_000.0);
        assert_eq!(dc.minimum.minimum_discharge_power_w, Some(300.0));
        assert!(!dc.exporting_to_grid);
    }

    #[test]
    fn dc_export_is_zeroed_without_a_matching_bidirectional_source() {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(supply());
        tree.note_supply_voltage(400.0);
        let dc = tree
            .enforce(input("evse", Some(-100.0), Some(-20_000.0)), context(None))
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 0.0);
        assert_eq!(dc.maximum.maximum_power_w, 0.0);
        assert_eq!(dc.maximum.maximum_discharge_current_a, Some(0.0));
        assert_eq!(dc.maximum.maximum_discharge_power_w, Some(0.0));
    }

    fn dc_tree() -> EnergyTree {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(supply());
        tree.note_supply_voltage(400.0);
        tree
    }

    /// `energyImpl.cpp:390-400` returns before anything else while the board
    /// has not reported its phase count, so no limit is enforced and nothing is
    /// republished. The port that skips this gate divides by zero in the watt
    /// conversion two lines later.
    #[test]
    fn an_uninitialized_phase_count_enforces_nothing() {
        let mut tree = EnergyTree::new(
            super::super::NodeUuid::from_module_id("evse"),
            EnergyConfig {
                charge_mode: ChargeMode::Ac,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: true,
            },
            super::super::random_delay::boot_defaults(),
        );
        assert_eq!(
            tree.enforce(input("evse", Some(16.0), Some(3_680.0)), context(None)),
            None
        );
    }

    /// `energyImpl.cpp:535-536` needs both figures present. One without the
    /// other leaves the vehicle's limit set alone rather than deriving it from
    /// half an allowance.
    #[test]
    fn the_dc_limit_set_needs_both_the_watt_and_the_ampere_figure() {
        assert!(dc_tree()
            .enforce(input("evse", Some(100.0), None), context(None))
            .unwrap()
            .dc
            .is_none());
        assert!(dc_tree()
            .enforce(input("evse", None, Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .is_none());
        assert!(dc_tree()
            .enforce(input("evse", Some(100.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .is_some());
    }

    /// An AC port never reaches the block at all, whatever it is given
    /// (`energyImpl.cpp:532`).
    #[test]
    fn an_ac_port_derives_no_dc_limit_set() {
        let mut tree = tree(ChargeMode::Ac);
        tree.note_supply_capabilities(supply());
        assert!(tree
            .enforce(input("evse", Some(100.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .is_none());
    }

    /// Each of the five terms of `energyImpl.cpp:548-554` on its own, because a
    /// conjunction is only as pinned as its least exercised term: a port that
    /// dropped four of the five would still satisfy a test that drives one.
    /// The thresholds differ and that difference is pinned too, `almost_eq` at
    /// a tenth (`:324-326`) against `voltage_changed` at a whole volt
    /// (`:18-21`).
    #[test]
    fn each_term_of_the_change_gate_defeats_it_on_its_own() {
        let base = input("evse", Some(100.0), Some(20_000.0));

        // The watt figure.
        let mut tree = dc_tree();
        assert!(tree
            .enforce(base.clone(), context(None))
            .unwrap()
            .dc
            .is_some());
        assert!(tree
            .enforce(input("evse", Some(100.0), Some(20_000.2)), context(None))
            .unwrap()
            .dc
            .is_some());

        // The ampere figure.
        let mut tree = dc_tree();
        tree.enforce(base.clone(), context(None));
        assert!(tree
            .enforce(input("evse", Some(100.2), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .is_some());

        // The target voltage, which arrives on the context rather than the
        // request.
        let mut tree = dc_tree();
        tree.enforce(base.clone(), context(None));
        assert!(tree
            .enforce(
                base.clone(),
                Context {
                    target_voltage_v: 400.2,
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .is_some());

        // The measured voltage, which needs more than a volt.
        let mut tree = dc_tree();
        tree.enforce(base.clone(), context(None));
        tree.note_supply_voltage(401.5);
        assert!(tree
            .enforce(base.clone(), context(None))
            .unwrap()
            .dc
            .is_some());

        // The capability report. Driven through the conversion efficiency
        // because it is one of the four fields nothing else in this port reads,
        // so only the comparison itself can notice it.
        let mut tree = dc_tree();
        tree.enforce(base.clone(), context(None));
        tree.note_supply_capabilities(PowerSupplyCapabilities {
            conversion_efficiency_export: Some(0.9),
            ..supply()
        });
        assert!(tree.enforce(base, context(None)).unwrap().dc.is_some());
    }

    /// Every one of the fifteen fields `energyImpl.cpp:338-356` compares, each
    /// on its own. A conjunction is only as pinned as its least exercised
    /// term, and four of these fields are read by nothing else in this port,
    /// so only this comparison can notice them: they were the four the first
    /// cut of the gate left out.
    ///
    /// The twelve `nominal_*` fields are asserted the other way, because the
    /// C++ overload does not name them and a port that compared everything
    /// would emit a new limit set for a report that only restated them.
    #[test]
    fn every_compared_capability_field_defeats_the_change_gate_on_its_own() {
        type Perturb = (
            &'static str,
            fn(PowerSupplyCapabilities) -> PowerSupplyCapabilities,
        );

        const COMPARED: &[Perturb] = &[
            ("bidirectional", |c| PowerSupplyCapabilities {
                bidirectional: !c.bidirectional,
                ..c
            }),
            ("current_regulation_tolerance_a", |c| {
                PowerSupplyCapabilities {
                    current_regulation_tolerance_a: c.current_regulation_tolerance_a + 1.0,
                    ..c
                }
            }),
            ("peak_current_ripple_a", |c| PowerSupplyCapabilities {
                peak_current_ripple_a: c.peak_current_ripple_a + 1.0,
                ..c
            }),
            ("max_export_voltage_v", |c| PowerSupplyCapabilities {
                max_export_voltage_v: c.max_export_voltage_v + 1.0,
                ..c
            }),
            ("min_export_voltage_v", |c| PowerSupplyCapabilities {
                min_export_voltage_v: c.min_export_voltage_v + 1.0,
                ..c
            }),
            ("max_export_current_a", |c| PowerSupplyCapabilities {
                max_export_current_a: c.max_export_current_a + 1.0,
                ..c
            }),
            ("min_export_current_a", |c| PowerSupplyCapabilities {
                min_export_current_a: c.min_export_current_a + 1.0,
                ..c
            }),
            ("max_export_power_w", |c| PowerSupplyCapabilities {
                max_export_power_w: c.max_export_power_w + 1.0,
                ..c
            }),
            ("max_import_voltage_v", |c| PowerSupplyCapabilities {
                max_import_voltage_v: Some(c.max_import_voltage_v.unwrap_or(0.0) + 1.0),
                ..c
            }),
            ("min_import_voltage_v", |c| PowerSupplyCapabilities {
                min_import_voltage_v: Some(c.min_import_voltage_v.unwrap_or(0.0) + 1.0),
                ..c
            }),
            ("max_import_current_a", |c| PowerSupplyCapabilities {
                max_import_current_a: Some(c.max_import_current_a.unwrap_or(0.0) + 1.0),
                ..c
            }),
            ("min_import_current_a", |c| PowerSupplyCapabilities {
                min_import_current_a: Some(c.min_import_current_a.unwrap_or(0.0) + 1.0),
                ..c
            }),
            ("max_import_power_w", |c| PowerSupplyCapabilities {
                max_import_power_w: Some(c.max_import_power_w.unwrap_or(0.0) + 1.0),
                ..c
            }),
            ("conversion_efficiency_import", |c| {
                PowerSupplyCapabilities {
                    conversion_efficiency_import: Some(
                        c.conversion_efficiency_import.unwrap_or(0.0) + 1.0,
                    ),
                    ..c
                }
            }),
            ("conversion_efficiency_export", |c| {
                PowerSupplyCapabilities {
                    conversion_efficiency_export: Some(
                        c.conversion_efficiency_export.unwrap_or(0.0) + 1.0,
                    ),
                    ..c
                }
            }),
        ];

        const NOT_COMPARED: &[Perturb] = &[
            ("nominal_max_export_current_a", |c| {
                PowerSupplyCapabilities {
                    nominal_max_export_current_a: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_max_export_power_w", |c| PowerSupplyCapabilities {
                nominal_max_export_power_w: Some(1_234.0),
                ..c
            }),
            ("nominal_max_export_voltage_v", |c| {
                PowerSupplyCapabilities {
                    nominal_max_export_voltage_v: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_max_import_current_a", |c| {
                PowerSupplyCapabilities {
                    nominal_max_import_current_a: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_max_import_power_w", |c| PowerSupplyCapabilities {
                nominal_max_import_power_w: Some(1_234.0),
                ..c
            }),
            ("nominal_max_import_voltage_v", |c| {
                PowerSupplyCapabilities {
                    nominal_max_import_voltage_v: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_min_export_current_a", |c| {
                PowerSupplyCapabilities {
                    nominal_min_export_current_a: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_min_export_voltage_v", |c| {
                PowerSupplyCapabilities {
                    nominal_min_export_voltage_v: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_min_import_current_a", |c| {
                PowerSupplyCapabilities {
                    nominal_min_import_current_a: Some(1_234.0),
                    ..c
                }
            }),
            ("nominal_min_import_voltage_v", |c| {
                PowerSupplyCapabilities {
                    nominal_min_import_voltage_v: Some(1_234.0),
                    ..c
                }
            }),
        ];

        let request = input("evse", Some(100.0), Some(20_000.0));

        for (field, perturb) in COMPARED {
            let mut tree = dc_tree();
            assert!(tree
                .enforce(request.clone(), context(None))
                .unwrap()
                .dc
                .is_some());
            tree.note_supply_capabilities(perturb(supply()));
            assert!(
                tree.enforce(request.clone(), context(None))
                    .unwrap()
                    .dc
                    .is_some(),
                "a change to {field} has to defeat the gate"
            );
        }

        for (field, perturb) in NOT_COMPARED {
            let mut tree = dc_tree();
            assert!(tree
                .enforce(request.clone(), context(None))
                .unwrap()
                .dc
                .is_some());
            tree.note_supply_capabilities(perturb(supply()));
            assert!(
                tree.enforce(request.clone(), context(None))
                    .unwrap()
                    .dc
                    .is_none(),
                "a change to {field} must not defeat the gate"
            );
        }
    }

    /// The other side of the same conjunction: a move inside every threshold
    /// leaves the set suppressed. The measured voltage moves by half a volt,
    /// which is inside its own whole volt threshold and well outside the tenth
    /// the other four use, so a port that gave it the tighter threshold fails
    /// here.
    #[test]
    fn a_move_inside_every_threshold_leaves_the_change_gate_shut() {
        let mut tree = dc_tree();
        assert!(tree
            .enforce(input("evse", Some(100.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .is_some());

        tree.note_supply_voltage(400.5);
        assert!(tree
            .enforce(
                input("evse", Some(100.05), Some(20_000.05)),
                Context {
                    target_voltage_v: 400.05,
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .is_none());
    }

    /// `energyImpl.cpp:576-590`. Three ways to reach a total current and the
    /// port only ever drove the first. The second and third differ in which
    /// voltage divides the watt figure, and the third does not divide at all.
    #[test]
    fn the_total_current_has_three_derivations() {
        // Target and measured voltage both above ten: the measured one divides.
        let mut measured = dc_tree();
        measured.note_supply_voltage(500.0);
        let dc = measured
            .enforce(input("evse", Some(200.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 40.0);

        // Target above ten, nothing measured yet: the target divides.
        let mut unmeasured = dc_tree();
        unmeasured.note_supply_voltage(0.0);
        let dc = unmeasured
            .enforce(input("evse", Some(200.0), Some(20_000.0)), context(None))
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 50.0);

        // No target voltage: the supply's own export ceiling stands in, and
        // the watt allowance does not reach the current maximum at all.
        let mut untargeted = dc_tree();
        let dc = untargeted
            .enforce(
                input("evse", Some(200.0), Some(20_000.0)),
                Context {
                    target_voltage_v: 0.0,
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 200.0);
        assert!(!dc.reapply_target);
    }

    /// The four branches of `energyImpl.cpp:632-660`, in the order the C++
    /// tests them. Only the last was driven before, and it is the only one of
    /// the four that is not an export.
    #[test]
    fn each_bidirectional_source_shapes_the_export_differently() {
        let request = input("evse", Some(-100.0), Some(-20_000.0));

        // The ISO 15118-2 hack reports the discharge figures as positive
        // numbers on the charge maxima.
        let dc = dc_tree()
            .enforce(
                request.clone(),
                Context {
                    allow_bpt_with_iso2: true,
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_power_w, 20_000.0);
        assert_eq!(dc.maximum.maximum_current_a, 50.0);
        assert!(dc.exporting_to_grid);

        // SAE J2847/2 reports the same figures negated.
        let dc = dc_tree()
            .enforce(
                request.clone(),
                Context {
                    sae_bidi_active: true,
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_power_w, -20_000.0);
        assert_eq!(dc.maximum.maximum_current_a, -50.0);
        assert!(dc.exporting_to_grid);

        // An ISO 15118-20 BPT session leaves both maxima at the supply's
        // export ceiling and only records the direction. Pinned because
        // leaving them untouched reads like an oversight and is the C++
        // behavior.
        let dc = dc_tree()
            .enforce(
                request.clone(),
                Context {
                    selected_service: Some(SelectedService::DcBpt),
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_power_w, 100_000.0);
        assert_eq!(dc.maximum.maximum_current_a, 200.0);
        assert_eq!(dc.maximum.maximum_discharge_power_w, Some(20_000.0));
        assert_eq!(dc.maximum.maximum_discharge_current_a, Some(50.0));
        assert!(dc.exporting_to_grid);

        // No source at all zeroes all four and reports no export.
        let dc = dc_tree()
            .enforce(request, context(None))
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_power_w, 0.0);
        assert_eq!(dc.maximum.maximum_current_a, 0.0);
        assert_eq!(dc.maximum.maximum_discharge_power_w, Some(0.0));
        assert_eq!(dc.maximum.maximum_discharge_current_a, Some(0.0));
        assert!(!dc.exporting_to_grid);
    }

    /// A discharge allowance with no target voltage is not an export, because
    /// the total current falls back to the supply's positive export ceiling and
    /// the four way guard at `energyImpl.cpp:633-635` needs both figures
    /// negative. So the charge maxima survive and the discharge power maximum
    /// is still filled: the combination reads like a contradiction and is what
    /// the C++ produces.
    #[test]
    fn a_discharge_allowance_before_any_target_voltage_is_not_an_export() {
        let dc = dc_tree()
            .enforce(
                input("evse", Some(-100.0), Some(-20_000.0)),
                Context {
                    target_voltage_v: 0.0,
                    selected_service: Some(SelectedService::DcBpt),
                    ..context(None)
                },
            )
            .unwrap()
            .dc
            .unwrap();
        assert_eq!(dc.maximum.maximum_current_a, 200.0);
        assert_eq!(dc.maximum.maximum_discharge_power_w, Some(20_000.0));
        assert!(!dc.exporting_to_grid);
    }

    #[test]
    fn the_five_way_change_gate_suppresses_only_an_unchanged_dc_set() {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(supply());
        tree.note_supply_voltage(400.0);
        let request = input("evse", Some(100.0), Some(20_000.0));
        assert!(tree
            .enforce(request.clone(), context(None))
            .unwrap()
            .dc
            .is_some());
        assert!(tree
            .enforce(request.clone(), context(None))
            .unwrap()
            .dc
            .is_none());
        tree.note_supply_voltage(401.01);
        assert!(tree.enforce(request, context(None)).unwrap().dc.is_some());
    }

    #[test]
    fn bidirectional_setpoints_keep_the_enforced_sign() {
        let mut ac = tree(ChargeMode::Ac);
        ac.enforce(
            input("evse", Some(-16.0), None),
            context(Some(SelectedService::AcBpt)),
        );
        let request = ac.flow_request(super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional: true,
            priority: false,
        });
        assert_eq!(
            request.schedule_setpoints[0].value,
            super::super::flow_request::SetpointValue::AcCurrent(-16.0)
        );

        let mut dc = tree(ChargeMode::Dc);
        dc.note_supply_capabilities(supply());
        dc.enforce(
            input("evse", Some(-100.0), Some(-20_000.0)),
            Context {
                selected_service: Some(SelectedService::DcBpt),
                ..context(None)
            },
        );
        let request = dc.flow_request(super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional: true,
            priority: false,
        });
        assert_eq!(
            request.schedule_setpoints[0].value,
            super::super::flow_request::SetpointValue::TotalPower(-20_000.0)
        );
    }

    #[test]
    fn a_unidirectional_profile_publishes_no_setpoint() {
        let mut tree = tree(ChargeMode::Ac);
        tree.enforce(input("evse", Some(16.0), None), context(None));
        let request = tree.flow_request(super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional: false,
            priority: false,
        });
        assert!(request.schedule_setpoints.is_empty());
    }

    fn v2h_tree() -> EnergyTree {
        let mut tree = EnergyTree::new(
            super::super::NodeUuid::from_module_id("evse"),
            EnergyConfig {
                charge_mode: ChargeMode::Dc,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: true,
                request_zero_power_in_idle: true,
            },
            super::super::random_delay::boot_defaults(),
        );
        tree.note_supply_capabilities(supply());
        tree
    }

    fn import_power_w(tree: &mut EnergyTree, bidirectional: bool) -> f64 {
        tree.flow_request(super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional,
            priority: false,
        })
        .schedule_import
        .first()
        .limits_to_leaves
        .total_power_w
        .as_ref()
        .unwrap()
        .value
    }

    /// `setup_v2h_mode` is the only writer of `external_local_energy_limits`
    /// and nothing clears it, so the V2H schedule outlives the discharge that
    /// installed it and the next session still asks for zero import. The
    /// current demand finished callback clears `sae_bidi_active` and leaves the
    /// schedule standing, so releasing it here would be a behaviour the C++
    /// module does not have.
    #[test]
    fn the_v2h_schedule_outlives_the_discharge_that_installed_it() {
        let mut tree = v2h_tree();

        // The ordinary derived budget, before the mode is ever active.
        assert_eq!(import_power_w(&mut tree, true), 100_000.0);

        tree.note_sae_bidi_active();
        assert_eq!(import_power_w(&mut tree, true), 0.0);

        // Everything the tree can still be told after the mode went active.
        // None of it is a release path in the C++, so none of it is one here.
        tree.note_supply_capabilities(supply());
        tree.note_supply_voltage(400.0);
        tree.note_pp_ampacity(32.0);

        assert_eq!(import_power_w(&mut tree, true), 0.0);
    }

    /// A port that is not configured for vehicle to home takes no part in the
    /// rewrite even when the bidirectional flag goes up: the C++ calls
    /// `setup_v2h_mode` only on the `V2H` arm of `EvseManager.cpp:934-940`.
    #[test]
    fn a_v2g_port_keeps_its_ordinary_import_budget() {
        let mut tree = tree(ChargeMode::Dc);
        tree.note_supply_capabilities(supply());
        tree.note_sae_bidi_active();
        assert_eq!(import_power_w(&mut tree, true), 100_000.0);
    }

    #[test]
    fn active_v2h_forbids_grid_import_and_allows_house_export() {
        let mut tree = EnergyTree::new(
            super::super::NodeUuid::from_module_id("evse"),
            EnergyConfig {
                charge_mode: ChargeMode::Dc,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: true,
                request_zero_power_in_idle: true,
            },
            super::super::random_delay::boot_defaults(),
        );
        tree.note_supply_capabilities(supply());
        tree.note_sae_bidi_active();
        let request = tree.flow_request(super::super::Publish {
            charger_state: crate::core::path::iec::AcState::Charging,
            bidirectional: true,
            priority: false,
        });
        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_leaves
                .total_power_w
                .as_ref()
                .unwrap()
                .value,
            0.0
        );
        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .total_power_w
                .as_ref()
                .unwrap()
                .value,
            80_000.0
        );
    }
}
