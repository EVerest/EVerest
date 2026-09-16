// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! What this EVSE asks the energy manager for.
//!
//! `modules/EVSE/EvseManager/energy_grid/energyImpl.cpp`: a request rebuilt
//! once a second and on two state transitions, carrying what the hardware can
//! do, what the power supply can do, and what the port is currently doing.
//!
//! The request is not rebuilt from nothing. In the C++ it is a member
//! (`energyImpl.hpp:54`) whose identity, node type and meter records outlive
//! every publish, and only the two schedules, the state and the priority flag
//! are rebuilt. That split is what `EnergyTree` is: the identity lives on the
//! struct, and the builder takes `&self`, so no publish can write it.

pub mod enforce;
pub mod flow_request;
pub mod random_delay;

use flow_request::{
    EntryTime, EvseState, FlowRequest, IntegerWithSource, LimitsReq, NodeType, NumberWithSource,
    Schedule, ScheduleReqEntry, ScheduleSetpointEntry,
};

use crate::core::config::ChargeMode;
use crate::core::effect::TimerId;
use crate::core::event::{HardwareCapabilities, PowerSupplyCapabilities};
use crate::core::path::iec::AcState;

/// The repeating publish. `energyImpl.cpp:122-128` runs a detached thread that
/// publishes and then sleeps a second; here the loop is the timer being rearmed
/// by the publish it woke.
pub const TIMER_ENERGY_FLOW_REQUEST: TimerId = TimerId(400);

/// How long the budget a grant carried stays usable, `EnforcedLimits::valid_for`
/// as `max_current_valid_until` (`Charger.cpp:1389`). The C++ notices the
/// overrun lazily, on the next read of `power_available()` in its polling loop;
/// here the deadline is the notice, armed by the grant that carried it.
pub const TIMER_BUDGET_VALIDITY: TimerId = TimerId(402);

/// What the idle branch names as the source of its zero (`energyImpl.cpp:302`).
const IDLE_SOURCE: &str = "Idle";

/// How often the request is published, `energyImpl.cpp:126`.
pub const PUBLISH_INTERVAL: std::time::Duration = std::time::Duration::from_secs(1);

/// This node's identity in the energy tree.
///
/// Load bearing rather than decorative. `handle_enforce_limits` is gated on
/// `value.uuid == energy_flow_request.uuid` (`energyImpl.cpp:386`), and that
/// gate wraps the whole handler with no else branch and no logging: a node that
/// minted a fresh identity per publish would have every enforced limit the
/// energy manager sends back silently discarded.
///
/// So the identity is a type with one constructor and no setter, and the
/// constructor is the identity function on the module id, exactly as
/// `energyImpl::init` fills it (`:33`). Deriving it twice cannot produce two
/// values.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct NodeUuid(String);

impl NodeUuid {
    /// `mod->info.id`. The module id is unique per configured module instance,
    /// which is what makes two EVSEs on one station two nodes.
    pub fn from_module_id(module_id: &str) -> Self {
        Self(module_id.to_owned())
    }

    pub fn as_str(&self) -> &str {
        &self.0
    }
}

/// What configuration decides about the request.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct EnergyConfig {
    pub charge_mode: ChargeMode,
    pub ac_nominal_voltage_v: f64,
    pub sae_v2h: bool,
    /// `request_zero_power_in_idle`. When off, the port asks for its full
    /// budget even with no vehicle attached, which is the `!` term in the
    /// branch at `energyImpl.cpp:189`.
    pub request_zero_power_in_idle: bool,
}

/// The three source strings the C++ builds once in `init` (`:29-31`), plus the
/// ones it formats at their use sites.
struct Sources {
    base: String,
    bsp_caps: String,
    psu_caps: String,
}

impl Sources {
    fn new(uuid: &NodeUuid) -> Self {
        let base = uuid.as_str().to_owned();
        Self {
            bsp_caps: format!("{base}/evse_board_support_caps"),
            psu_caps: format!("{base}/powersupply_dc_caps"),
            base,
        }
    }
}

/// What the port asks for, and everything it asks for it with.
pub struct EnergyTree {
    /// Written once, by `new`. Every method below takes `&self` or writes some
    /// other field, so the compiler is what keeps this stable.
    uuid: NodeUuid,
    sources: Sources,
    config: EnergyConfig,
    /// `hw_caps`, refreshed before each publish in the C++ (`:125`) and on
    /// arrival here, which is the same value at the same instants.
    hw: HardwareCapabilities,
    /// `get_powersupply_capabilities()`. Seeded with the C++ boot seed so a DC
    /// port that never hears from its supply asks for nothing rather than for
    /// an uninitialized figure.
    supply: PowerSupplyCapabilities,
    /// `bsp->read_pp_ampacity()`, which reports `nullopt` for a zero reading
    /// (`IECStateMachine.cpp:454-459`). Absent means the cable imposes no cap.
    pp_ampacity: Option<f64>,
    /// `ac_nr_phases_active` (`EvseManager.hpp:294`), derived from the board
    /// support capability report at `EvseManager.cpp:253-259`.
    active_phases: i64,
    /// `evse_managerImpl::limits.nr_of_phases_available` (`:49`), which is not
    /// `active_phases`: the publish is guarded on a range and the working count
    /// is not. See `enforce`.
    published_phases: i64,
    actual_voltage_v: f64,
    last_dc_inputs: Option<enforce::DcInputs>,
    last_setpoint: Option<flow_request::SetpointValue>,
    /// Whether `setup_v2h_mode`'s schedule has been installed. This is the
    /// C++ `external_local_energy_limits`, whose only writer is
    /// `setup_v2h_mode`, so once installed it outlives the discharge and the
    /// session. `sae_bidi_active`, which the current demand finished callback
    /// does clear, lives on `Bpt`.
    v2h_schedule_installed: bool,
    /// The UK smart charging random delay, which lives here because the whole
    /// of its effect is on the enforced limit this tree applies
    /// (`energyImpl.cpp:448-501`).
    random_delay: random_delay::RandomDelay,
}

impl EnergyTree {
    pub fn new(
        uuid: NodeUuid,
        config: EnergyConfig,
        random_delay: random_delay::RandomDelay,
    ) -> Self {
        Self {
            sources: Sources::new(&uuid),
            uuid,
            config,
            hw: HardwareCapabilities::default(),
            supply: PowerSupplyCapabilities::sane_default(),
            pp_ampacity: None,
            active_phases: 0,
            published_phases: 1,
            actual_voltage_v: 0.0,
            last_dc_inputs: None,
            last_setpoint: None,
            v2h_schedule_installed: false,
            random_delay,
        }
    }

    /// The random delay, for the four `uk_random_delay` commands. They change
    /// state and publish nothing, which is what their C++ handlers do
    /// (`random_delay/uk_random_delayImpl.cpp:15-31`): the next enforced limit
    /// is what observes the write.
    pub fn random_delay_mut(&mut self) -> &mut random_delay::RandomDelay {
        &mut self.random_delay
    }

    /// `EvseManager.cpp:246-261`. The active phase count follows the report:
    /// unset it takes the import maximum, and a maximum that dropped below it
    /// takes the import minimum.
    pub fn note_capabilities(&mut self, caps: HardwareCapabilities) {
        self.hw = caps;
        if self.active_phases == 0 {
            self.active_phases = caps.max_phase_count_import;
        }
        if self.active_phases > caps.max_phase_count_import {
            self.active_phases = caps.min_phase_count_import;
        }
        // `EvseManager.cpp:261` signals the count straight out of the
        // capability handler, so the published figure is the board's from boot
        // and an enforced limit finds it already set. That emission itself is
        // not ported: this port publishes its limits only from the enforced
        // limits handler, so a capability report alone republishes nothing and
        // the next enforced limit carries the new count. What is ported is
        // which count the publish is allowed to carry; see `enforce`.
        if (1..=3).contains(&self.active_phases) {
            self.published_phases = self.active_phases;
        }
    }

    pub fn note_supply_capabilities(&mut self, caps: PowerSupplyCapabilities) {
        self.supply = caps;
    }

    pub fn note_supply_voltage(&mut self, voltage_v: f64) {
        self.actual_voltage_v = voltage_v;
    }

    pub fn note_sae_bidi_active(&mut self) {
        if self.config.sae_v2h {
            self.v2h_schedule_installed = true;
        }
    }

    /// The proximity pilot rating. Zero is how the board reports no cable
    /// rating at all, and the C++ reader turns that into no cap rather than
    /// into a cap of zero amperes.
    pub fn note_pp_ampacity(&mut self, ampacity_a: f64) {
        self.pp_ampacity = (ampacity_a > 0.0).then_some(ampacity_a);
    }

    /// `request_energy_from_energy_manager` (`energyImpl.cpp:182-322`).
    ///
    /// Takes `&self`: a publish reads the node, it does not rewrite it.
    pub fn flow_request(&self, publish: Publish) -> FlowRequest {
        let mut schedule_import = self.clear_import_request_schedule();
        let mut schedule_export = self.clear_export_request_schedule();

        if needs_energy(
            publish.charger_state,
            self.config.request_zero_power_in_idle,
        ) {
            let local = self.local_energy_limits(publish.bidirectional);

            // `energyImpl.cpp:192-208`. The locally derived schedule replaces
            // the cleared one whole, which is why nothing the clear helpers put
            // on the leaves side survives here.
            schedule_import = local.import;
            if self.config.charge_mode == ChargeMode::Dc {
                cap_leaves_power(
                    &mut schedule_import,
                    self.supply.max_export_power_w,
                    &self.sources.psu_caps,
                );
            }
            self.apply_root_import_limits(&mut schedule_import, publish.charger_state);

            schedule_export = local.export;
            if self.config.charge_mode == ChargeMode::Dc {
                // Only capped when the supply named an import figure at all;
                // a supply that named none imposes no ceiling of its own
                // (`energyImpl.cpp:240-247`).
                //
                // The source names the board support report rather than the
                // power supply one, which is the one place the C++ pair
                // disagrees (`:245` against `:203`). Preserved: the source is
                // read by whoever traces an enforced limit back, and inventing
                // the consistent answer would name a limit the C++ does not.
                if let Some(max_import_power_w) = self.supply.max_import_power_w {
                    cap_leaves_power(
                        &mut schedule_export,
                        max_import_power_w,
                        &self.sources.bsp_caps,
                    );
                }
            }
            self.apply_root_export_limits(&mut schedule_export, publish.charger_state);

            if self.config.charge_mode == ChargeMode::Ac {
                self.cap_by_cable_rating(&mut schedule_import);
            }

            if self.config.charge_mode == ChargeMode::Dc {
                // `energyImpl.cpp:290-299`. An ampere limit on the leaves side
                // of a DC port has no meaning, and the zero discharge limit
                // above sets one on the export schedule, so it is taken off
                // again here rather than left to be read as a per phase limit.
                for entry in schedule_import
                    .entries_mut()
                    .chain(schedule_export.entries_mut())
                {
                    entry.limits_to_leaves.ac_max_current_a = None;
                }
            }
        } else {
            // `energyImpl.cpp:300-309`. Nothing is being charged, so the port
            // asks for nothing, in the unit its mode speaks: watts on a DC
            // port, amperes per phase on an AC one.
            let idle = |entry: &mut ScheduleReqEntry| match self.config.charge_mode {
                ChargeMode::Dc => {
                    entry.limits_to_leaves.total_power_w =
                        Some(NumberWithSource::new(0.0, IDLE_SOURCE))
                }
                ChargeMode::Ac => {
                    entry.limits_to_leaves.ac_max_current_a =
                        Some(NumberWithSource::new(0.0, IDLE_SOURCE))
                }
            };
            idle(schedule_import.first_mut());
            idle(schedule_export.first_mut());
        }

        FlowRequest {
            uuid: self.uuid.clone(),
            node_type: NodeType::Evse,
            priority_request: publish.priority,
            evse_state: energy_evse_state(publish.charger_state),
            schedule_import,
            schedule_export,
            schedule_setpoints: publish
                .bidirectional
                .then_some(self.last_setpoint)
                .flatten()
                .map(|value| ScheduleSetpointEntry {
                    timestamp: EntryTime::Now,
                    priority: 0,
                    source: self.sources.base.clone(),
                    value,
                })
                .into_iter()
                .collect(),
        }
    }

    /// `EvseManager::get_local_energy_limits` (`EvseManager.cpp:2503-2529`),
    /// the budget the port asks for before any hardware ceiling is applied.
    ///
    /// The C++ takes externally set limits when it has any and derives them
    /// from its own capabilities when it does not. Its only remaining writer
    /// is `setup_v2h_mode` (`EvseManager.cpp:1619-1636`), represented by the
    /// first branch below; the removed general command has no input to port.
    fn local_energy_limits(&self, bidirectional: bool) -> LocalLimits {
        let mut import = ScheduleReqEntry::new(EntryTime::Now);
        let mut export = ScheduleReqEntry::new(EntryTime::Now);

        if self.config.charge_mode == ChargeMode::Dc && self.v2h_schedule_installed {
            let source = format!("{}/setup_v2h_mode", self.sources.base);
            import.limits_to_leaves.total_power_w =
                Some(NumberWithSource::new(0.0, String::new()));
            export.limits_to_leaves.total_power_w = Some(NumberWithSource::new(
                self.supply.max_import_power_w.unwrap_or(0.0),
                source,
            ));
            return LocalLimits {
                import: Schedule::new(import),
                export: Schedule::new(export),
            };
        }

        match self.config.charge_mode {
            // `update_max_current_limit` (`EvseManager.cpp:1670-1686`), which
            // refuses to run at all on a DC port.
            ChargeMode::Ac => {
                let source = format!("{} update_max_current_limit", self.sources.base);
                import.limits_to_leaves.ac_max_current_a = Some(NumberWithSource::new(
                    self.hw.max_current_a_import,
                    source.clone(),
                ));
                export.limits_to_leaves.ac_max_current_a =
                    Some(NumberWithSource::new(self.hw.max_current_a_export, source));
            }
            // `update_max_watt_limit` (`EvseManager.cpp:1646-1658`). The same
            // crossing as the clear helpers: what the vehicle imports is what
            // the supply exports.
            ChargeMode::Dc => {
                let source = format!("{} update_max_watt_limit", self.sources.base);
                import.limits_to_leaves.total_power_w = Some(NumberWithSource::new(
                    self.supply.max_export_power_w,
                    source.clone(),
                ));
                export.limits_to_leaves.total_power_w = Some(NumberWithSource::new(
                    self.supply.max_import_power_w.unwrap_or(0.0),
                    source,
                ));
            }
        }

        if !bidirectional {
            // `update_to_zero_discharge_limit` (`EvseManager.cpp:1660-1667`).
            // A session that did not resolve to bidirectional may not push
            // power back, whatever the hardware could do, so the export budget
            // is replaced rather than reduced.
            let source = format!("{} set_zero_discharge_limit", self.sources.base);
            export = ScheduleReqEntry::new(EntryTime::Now);
            export.limits_to_leaves.ac_max_current_a =
                Some(NumberWithSource::new(0.0, source.clone()));
            export.limits_to_leaves.total_power_w = Some(NumberWithSource::new(0.0, source));
        }

        LocalLimits {
            import: Schedule::new(import),
            export: Schedule::new(export),
        }
    }

    /// `energyImpl.cpp:210-233`, the hardware ceiling on the root side of the
    /// import schedule.
    ///
    /// The maximum is only lowered, never raised: an entry already asking for
    /// less than the board can do keeps its own figure. The three below it are
    /// overwritten unconditionally, because they describe the board rather than
    /// the request.
    fn apply_root_import_limits(&self, schedule: &mut Schedule, charger_state: AcState) {
        for entry in schedule.entries_mut() {
            let root = &mut entry.limits_to_root;
            if root
                .ac_max_current_a
                .as_ref()
                .is_none_or(|limit| limit.value > self.hw.max_current_a_import)
            {
                root.ac_max_current_a = Some(NumberWithSource::new(
                    self.hw.max_current_a_import,
                    &self.sources.bsp_caps,
                ));

                if charger_state == AcState::ChargingPausedEv
                    && self.config.request_zero_power_in_idle
                {
                    root.ac_max_current_a = Some(NumberWithSource::new(
                        self.hw.min_current_a_import,
                        &self.sources.bsp_caps,
                    ));
                }
            }

            if root
                .ac_max_phase_count
                .as_ref()
                .is_none_or(|count| count.value > self.hw.max_phase_count_import)
            {
                root.ac_max_phase_count = Some(IntegerWithSource::new(
                    self.hw.max_phase_count_import,
                    &self.sources.bsp_caps,
                ));
            }

            root.ac_min_phase_count = Some(IntegerWithSource::new(
                self.hw.min_phase_count_import,
                &self.sources.bsp_caps,
            ));
            root.ac_min_current_a = Some(NumberWithSource::new(
                self.hw.min_current_a_import,
                &self.sources.bsp_caps,
            ));
            root.ac_supports_changing_phases_during_charging =
                Some(self.hw.supports_changing_phases_during_charging);
            root.ac_number_of_active_phases = Some(self.active_phases);
        }
    }

    /// `energyImpl.cpp:252-276`, the export twin of the loop above.
    ///
    /// Two deliberate differences from it, both in the C++: the pause reduction
    /// is not gated on `request_zero_power_in_idle`, and it names its own
    /// source so a reduced discharge budget is traceable to the pause rather
    /// than to the board report.
    fn apply_root_export_limits(&self, schedule: &mut Schedule, charger_state: AcState) {
        for entry in schedule.entries_mut() {
            let root = &mut entry.limits_to_root;
            if root
                .ac_max_current_a
                .as_ref()
                .is_none_or(|limit| limit.value > self.hw.max_current_a_export)
            {
                root.ac_max_current_a = Some(NumberWithSource::new(
                    self.hw.max_current_a_export,
                    &self.sources.bsp_caps,
                ));

                if charger_state == AcState::ChargingPausedEv {
                    root.ac_max_current_a = Some(NumberWithSource::new(
                        self.hw.min_current_a_export,
                        format!("{}_pause", self.sources.bsp_caps),
                    ));
                }
            }

            if root
                .ac_max_phase_count
                .as_ref()
                .is_none_or(|count| count.value > self.hw.max_phase_count_export)
            {
                root.ac_max_phase_count = Some(IntegerWithSource::new(
                    self.hw.max_phase_count_export,
                    &self.sources.bsp_caps,
                ));
            }

            root.ac_min_phase_count = Some(IntegerWithSource::new(
                self.hw.min_phase_count_export,
                &self.sources.bsp_caps,
            ));
            root.ac_min_current_a = Some(NumberWithSource::new(
                self.hw.min_current_a_export,
                &self.sources.bsp_caps,
            ));
            root.ac_supports_changing_phases_during_charging =
                Some(self.hw.supports_changing_phases_during_charging);
            root.ac_number_of_active_phases = Some(self.active_phases);
        }
    }

    /// `energyImpl.cpp:277-288`. The cable is the last thing between the
    /// station and the vehicle, so nothing may be allocated above its rating.
    fn cap_by_cable_rating(&self, schedule: &mut Schedule) {
        let Some(rating_a) = self.pp_ampacity else {
            return;
        };
        let source = format!("{}/pp_ampacity", self.sources.base);
        for entry in schedule.entries_mut() {
            let root = &mut entry.limits_to_root;
            if root
                .ac_max_current_a
                .as_ref()
                .is_some_and(|limit| limit.value > rating_a)
            {
                root.ac_max_current_a = Some(NumberWithSource::new(rating_a, &source));
            }
        }
    }

    /// `clear_import_request_schedule` (`energyImpl.cpp:55-80`).
    ///
    /// The four root side figures carry no source, because the C++ brace
    /// initializes them with the value alone while its export twin names
    /// `source_bsp_caps` on all four. Preserved rather than tidied: a source
    /// this port invented would name a limit the C++ does not name.
    fn clear_import_request_schedule(&self) -> Schedule {
        let mut entry = ScheduleReqEntry::new(EntryTime::TopOfHour);
        entry.limits_to_root = LimitsReq {
            total_power_w: None,
            ac_max_current_a: Some(NumberWithSource::new(self.hw.max_current_a_import, "")),
            ac_min_current_a: Some(NumberWithSource::new(self.hw.min_current_a_import, "")),
            ac_max_phase_count: Some(IntegerWithSource::new(self.hw.max_phase_count_import, "")),
            ac_min_phase_count: Some(IntegerWithSource::new(self.hw.min_phase_count_import, "")),
            ac_supports_changing_phases_during_charging: Some(
                self.hw.supports_changing_phases_during_charging,
            ),
            ac_number_of_active_phases: Some(self.active_phases),
        };

        if self.config.charge_mode == ChargeMode::Dc {
            // The import schedule is capped by the *export* power figure. Not a
            // transposition: energy flowing to the vehicle is what the supply
            // exports, and the C++ crosses the two names for that reason
            // (`:70-77`).
            //
            // No reader ever sees this figure, in the C++ either: the idle
            // branch overwrites it with zero and the active branch replaces the
            // whole entry with the derived one. It is written because the
            // helper is what the two branches start from, and the efficiency
            // beside it is read. Ported rather than dropped for the same
            // reason: the day an entry survives to a reader, it should carry
            // what the C++ put there.
            entry.limits_to_leaves.total_power_w = Some(NumberWithSource::new(
                self.supply.max_export_power_w,
                format!("{}/clear_import_request_schedule", self.sources.base),
            ));
            entry.conversion_efficiency = self.supply.conversion_efficiency_export;
        }

        Schedule::new(entry)
    }

    /// `clear_export_request_schedule` (`energyImpl.cpp:82-106`).
    fn clear_export_request_schedule(&self) -> Schedule {
        let mut entry = ScheduleReqEntry::new(EntryTime::TopOfHour);
        entry.limits_to_root = LimitsReq {
            total_power_w: None,
            ac_max_current_a: Some(NumberWithSource::new(
                self.hw.max_current_a_export,
                &self.sources.bsp_caps,
            )),
            ac_min_current_a: Some(NumberWithSource::new(
                self.hw.min_current_a_export,
                &self.sources.bsp_caps,
            )),
            ac_max_phase_count: Some(IntegerWithSource::new(
                self.hw.max_phase_count_export,
                &self.sources.bsp_caps,
            )),
            ac_min_phase_count: Some(IntegerWithSource::new(
                self.hw.min_phase_count_export,
                &self.sources.bsp_caps,
            )),
            ac_supports_changing_phases_during_charging: Some(
                self.hw.supports_changing_phases_during_charging,
            ),
            ac_number_of_active_phases: Some(self.active_phases),
        };

        if self.config.charge_mode == ChargeMode::Dc {
            // The export schedule is capped by the *import* power figure, the
            // other half of the crossing above, and unobservable for the same
            // reason as its twin: the idle branch overwrites it with zero and
            // the active branch replaces the entry. The efficiency beside it is
            // read.
            entry.limits_to_leaves.total_power_w = Some(NumberWithSource::new(
                self.supply.max_import_power_w.unwrap_or(0.0),
                &self.sources.psu_caps,
            ));
            entry.conversion_efficiency = self.supply.conversion_efficiency_import;
        }

        Schedule::new(entry)
    }
}

/// The two schedules the local budget is expressed in, neither of which can be
/// empty.
///
/// The C++ checks both for emptiness before it uses them
/// (`energyImpl.cpp:193` and `:236`), because its `ExternalLimits` is a pair of
/// vectors that a caller may leave unfilled. Here the type says what those two
/// checks were guarding, so there is no branch to get wrong and no path on
/// which the cleared schedule silently survives.
struct LocalLimits {
    import: Schedule,
    export: Schedule,
}

/// The power supply ceiling on the leaves side (`energyImpl.cpp:198-206` and
/// `:241-247`).
///
/// Applied per entry and only downward, so an entry asking for less than the
/// supply can do keeps its own figure and its own source.
///
/// Nothing this port produces exceeds the ceiling today: the only leaves side
/// power figures come from `local_energy_limits`, which derives them from the
/// same capability report. It runs anyway, because the entries stop being
/// derived the moment external limits are ported, and a ceiling added later is
/// a ceiling nobody remembers to add.
fn cap_leaves_power(schedule: &mut Schedule, ceiling_w: f64, source: &str) {
    for entry in schedule.entries_mut() {
        let leaves = &mut entry.limits_to_leaves;
        if leaves
            .total_power_w
            .as_ref()
            .is_some_and(|power| power.value > ceiling_w)
        {
            leaves.total_power_w = Some(NumberWithSource::new(ceiling_w, source));
        }
    }
}

/// `to_energy_evse_state` (`energyImpl.cpp:140-180`), what the energy manager
/// is told this port is doing.
///
/// The C++ mapping is lossy in three places, and all three are preserved here
/// because the energy tree's reader is the one that has to keep behaving the
/// same:
///
/// - `StoppingCharging` and `Finished` both arrive as `Finished`, so a port
///   winding a charge down is indistinguishable from one that has finished.
/// - `T_step_EF` and `T_step_X1` both arrive as `PrepareCharging`. Neither is a
///   state in this port: the pilot detours are timers rather than states here,
///   and the state they run from is the one that gets reported, which is the
///   same answer for the same reason.
/// - `SwitchPhases` arrives as `Charging`, and this port does hold that state.
///   The break interrupts a charging session and reports as one, so an energy
///   manager sees no pause and no stop where there is neither.
///
/// `Startup` has no C++ counterpart and reports disabled, which is the value
/// `energyImpl::init` seeds its state with before any transition arrives
/// (`:26`).
fn energy_evse_state(charger_state: AcState) -> EvseState {
    match charger_state {
        AcState::Startup | AcState::Disabled => EvseState::Disabled,
        AcState::Idle => EvseState::Unplugged,
        AcState::WaitingForAuthentication => EvseState::WaitForAuth,
        // `energyImpl.cpp` reports the reinitialization as preparing to
        // charge, beside the two pilot detour states, which is what it is: the
        // session is on its way back into a charge it never left.
        AcState::PrepareCharging | AcState::Reinit => EvseState::PrepareCharging,
        // `energyImpl.cpp:175-177` reports the switching break as charging,
        // which is what the session it interrupts still is.
        AcState::Charging | AcState::SwitchPhases => EvseState::Charging,
        AcState::ChargingPausedEv => EvseState::PausedEv,
        AcState::ChargingPausedEvse => EvseState::PausedEvse,
        AcState::StoppingCharging | AcState::Finished => EvseState::Finished,
    }
}

/// Whether the port wants energy at all (`energyImpl.cpp:186-189`).
///
/// The four states are the ones that can consume: charging, getting ready to,
/// waiting for the driver to authorize, and paused by the vehicle, which is a
/// charge that can resume without a new decision. A port configured not to
/// request zero in idle wants energy in every state instead, which is the
/// deployment where many stations idling at zero would make the energy manager
/// re-plan constantly.
fn needs_energy(charger_state: AcState, request_zero_power_in_idle: bool) -> bool {
    matches!(
        charger_state,
        AcState::Charging
            | AcState::PrepareCharging
            | AcState::WaitingForAuthentication
            | AcState::ChargingPausedEv
    ) || !request_zero_power_in_idle
}

/// What one publish decides, as opposed to what the node holds.
///
/// A struct rather than three parameters: the charger state, the direction the
/// session resolved to and the priority flag are three unrelated facts, and two
/// of them are booleans.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Publish {
    pub charger_state: AcState,
    /// Whether the session may push power back. `get_local_energy_limits`
    /// forces the export schedule to zero unless it resolves true
    /// (`EvseManager.cpp:2522-2526`).
    pub bidirectional: bool,
    /// `energyImpl.cpp:311-315`. True for the first publish after the port
    /// comes up and for the two state transitions the C++ hooks; false for the
    /// periodic one.
    pub priority: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::event::{HardwareCapabilities, PowerSupplyCapabilities};

    fn tree(charge_mode: ChargeMode) -> EnergyTree {
        EnergyTree::new(
            NodeUuid::from_module_id("evse_manager"),
            EnergyConfig {
                charge_mode,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: true,
            },
            random_delay::boot_defaults(),
        )
    }

    fn idle(charger_state: AcState) -> Publish {
        Publish {
            charger_state,
            bidirectional: false,
            priority: false,
        }
    }

    /// A board whose two directions differ in every field.
    ///
    /// Deliberately not a realistic pair. The import and export halves are
    /// adjacent same typed figures, and a fixture that gave them equal values
    /// would let a loop reading the wrong half pass every assertion.
    fn ac_caps() -> HardwareCapabilities {
        HardwareCapabilities {
            max_current_a_import: 32.0,
            min_current_a_import: 6.0,
            max_phase_count_import: 3,
            min_phase_count_import: 1,
            max_current_a_export: 16.0,
            min_current_a_export: 4.0,
            max_phase_count_export: 2,
            min_phase_count_export: 2,
            supports_changing_phases_during_charging: false,
            supports_cp_state_e: false,
        }
    }

    fn charging(charger_state: AcState) -> Publish {
        Publish {
            charger_state,
            bidirectional: false,
            priority: false,
        }
    }

    #[test]
    fn an_ac_port_asks_for_what_its_board_can_import() {
        // `energyImpl.cpp:193-233`. The budget it wants sits on the leaves side
        // and the hardware ceiling on the root side, both from the board
        // report, and the root side names the report as its source.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());

        let request = ac.flow_request(charging(AcState::Charging));
        let entry = request.schedule_import.first();

        assert_eq!(
            entry.limits_to_leaves.ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager update_max_current_limit"
            ))
        );
        assert_eq!(
            entry.limits_to_root.ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            entry.limits_to_root.ac_min_current_a,
            Some(NumberWithSource::new(
                6.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            entry.limits_to_root.ac_max_phase_count,
            Some(IntegerWithSource::new(
                3,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            entry.limits_to_root.ac_min_phase_count,
            Some(IntegerWithSource::new(
                1,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            entry
                .limits_to_root
                .ac_supports_changing_phases_during_charging,
            Some(false)
        );
        // The board report is what first says how many phases are live
        // (`EvseManager.cpp:253-255`).
        assert_eq!(entry.limits_to_root.ac_number_of_active_phases, Some(3));
        assert_eq!(entry.timestamp, EntryTime::Now);
    }

    #[test]
    fn the_export_ceiling_comes_from_the_export_half_of_the_board_report() {
        // The two halves are adjacent same typed figures, so a loop reading the
        // import pair on the export schedule would compile and would offer the
        // grid more than the board can push.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());

        let request = ac.flow_request(charging(AcState::Charging));
        let root = &request.schedule_export.first().limits_to_root;

        assert_eq!(
            root.ac_max_current_a,
            Some(NumberWithSource::new(
                16.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            root.ac_min_current_a,
            Some(NumberWithSource::new(
                4.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            root.ac_max_phase_count,
            Some(IntegerWithSource::new(
                2,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            root.ac_min_phase_count,
            Some(IntegerWithSource::new(
                2,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_cable_rated_below_the_board_caps_what_the_ac_port_asks_for() {
        // `energyImpl.cpp:277-288`. The cap exists so the energy manager cannot
        // allocate more than the cable carries, and it is AC only.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        ac.note_pp_ampacity(20.0);

        let request = ac.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(20.0, "evse_manager/pp_ampacity"))
        );
    }

    #[test]
    fn a_cable_rated_above_the_board_caps_nothing() {
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        ac.note_pp_ampacity(63.0);

        let request = ac.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_board_reporting_no_cable_rating_caps_nothing() {
        // `IECStateMachine::read_pp_ampacity` reports nothing for a zero
        // reading, and the C++ cap is inside `if (pp_rating)`.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        ac.note_pp_ampacity(0.0);

        let request = ac.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_dc_port_never_reads_the_cable_rating() {
        // `energyImpl.cpp:277` gates the cap on `charge_mode == "AC"`, and a DC
        // port limits watts rather than amperes per phase anyway.
        let mut dc = tree(ChargeMode::Dc);
        dc.note_capabilities(ac_caps());
        dc.note_pp_ampacity(20.0);

        let request = dc.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_vehicle_paused_charge_asks_for_the_minimum_that_shows_it_resuming() {
        // `energyImpl.cpp:216-222`. Enough current to see the vehicle start
        // drawing again, and no more. The energy manager can still take it to
        // zero from outside.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());

        let request = ac.flow_request(charging(AcState::ChargingPausedEv));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                6.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_port_that_never_idles_at_zero_keeps_its_full_budget_through_a_pause() {
        // The pause reduction on the import side is gated on
        // `request_zero_power_in_idle` (`energyImpl.cpp:220`), while the export
        // side reduction below it is not. Two adjacent branches that do not
        // agree, preserved rather than harmonized.
        let mut ac = EnergyTree::new(
            NodeUuid::from_module_id("evse_manager"),
            EnergyConfig {
                charge_mode: ChargeMode::Ac,
                ac_nominal_voltage_v: 230.0,
                sae_v2h: false,
                request_zero_power_in_idle: false,
            },
            random_delay::boot_defaults(),
        );
        ac.note_capabilities(ac_caps());

        let request = ac.flow_request(charging(AcState::ChargingPausedEv));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_root
                .ac_max_current_a,
            Some(NumberWithSource::new(
                4.0,
                "evse_manager/evse_board_support_caps_pause"
            ))
        );
    }

    /// A supply that can push 30 kW out and pull 20 kW back.
    fn bidirectional_supply() -> PowerSupplyCapabilities {
        let mut caps = PowerSupplyCapabilities::sane_default();
        caps.bidirectional = true;
        caps.max_export_power_w = 30_000.0;
        caps.max_import_power_w = Some(20_000.0);
        caps
    }

    #[test]
    fn a_dc_port_asks_in_watts_for_what_its_supply_can_deliver() {
        // `EvseManager.cpp:1646-1658` through `energyImpl.cpp:194-208`. The
        // import schedule carries the export figure, the crossing this port
        // preserves throughout.
        let mut dc = tree(ChargeMode::Dc);
        dc.note_capabilities(ac_caps());
        dc.note_supply_capabilities(bidirectional_supply());

        let request = dc.flow_request(Publish {
            charger_state: AcState::Charging,
            bidirectional: true,
            priority: false,
        });

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_leaves
                .total_power_w,
            Some(NumberWithSource::new(
                30_000.0,
                "evse_manager update_max_watt_limit"
            ))
        );
        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .total_power_w,
            Some(NumberWithSource::new(
                20_000.0,
                "evse_manager update_max_watt_limit"
            ))
        );
    }

    #[test]
    fn a_session_that_may_not_discharge_asks_to_export_nothing() {
        // `EvseManager.cpp:2522-2526`. Whatever the supply can do, a session
        // that did not resolve to bidirectional asks for a zero export budget,
        // in both units so neither reader can miss it.
        let mut dc = tree(ChargeMode::Dc);
        dc.note_capabilities(ac_caps());
        dc.note_supply_capabilities(bidirectional_supply());

        let request = dc.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .total_power_w,
            Some(NumberWithSource::new(
                0.0,
                "evse_manager set_zero_discharge_limit"
            ))
        );
    }

    #[test]
    fn a_dc_port_asks_for_no_per_phase_current_at_all() {
        // `energyImpl.cpp:290-299`. The zero discharge limit above sets an
        // ampere figure on the export schedule, which means nothing on a DC
        // port, so it is taken off again.
        let mut dc = tree(ChargeMode::Dc);
        dc.note_capabilities(ac_caps());
        dc.note_supply_capabilities(bidirectional_supply());

        let request = dc.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .ac_max_current_a,
            None
        );
        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_leaves
                .ac_max_current_a,
            None
        );
    }

    #[test]
    fn an_ac_port_keeps_the_zero_discharge_limit_in_amperes() {
        // The clear above is DC only, so an AC port that may not discharge
        // still carries the zero it was given.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());

        let request = ac.flow_request(charging(AcState::Charging));

        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .ac_max_current_a,
            Some(NumberWithSource::new(
                0.0,
                "evse_manager set_zero_discharge_limit"
            ))
        );
    }

    /// An entry carrying a root side ceiling of its own, which is what an
    /// externally set limit would look like. Nothing produces one here yet, so
    /// the two tests below drive the step directly, the way the supply ceiling
    /// tests beside them do.
    fn entry_asking_for(current_a: f64, phase_count: i64) -> Schedule {
        let mut schedule = Schedule::new(ScheduleReqEntry::new(EntryTime::Now));
        let root = &mut schedule.first_mut().limits_to_root;
        root.ac_max_current_a = Some(NumberWithSource::new(current_a, "external"));
        root.ac_max_phase_count = Some(IntegerWithSource::new(phase_count, "external"));
        schedule
    }

    #[test]
    fn a_root_request_below_the_board_ceiling_keeps_its_own_figure() {
        // `energyImpl.cpp:211-213` and `:227-229` lower and never raise: an
        // entry asking for less than the board can do is a limit somebody
        // meant, and overwriting it with the board figure would hand the
        // energy manager more headroom than it was given.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        let mut schedule = entry_asking_for(10.0, 1);

        ac.apply_root_import_limits(&mut schedule, AcState::Charging);

        let root = &schedule.first().limits_to_root;
        assert_eq!(
            root.ac_max_current_a,
            Some(NumberWithSource::new(10.0, "external"))
        );
        assert_eq!(
            root.ac_max_phase_count,
            Some(IntegerWithSource::new(1, "external"))
        );
        // The four that describe the board rather than the request are
        // overwritten either way (`:225-232`).
        assert_eq!(
            root.ac_min_current_a,
            Some(NumberWithSource::new(
                6.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_root_request_above_the_board_ceiling_is_lowered_to_it() {
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        let mut schedule = entry_asking_for(63.0, 3);

        ac.apply_root_import_limits(&mut schedule, AcState::Charging);

        let root = &schedule.first().limits_to_root;
        assert_eq!(
            root.ac_max_current_a,
            Some(NumberWithSource::new(
                32.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        // The comparison is strict (`energyImpl.cpp:227`), so a request equal
        // to the board ceiling is not above it and keeps its own source. The
        // board can do three phases and the entry asked for three.
        assert_eq!(
            root.ac_max_phase_count,
            Some(IntegerWithSource::new(3, "external"))
        );
    }

    #[test]
    fn an_export_request_below_the_board_ceiling_keeps_its_own_figure() {
        // The export loop is a second copy of the same shape, so it needs its
        // own pin: a defect in one is invisible in the other.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        let mut schedule = entry_asking_for(5.0, 1);

        ac.apply_root_export_limits(&mut schedule, AcState::Charging);

        let root = &schedule.first().limits_to_root;
        assert_eq!(
            root.ac_max_current_a,
            Some(NumberWithSource::new(5.0, "external"))
        );
        assert_eq!(
            root.ac_max_phase_count,
            Some(IntegerWithSource::new(1, "external"))
        );
    }

    #[test]
    fn an_export_request_above_the_board_ceiling_is_lowered_to_it() {
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        let mut schedule = entry_asking_for(32.0, 3);

        ac.apply_root_export_limits(&mut schedule, AcState::Charging);

        let root = &schedule.first().limits_to_root;
        assert_eq!(
            root.ac_max_current_a,
            Some(NumberWithSource::new(
                16.0,
                "evse_manager/evse_board_support_caps"
            ))
        );
        assert_eq!(
            root.ac_max_phase_count,
            Some(IntegerWithSource::new(
                2,
                "evse_manager/evse_board_support_caps"
            ))
        );
    }

    #[test]
    fn a_paused_vehicle_holding_its_own_lower_ceiling_keeps_it() {
        // The pause reduction sits inside the lowering branch
        // (`energyImpl.cpp:216-222`), so an entry that was already below the
        // board ceiling is not reduced further. Reading the C++ as "a pause
        // always asks for the minimum" would be the wrong port.
        let mut ac = tree(ChargeMode::Ac);
        ac.note_capabilities(ac_caps());
        let mut schedule = entry_asking_for(10.0, 3);

        ac.apply_root_import_limits(&mut schedule, AcState::ChargingPausedEv);

        assert_eq!(
            schedule.first().limits_to_root.ac_max_current_a,
            Some(NumberWithSource::new(10.0, "external"))
        );
    }

    #[test]
    fn a_leaves_power_request_above_the_supply_ceiling_is_capped_to_it() {
        // Driven directly: no producer in this port asks for more than the
        // supply reported, because the figure it asks for is derived from that
        // same report. The step exists for the entries an external limit will
        // one day supply, so it is exercised the way one of those would.
        let mut schedule = Schedule::new(ScheduleReqEntry::new(EntryTime::Now));
        schedule.first_mut().limits_to_leaves.total_power_w =
            Some(NumberWithSource::new(50_000.0, "external"));

        cap_leaves_power(&mut schedule, 30_000.0, "psu");

        assert_eq!(
            schedule.first().limits_to_leaves.total_power_w,
            Some(NumberWithSource::new(30_000.0, "psu"))
        );
    }

    #[test]
    fn a_leaves_power_request_below_the_supply_ceiling_keeps_its_own_source() {
        // Conditional, not an overwrite: an entry asking for less than the
        // supply can do is not raised to the ceiling, and the source that
        // named the lower figure survives.
        let mut schedule = Schedule::new(ScheduleReqEntry::new(EntryTime::Now));
        schedule.first_mut().limits_to_leaves.total_power_w =
            Some(NumberWithSource::new(10_000.0, "external"));

        cap_leaves_power(&mut schedule, 30_000.0, "psu");

        assert_eq!(
            schedule.first().limits_to_leaves.total_power_w,
            Some(NumberWithSource::new(10_000.0, "external"))
        );
    }

    #[test]
    fn an_entry_asking_for_no_power_at_all_gains_no_ceiling() {
        // An absent limit means unlimited by this node, and the C++ cap is
        // inside `has_value()`, so the cap must not invent one.
        let mut schedule = Schedule::new(ScheduleReqEntry::new(EntryTime::Now));

        cap_leaves_power(&mut schedule, 30_000.0, "psu");

        assert_eq!(schedule.first().limits_to_leaves.total_power_w, None);
    }

    #[test]
    fn a_dc_port_with_no_vehicle_asks_for_zero_watts() {
        // `energyImpl.cpp:302-303`: the same idle branch on a DC port, in the
        // unit a DC port speaks. The ampere leaf stays absent, because a DC
        // port limiting amperes per phase would mean nothing.
        let request = tree(ChargeMode::Dc).flow_request(idle(AcState::Idle));

        for schedule in [&request.schedule_import, &request.schedule_export] {
            assert_eq!(
                schedule.first().limits_to_leaves.total_power_w,
                Some(NumberWithSource::new(0.0, "Idle"))
            );
            assert_eq!(schedule.first().limits_to_leaves.ac_max_current_a, None);
        }
    }

    /// The two conversion efficiencies cross the same way the two power figures
    /// do: what reaches the vehicle is what the supply exports, so the import
    /// schedule carries the export efficiency (`energyImpl.cpp:77` and `:104`).
    #[test]
    fn a_dc_schedule_carries_the_conversion_efficiency_of_its_own_direction() {
        let mut dc = tree(ChargeMode::Dc);
        let mut caps = PowerSupplyCapabilities::sane_default();
        caps.conversion_efficiency_export = Some(0.95);
        caps.conversion_efficiency_import = Some(0.9);
        dc.note_supply_capabilities(caps);

        let request = dc.flow_request(idle(AcState::Idle));

        assert_eq!(
            request.schedule_import.first().conversion_efficiency,
            Some(0.95)
        );
        assert_eq!(
            request.schedule_export.first().conversion_efficiency,
            Some(0.9)
        );
    }

    /// An AC port has no power supply to report one, so the field stays absent
    /// rather than defaulting to unity: the wire says an absent efficiency
    /// already means one.
    #[test]
    fn an_ac_schedule_carries_no_conversion_efficiency() {
        let request = tree(ChargeMode::Ac).flow_request(idle(AcState::Idle));

        assert_eq!(request.schedule_import.first().conversion_efficiency, None);
        assert_eq!(request.schedule_export.first().conversion_efficiency, None);
    }

    #[test]
    fn every_charger_state_reaches_the_energy_manager_as_its_own_simplified_state() {
        // `to_energy_evse_state` (`energyImpl.cpp:140-180`), one row per state
        // this port has. Written out rather than derived, so a new state is a
        // failing row rather than a silent fall through to disabled.
        for (charger_state, expected) in [
            (AcState::Startup, EvseState::Disabled),
            (AcState::Disabled, EvseState::Disabled),
            (AcState::Idle, EvseState::Unplugged),
            (AcState::WaitingForAuthentication, EvseState::WaitForAuth),
            (AcState::PrepareCharging, EvseState::PrepareCharging),
            (AcState::Charging, EvseState::Charging),
            (AcState::ChargingPausedEv, EvseState::PausedEv),
            (AcState::ChargingPausedEvse, EvseState::PausedEvse),
            // `energyImpl.cpp:175-177`. The break interrupts a charging
            // session and reports as one, not as a pause or a stop.
            (AcState::SwitchPhases, EvseState::Charging),
            (AcState::StoppingCharging, EvseState::Finished),
            (AcState::Finished, EvseState::Finished),
            // Beside the two pilot detour states, as `energyImpl.cpp` has it.
            (AcState::Reinit, EvseState::PrepareCharging),
        ] {
            let request = tree(ChargeMode::Ac).flow_request(charging(charger_state));
            assert_eq!(request.evse_state, expected, "{charger_state:?}");
        }
    }

    #[test]
    fn the_priority_flag_is_carried_from_the_publish_that_asked_for_it() {
        // `energyImpl.cpp:311-315`. A priority request asks the energy manager
        // to answer now rather than merging it into the next optimizer run.
        let ac = tree(ChargeMode::Ac);

        assert!(
            ac.flow_request(Publish {
                charger_state: AcState::WaitingForAuthentication,
                bidirectional: false,
                priority: true,
            })
            .priority_request
        );
        assert!(
            !ac.flow_request(charging(AcState::WaitingForAuthentication))
                .priority_request
        );
    }

    #[test]
    fn the_node_keeps_one_identity_for_its_whole_life() {
        // The enforce limits handler is gated on this value matching the one it
        // sent (`energyImpl.cpp:386`), with no else branch: a node that minted
        // a new identity per publish would have every enforced limit silently
        // discarded.
        let mut ac = tree(ChargeMode::Ac);
        let first = ac.flow_request(charging(AcState::Charging)).uuid;

        ac.note_capabilities(ac_caps());
        ac.note_pp_ampacity(20.0);
        ac.note_supply_capabilities(bidirectional_supply());
        let later = ac.flow_request(idle(AcState::Idle)).uuid;

        assert_eq!(first, later);
        assert_eq!(first, NodeUuid::from_module_id("evse_manager"));
    }

    #[test]
    fn the_node_is_an_evse_leaf() {
        assert_eq!(
            tree(ChargeMode::Ac)
                .flow_request(charging(AcState::Charging))
                .node_type,
            NodeType::Evse
        );
    }

    #[test]
    fn an_ac_port_with_no_vehicle_asks_for_zero_amperes() {
        // `energyImpl.cpp:305-306`: the idle branch of an AC port asks for no
        // current on the leaves side, in amperes rather than watts.
        let request = tree(ChargeMode::Ac).flow_request(idle(AcState::Idle));

        assert_eq!(
            request
                .schedule_import
                .first()
                .limits_to_leaves
                .ac_max_current_a,
            Some(NumberWithSource::new(0.0, "Idle"))
        );
        assert_eq!(
            request
                .schedule_export
                .first()
                .limits_to_leaves
                .ac_max_current_a,
            Some(NumberWithSource::new(0.0, "Idle"))
        );
    }
}
