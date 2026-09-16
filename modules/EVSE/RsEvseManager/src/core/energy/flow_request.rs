// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The wire shape of an energy flow request, and the schedule that carries it.
//!
//! `types/energy.yaml` in Rust, narrowed to the fields
//! `energy_grid/energyImpl.cpp` fills. What it leaves out is stated at each
//! omission rather than implied by absence.

use super::NodeUuid;

/// A value and where it came from (`types::energy::NumberWithSource`).
///
/// Both fields are required on the wire, so neither is optional here. The
/// source is what a reader of an enforced limit traces back to the limit that
/// produced it, and the C++ leaves it empty at a handful of sites; those keep
/// their empty string rather than gaining an invented one.
#[derive(Clone, Debug, PartialEq)]
pub struct NumberWithSource {
    pub value: f64,
    pub source: String,
}

impl NumberWithSource {
    pub fn new(value: f64, source: impl Into<String>) -> Self {
        Self {
            value,
            source: source.into(),
        }
    }
}

/// `types::energy::IntegerWithSource`. Its own type rather than a generic, so a
/// phase count and an ampere figure cannot be assigned to each other.
#[derive(Clone, Debug, PartialEq)]
pub struct IntegerWithSource {
    pub value: i64,
    pub source: String,
}

impl IntegerWithSource {
    pub fn new(value: i64, source: impl Into<String>) -> Self {
        Self {
            value,
            source: source.into(),
        }
    }
}

/// `types::energy::LimitsReq`. Every field is optional on the wire and optional
/// here: the meaning of an absent limit is "not limited by this node", which a
/// zero would not express.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct LimitsReq {
    pub total_power_w: Option<NumberWithSource>,
    pub ac_max_current_a: Option<NumberWithSource>,
    pub ac_min_current_a: Option<NumberWithSource>,
    pub ac_max_phase_count: Option<IntegerWithSource>,
    pub ac_min_phase_count: Option<IntegerWithSource>,
    pub ac_supports_changing_phases_during_charging: Option<bool>,
    pub ac_number_of_active_phases: Option<i64>,
}

/// When a schedule entry is stamped.
///
/// `core` reads no clock, so the entry names which of the two wall clock
/// readings the C++ takes and the boundary renders it. There are exactly two:
/// the clear helpers floor to the hour
/// (`energy_grid/energyImpl.cpp:57-59` and `:84-86`) and the local limit
/// helpers stamp the moment (`EvseManager.cpp:1649`, `:1663`, `:1677`).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EntryTime {
    /// The current hour, truncated. The first entry of a schedule is active
    /// immediately whatever its timestamp says, so a stamp in the recent past
    /// is what the C++ chose.
    TopOfHour,
    Now,
}

/// `types::energy::ScheduleReqEntry`.
///
/// `price_per_kwh` is not carried: no site in `modules/EVSE/EvseManager/` fills
/// it, so a field here would always be absent.
#[derive(Clone, Debug, PartialEq)]
pub struct ScheduleReqEntry {
    pub timestamp: EntryTime,
    pub limits_to_root: LimitsReq,
    pub limits_to_leaves: LimitsReq,
    pub conversion_efficiency: Option<f64>,
}

impl ScheduleReqEntry {
    pub fn new(timestamp: EntryTime) -> Self {
        Self {
            timestamp,
            limits_to_root: LimitsReq::default(),
            limits_to_leaves: LimitsReq::default(),
            conversion_efficiency: None,
        }
    }
}

/// A schedule that cannot be empty.
///
/// The C++ idle branch indexes `schedule_import[0]` and `schedule_export[0]`
/// with no empty check (`energy_grid/energyImpl.cpp:301-308`). That is safe
/// only because the two clear helpers run unconditionally just above it and
/// leave exactly one entry each, which is a silent invariant rather than a
/// checked one. Here the first entry is a field, so the same access is total
/// and no future edit can produce a schedule with nothing in it.
#[derive(Clone, Debug, PartialEq)]
pub struct Schedule {
    first: ScheduleReqEntry,
    /// Entries after the first. Always empty today: the only producer of a
    /// multi entry schedule in the C++ is an externally set limit, and nothing
    /// sets one here (see `EnergyTree::local_energy_limits`). Present so the
    /// capping loops below are written per entry, as the C++ writes them,
    /// rather than being written for one entry and needing rework later.
    rest: Vec<ScheduleReqEntry>,
}

impl Schedule {
    pub fn new(first: ScheduleReqEntry) -> Self {
        Self {
            first,
            rest: Vec::new(),
        }
    }

    /// The entry the idle branch writes. Total, which is the whole point of
    /// the type.
    pub fn first_mut(&mut self) -> &mut ScheduleReqEntry {
        &mut self.first
    }

    pub fn first(&self) -> &ScheduleReqEntry {
        &self.first
    }

    pub fn entries_mut(&mut self) -> impl Iterator<Item = &mut ScheduleReqEntry> {
        std::iter::once(&mut self.first).chain(self.rest.iter_mut())
    }

    pub fn entries(&self) -> impl Iterator<Item = &ScheduleReqEntry> {
        std::iter::once(&self.first).chain(self.rest.iter())
    }
}

/// `types::energy::NodeType`. This module is always an EVSE leaf; the other two
/// values exist so the type says what the wire says.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum NodeType {
    Undefined,
    Evse,
    Generic,
}

/// `types::energy::EvseState`, the simplified state the energy manager sees.
///
/// `WaitForEnergy` is on the wire and is never produced: `to_energy_evse_state`
/// (`energy_grid/energyImpl.cpp:140-180`) has no arm that returns it, and this
/// port has no arm either. It is named so the type is the wire's type.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EvseState {
    Unplugged,
    WaitForAuth,
    WaitForEnergy,
    PrepareCharging,
    PausedEv,
    PausedEvse,
    Charging,
    Finished,
    Disabled,
}

/// The one figure a setpoint entry carries, in the spelling its charge mode
/// uses. `types::energy::SetpointType` declares both fields as optional and
/// this node fills exactly one of them, so the choice is the type.
///
/// Positive is charging and negative is discharging, per the sign convention
/// documented on the wire type at `types/energy.yaml:159-170`.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum SetpointValue {
    AcCurrent(f64),
    TotalPower(f64),
}

/// One entry of `schedule_setpoints`.
#[derive(Clone, Debug, PartialEq)]
pub struct ScheduleSetpointEntry {
    pub timestamp: EntryTime,
    pub priority: i64,
    pub source: String,
    pub value: SetpointValue,
}

/// `types::energy::EnergyFlowRequest` as this node fills it.
///
/// Two wire fields are not carried:
///
/// - `children`, always empty for a leaf.
/// - `optimizer_target`, which the C++ declares and never fills; the two lines
///   that would are commented out at `energy_grid/energyImpl.hpp:52-53`.
///
/// `energy_usage_root` and `energy_usage_leaves` are not on this struct and are
/// carried all the same. They are whole `types::powermeter::Powermeter`
/// records, which `core` cannot name, and the C++ does not derive them either:
/// `energyImpl::init` keeps one per requirement slot and each subscription
/// simply replaces its field (`energy_grid/energyImpl.cpp:42-60`). So the
/// boundary holds them, beside the billing record it already holds for the
/// same reason, and fills them when it renders a request. See `EnergyUsage` in
/// `main.rs`.
#[derive(Clone, Debug, PartialEq)]
pub struct FlowRequest {
    /// The node identity, which is the same value for the life of the node.
    /// It is a `NodeUuid` rather than a `String` because the enforce limits
    /// handler is gated on it; see `super::NodeUuid`.
    pub uuid: NodeUuid,
    pub node_type: NodeType,
    pub priority_request: bool,
    pub evse_state: EvseState,
    pub schedule_import: Schedule,
    pub schedule_export: Schedule,
    pub schedule_setpoints: Vec<ScheduleSetpointEntry>,
}
