// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The DC limit and present value port.
//!
//! What the module tells the ISO 15118 stack about the DC power supply: the
//! capability report itself, the physical setup values derived from it, the
//! minimum limits it implies, and the supply's live output. It also owns the
//! clamp the ISO 15118-20 dynamic control mode needs, because that clamp reads
//! the EVSE limit set this port derives.

use crate::core::derate::{Derate, ExternalDerating};
use crate::core::effect::{Effect, HlcUpdate};
use crate::core::event::{PowerSupplyCapabilities, PowermeterCapabilities};
use crate::core::powermeter_limits::{apply_powermeter_limits, MeterFloorsChanged};

/// `types::iso15118::SetupPhysicalValues`.
///
/// All four fields are optional on the wire and stay optional here, because the
/// two C++ call sites fill disjoint halves: the AC one sets `ac_nominal_voltage`
/// alone (`EvseManager.cpp:447-449`) and the DC one sets the three DC fields
/// alone (`EvseManager.hpp:275-279`). A struct with required fields could not
/// express either.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct PhysicalValues {
    pub ac_nominal_voltage_v: Option<f64>,
    pub dc_current_regulation_tolerance_a: Option<f64>,
    pub dc_peak_current_ripple_a: Option<f64>,
    pub dc_energy_to_be_delivered_wh: Option<f64>,
}

/// `types::iso15118::DcEvseMinimumLimits`. Three required fields and an
/// optional discharge pair, exactly as the wire type declares them.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MinimumLimits {
    pub minimum_current_a: f64,
    pub minimum_voltage_v: f64,
    pub minimum_power_w: f64,
    pub minimum_discharge_current_a: Option<f64>,
    pub minimum_discharge_power_w: Option<f64>,
}

/// `types::iso15118::DcEvseMaximumLimits`.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct MaximumLimits {
    pub maximum_current_a: f64,
    pub maximum_voltage_v: f64,
    pub maximum_power_w: f64,
    pub maximum_discharge_current_a: Option<f64>,
    pub maximum_discharge_power_w: Option<f64>,
}

/// The vehicle's ISO 15118-20 dynamic control mode request, narrowed to the
/// eight fields `subscribe_d20_dc_dynamic_charge_mode` reads
/// (`EvseManager.cpp:740-825`).
///
/// `types::iso15118::DcChargeDynamicModeValues` carries six more: the four
/// energy request figures, the V2X pair and the departure time. None is read by
/// that handler, and the ones the C++ acts on elsewhere arrive on their own
/// variables (`dc_ev_energy_request`, `departure_time`), which are not ported.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DynamicModeRequest {
    pub max_charge_power_w: f64,
    pub min_charge_power_w: f64,
    pub max_charge_current_a: f64,
    pub max_voltage_v: f64,
    pub min_voltage_v: f64,
    pub max_discharge_power_w: Option<f64>,
    pub min_discharge_power_w: Option<f64>,
    pub max_discharge_current_a: Option<f64>,
}

/// The maximum figures the vehicle reports for itself
/// (`subscribe_dc_ev_maximum_limits`, `EvseManager.cpp:851-870`).
///
/// All three wire fields are optional, and two of them are read: the current
/// and voltage maxima clamp the vehicle's own target
/// (`process_dc_ev_target_voltage_current`, `:2576-2586`) and the voltage
/// maximum is also an input to the cable check voltage derivation (`:2144-2152`).
/// The power maximum is not carried, because no reader in
/// `modules/EVSE/EvseManager/` reads it: the C++ stores it in `ev_info` for
/// republication and nothing else, and `ev_info` is not ported.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct EvMaximumLimits {
    pub maximum_current_a: Option<f64>,
    pub maximum_voltage_v: Option<f64>,
}

/// A DC voltage and current the supply is asked to deliver.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct DcTarget {
    pub voltage_v: f64,
    pub current_a: f64,
}

/// The energy the EVSE tells the vehicle it is prepared to deliver, in watt
/// hours. Hardcoded in the C++ (`EvseManager.hpp:278`) and hardcoded here, so
/// the port does not invent a configuration key the C++ manifest never had.
const ENERGY_TO_BE_DELIVERED_WH: f64 = 10_000.0;

/// The DC limit port state.
///
/// `capabilities` is the same member the C++ keeps (`EvseManager.hpp:316`,
/// written only by `update_powersupply_capabilities`), and it holds the supply's
/// report **raw**. `derate` is the external narrowing applied on top of it, kept
/// beside the report rather than folded into it for the reason
/// `core::derate` gives: the C++ derives per read, so the raw report has to
/// survive in order for a relaxed derate to restore it. `meter` is the car side
/// power meter's measurable floors, kept beside the report for the same reason
/// and applied by `core::powermeter_limits`.
///
/// Which of the three a reader wants is the whole of what this split decides.
/// What the stack is told directly reads the stored report with the meter's
/// floors merged in (`capabilities_for_hlc`); the readers that derive a limit
/// set from it read the derated report with the same floors merged in
/// (`capabilities_for_hlc`, the port of
/// `get_powersupply_capabilities_for_hlc`); and the untouched fields are the
/// same in either. See `evse_maximum_limits` and `note_capabilities`.
///
/// **One writer each.** `capabilities` is written by `note_capabilities` alone,
/// and `derate` by `note_external_derating` and `note_present_voltage` (the
/// request and the measurement it derives from, which are two fields of one
/// value).
///
/// The car side meter's floors are **not** a field here. They are an argument
/// at each of the three merge seams, held by `Core::car_side_meter`: the C++
/// keeps them on `EvseManager` (`EvseManager.hpp:310`) outside its
/// `hlc_enabled` block, and a basic AC port has no high level communication
/// port to keep them in. See `core::powermeter_limits::CarSideMeter`.
pub struct DcLimits {
    capabilities: PowerSupplyCapabilities,
    derate: Derate,
    /// `last_hlc_capabilities` (`EvseManager.hpp:341`), the report the stack was
    /// last told. It is the gate on the forward and the reason an unchanged
    /// report is pushed once rather than never: `None` means nothing has been
    /// sent yet.
    last_hlc: Option<PowerSupplyCapabilities>,
}

/// Present because `new` takes no arguments and clippy asks for it. It is the
/// same boot seed, not a value initialized report; see `new`.
impl Default for DcLimits {
    fn default() -> Self {
        Self::new()
    }
}

impl DcLimits {
    /// Seeded with the C++ boot seed (`EvseManager.cpp:212`), which is what
    /// makes the boot emission's change gate answer "unchanged": see `boot`.
    pub fn new() -> Self {
        Self {
            capabilities: PowerSupplyCapabilities::sane_default(),
            derate: Derate::default(),
            last_hlc: None,
        }
    }

    pub fn bidirectional(&self) -> bool {
        self.capabilities.bidirectional
    }

    /// `update_powersupply_capabilities` (`EvseManager.cpp:2761-2771`).
    ///
    /// The **raw** report is what is stored, and this is the one place that
    /// stores it: every reader takes it back through the derate.
    ///
    /// A report that changes nothing, on a port that has already pushed once,
    /// produces **nothing at all** - not the forward and not the two derived
    /// emissions. That is the early return at `:2764-2767`, and the second half
    /// of its condition is what makes the first report push whether or not it
    /// equals the boot seed.
    pub fn note_capabilities(
        &mut self,
        capabilities: PowerSupplyCapabilities,
        floors: Option<PowermeterCapabilities>,
    ) -> Vec<Effect> {
        if self.capabilities == capabilities && self.last_hlc.is_some() {
            return Vec::new();
        }
        self.capabilities = capabilities;
        self.push_to_hlc(floors)
    }

    /// `EvseManager::update_powermeter_capabilities` (`EvseManager.cpp`) below
    /// the session log line: the DC push that follows it, and nothing else.
    ///
    /// The early return above the line, the store and the transcript are the
    /// caller's, because they happen on every deployment and this port exists
    /// only on some. What is left here is the push, whose capability forward is
    /// gated on the report the stack is told having actually moved; a meter
    /// floor below the standing minimum therefore reaches the transcript and no
    /// wire message.
    pub fn note_floors(
        &mut self,
        _change: MeterFloorsChanged,
        floors: Option<PowermeterCapabilities>,
    ) -> Vec<Effect> {
        self.push_to_hlc(floors)
    }

    /// `EvseManager::push_powersupply_capabilities_to_hlc`
    /// (`EvseManager.cpp:2793-2823`), which has four call sites and is the only
    /// producer of any of these three emissions.
    ///
    /// What the stack is told is the report it is **offered**:
    /// `apply_powermeter_limits(apply_external_derating(raw))`, which is
    /// `capabilities_for_hlc`. The port used to forward the raw report with the
    /// meter floors merged and the derate left out, so a port with an active
    /// derate told the vehicle maxima its supply would not deliver.
    ///
    /// The forward carries the C++ change gate, which compares against the last
    /// report sent rather than against the raw one: a raw change the derate or a
    /// meter floor flattens is not re-sent, and a derate change that moves
    /// nothing else is. The two derived emissions go out on every push, as they
    /// do there.
    fn push_to_hlc(&mut self, floors: Option<PowermeterCapabilities>) -> Vec<Effect> {
        let offered = self.capabilities_for_hlc(floors);
        let mut effects = Vec::new();
        if self.last_hlc.as_ref() != Some(&offered) {
            effects.push(Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(
                Box::new(offered),
            )));
        }
        self.last_hlc = Some(offered);
        effects.extend(self.emit_derived(floors));
        effects
    }

    /// The two unconditional emissions of `update_powersupply_capabilities`.
    fn emit_derived(&self, floors: Option<PowermeterCapabilities>) -> Vec<Effect> {
        vec![
            Effect::HlcUpdate(HlcUpdate::ChargingParameters(self.physical_values())),
            Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(
                self.minimum_limits_emitted(floors),
            )),
        ]
    }

    /// `EvseManager.hpp:275-279`. Only the three DC fields; the AC nominal
    /// voltage is the other call site's (`EvseManager.cpp:447-449`) and is
    /// deliberately left absent here rather than filled with a zero.
    fn physical_values(&self) -> PhysicalValues {
        PhysicalValues {
            ac_nominal_voltage_v: None,
            dc_current_regulation_tolerance_a: Some(
                self.capabilities.current_regulation_tolerance_a,
            ),
            dc_peak_current_ripple_a: Some(self.capabilities.peak_current_ripple_a),
            dc_energy_to_be_delivered_wh: Some(ENERGY_TO_BE_DELIVERED_WH),
        }
    }

    /// The supply's report with external derating applied, which is what
    /// `get_powersupply_capabilities()` returns (`EvseManager.cpp:2679-2699`).
    ///
    /// Every C++ caller of that function reads the derated report; the raw one
    /// is reachable only from inside `update_powersupply_capabilities`, which is
    /// why `note_capabilities` below is the one place that does not use this.
    pub fn derated_capabilities(&self) -> PowerSupplyCapabilities {
        self.derate.apply(self.capabilities)
    }

    /// The same narrowing against a report the caller supplies rather than the
    /// stored one, for a reader holding a report this port did not store.
    pub fn derate_only(&self, capabilities: PowerSupplyCapabilities) -> PowerSupplyCapabilities {
        self.derate.apply(capabilities)
    }

    /// `get_powersupply_capabilities_for_hlc()`
    /// (`EvseManager::get_powersupply_capabilities_for_hlc`): the derated
    /// report with the car side power meter's floors merged into it.
    ///
    /// Everything the ISO 15118 stack is told about a **minimum** reads this,
    /// which is the whole point of the seam: the merge raises minima and never
    /// touches a maximum, so a reader that only wants a ceiling gets the same
    /// number from either. The C++ header says the same in the other
    /// direction, reserving `get_powersupply_capabilities()` for the readers
    /// that are not talking to the vehicle (`EvseManager.hpp:275`), among them
    /// the precharge and over voltage thresholds.
    pub fn capabilities_for_hlc(
        &self,
        floors: Option<PowermeterCapabilities>,
    ) -> PowerSupplyCapabilities {
        apply_powermeter_limits(self.derated_capabilities(), floors)
    }

    /// The same merge against a report the caller supplies, the `derate_only`
    /// of this seam and for the same reason: the arrival path holds a report
    /// this port has not stored yet.
    pub fn for_hlc_only(
        &self,
        capabilities: PowerSupplyCapabilities,
        floors: Option<PowermeterCapabilities>,
    ) -> PowerSupplyCapabilities {
        apply_powermeter_limits(self.derate_only(capabilities), floors)
    }

    /// `EvseManager::set_external_derating` (`EvseManager.cpp:2701-2704`).
    ///
    /// Returns whether the derated report actually moved, which is how the
    /// caller decides whether anything downstream needs retelling. The C++ needs
    /// no such answer because it derives per read; see `docs/architecture.md`.
    /// `EvseManager::set_external_derating` (`EvseManager.cpp:2826-2838`): the
    /// request is stored, and a request that changed pushes the whole report to
    /// the stack. Before this the port stored it and told the vehicle nothing,
    /// so a thermal derate left the announcement it contradicts standing.
    ///
    /// Gated on the **request** moving, as the C++ gates (`:2829-2833`), rather
    /// than on the derated report moving: the push has its own gate and the two
    /// answer different questions.
    ///
    /// Returns whether the derated report moved as well, which is what tells
    /// the caller to retell the one reader that keeps a copy.
    pub fn note_external_derating(
        &mut self,
        requested: ExternalDerating,
        floors: Option<PowermeterCapabilities>,
    ) -> (Vec<Effect>, bool) {
        let before = self.derated_capabilities();
        if !self.derate.note_request(requested) {
            return (Vec::new(), false);
        }
        let moved = self.derated_capabilities() != before;
        (self.push_to_hlc(floors), moved)
    }

    /// The present output voltage, which the derate's current and power halves
    /// derive from (`EvseManager.cpp:2689`).
    ///
    /// Returns whether the derated report moved, for the reason
    /// `note_external_derating` does. With no derate set the answer is always
    /// `false`, so a port that never derates pays nothing for this.
    pub fn note_present_voltage(&mut self, voltage_v: f64) -> bool {
        let before = self.derated_capabilities();
        self.derate.note_present_voltage(voltage_v);
        self.derated_capabilities() != before
    }

    /// The EVSE maximum limits the vehicle's request is clamped against.
    ///
    /// A port of `energy_grid/energyImpl.cpp:568-628`, which is the only writer
    /// of the set the C++ clamp reads (`Charger::inform_new_evse_max_hlc_limits`
    /// at `:669`).
    ///
    /// The energy management narrowing is **not** applied, because
    /// the energy flow request and the enforce limits handler are not ported.
    /// Maximum voltage the supply reports it can export, which the cable check
    /// voltage derivation reads directly off the capability report
    /// (`EvseManager.cpp:2150-2152`) rather than off the limit set.
    pub fn max_export_voltage_v(&self) -> f64 {
        self.capabilities.max_export_voltage_v
    }

    /// The minimum the supply can export, which gates the voltage to earth
    /// check: below it the monitor's earth readings are measured against a
    /// voltage that may already have been ramped down.
    pub fn min_export_voltage_v(&self) -> f64 {
        self.capabilities.min_export_voltage_v
    }

    /// The minimum limits **as this call site emits them**
    /// (`EvseManager.hpp:281-286`): the three required fields and no discharge
    /// pair.
    ///
    /// Distinct from `minimum_limits` below, and the distinction is the C++
    /// one rather than a convenience. Two call sites reach
    /// `call_update_dc_minimum_limits`: this one, which fills three fields, and
    /// the energy management one (`energy_grid/energyImpl.cpp:572-574`,
    /// `:607-612`), which fills five. Only the second also informs the charger
    /// (`:670`), so the set the dynamic control mode clamp reads is the five
    /// field one and the set this emission carries is the three field one.
    /// Collapsing them would put discharge minima on a wire message the C++
    /// leaves them off.
    ///
    /// The power figure is the product of the two limits just named, not a
    /// separately reported field.
    /// The DC half of the boot sequence (`EvseManager.cpp:544-561`).
    ///
    /// It is `note_capabilities` run against what the member already holds,
    /// followed by the present value zeroing. The capability report itself is
    /// therefore **not** sent: `:545` reads `get_powersupply_capabilities()`,
    /// which returns the seeded member with external derating applied, and no
    /// derating has arrived at boot, so `:268` compares the seed against itself
    /// and the change gate holds. Written as the two unconditional emissions
    /// rather than as a call to `note_capabilities`, so the fact that no
    /// capability report goes out at boot is visible here instead of depending
    /// on the reader tracing a comparison.
    pub fn boot(&mut self, floors: Option<PowermeterCapabilities>) -> Vec<Effect> {
        // `EvseManager.cpp:561-564`, the fourth push site: the DC setup block
        // pushes directly, with the comment that going through
        // `update_powersupply_capabilities` would store an active derate as raw
        // supply capabilities. Nothing has been sent yet, so the forward goes
        // out with the derived pair.
        let mut effects = self.push_to_hlc(floors);
        // `:557-561`. Sane defaults, so a stack that asks before the supply has
        // reported is told zero rather than nothing.
        effects.extend(self.note_present_values(0.0, 0.0));
        effects
    }

    /// `subscribe_voltage_current` (`EvseManager.cpp:695-726`), reduced to the
    /// one emission at `:716`.
    ///
    /// Three things the C++ handler also does are absent, each for its own
    /// reason. The debug current offset (`:704-708`) and the isolation monitor
    /// restart (`:710-714`) are driven by `hack_present_current_offset` and
    /// `hack_pause_imd_during_precharge`, two of the six manifest keys this
    /// port rejects outright (`core::config`), so there is no setting that
    /// could turn either on. The voltage plausibility monitor update (`:698`)
    /// belongs to a subsystem that is not ported at all.
    ///
    /// The voltage is clamped up to zero and the current is not clamped. That
    /// asymmetry is the C++ one and it is not an oversight on either side: the
    /// wire type gives `evse_present_voltage` a minimum of zero, while a
    /// negative present current is how the import direction reports itself.
    pub fn note_present_values(&self, voltage_v: f64, current_a: f64) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::DcPresentValues {
            voltage_v: if voltage_v > 0.0 { voltage_v } else { 0.0 },
            current_a,
        })]
    }

    /// Read off the same offered report the forward carries, which is what
    /// `push_powersupply_capabilities_to_hlc` fills these three fields from
    /// (`EvseManager.cpp:2812-2817`), so one push cannot tell the stack two
    /// different minima. This is the message the stack turns into
    /// `EVSEMinimumCurrentLimit`, so it is where a car side meter's floor
    /// becomes an offer the vehicle can see.
    ///
    /// The derate makes no difference to the numbers here today, because
    /// `Derate::apply` touches only the four maxima. Reading the offered report
    /// rather than a second helper that leaves the derate out is what keeps
    /// that a fact about the derate instead of an assumption in two places.
    fn minimum_limits_emitted(&self, floors: Option<PowermeterCapabilities>) -> MinimumLimits {
        export_minimum_limits(&self.capabilities_for_hlc(floors))
    }
}

/// The export half of the EVSE minimum set, which is the whole of what
/// `update_powersupply_capabilities` emits (`EvseManager.hpp:281-286`).
fn export_minimum_limits(capabilities: &PowerSupplyCapabilities) -> MinimumLimits {
    MinimumLimits {
        minimum_current_a: capabilities.min_export_current_a,
        minimum_voltage_v: capabilities.min_export_voltage_v,
        minimum_power_w: capabilities.min_export_current_a * capabilities.min_export_voltage_v,
        minimum_discharge_current_a: None,
        minimum_discharge_power_w: None,
    }
}

/// The EVSE maximum set before the energy allowance narrows it
/// (`energyImpl.cpp:566-607`), which is the supply's own ceiling and the base
/// the enforced limits handler cuts down.
///
/// It is the supply's ceiling and nothing else here on purpose: the C++ builds
/// the same base from `powersupply_capabilities` before applying the watt
/// allowance at `:592-602` and `:614-625`, and this port applies the allowance
/// in the one place that has it. The alternative, leaving the set at the value
/// initialized zeros `Charger` holds before the first energy pass, would clamp
/// every request to zero and leave a DC port unable to charge at all.
pub fn evse_maximum_limits(capabilities: &PowerSupplyCapabilities) -> MaximumLimits {
    MaximumLimits {
        maximum_current_a: capabilities.max_export_current_a,
        maximum_voltage_v: capabilities.max_export_voltage_v,
        maximum_power_w: capabilities.max_export_power_w,
        // Copied across from the optional capability fields, so an absent
        // import capability stays an absent limit (`energyImpl.cpp:569-570`,
        // `:606`).
        maximum_discharge_current_a: capabilities.max_import_current_a,
        maximum_discharge_power_w: capabilities.max_import_power_w,
    }
}

/// The EVSE minimum set the vehicle's request is clamped against, which is the
/// five field set (`energyImpl.cpp:572-574`, `:607-612`) and not the three
/// field set `DcLimits::minimum_limits_emitted` carries. See that function for
/// why the two differ.
pub fn evse_minimum_limits(capabilities: &PowerSupplyCapabilities) -> MinimumLimits {
    MinimumLimits {
        minimum_discharge_current_a: capabilities.min_import_current_a,
        // Unconditionally present, unlike the discharge current beside it: the
        // C++ assigns the product of two `value_or(0.0)` reads
        // (`energyImpl.cpp:610-612`), so a unidirectional supply names a zero
        // here rather than naming nothing.
        minimum_discharge_power_w: Some(
            capabilities.min_import_voltage_v.unwrap_or(0.0)
                * capabilities.min_import_current_a.unwrap_or(0.0),
        ),
        ..export_minimum_limits(capabilities)
    }
}

/// The ISO 15118-20 dynamic control mode clamp, a port of
/// `subscribe_d20_dc_dynamic_charge_mode` (`EvseManager.cpp:740-825`).
///
/// `None` means the whole update is abandoned, which is the C++ early return at
/// `:805-808`. The two power bounds are clamped against **different sources**,
/// the maximum against the EVSE maximum limits and the minimum against the EVSE
/// minimum limits, so they can cross: a vehicle naming a minimum above its own
/// maximum is malformed but representable, and an in flight limit update can
/// put the EVSE minimum above the EVSE maximum. Neither is a reason to program
/// the supply with a bound pair that does not describe an interval, so nothing
/// is applied at all.
///
/// Written with `f64::min` and `f64::max` rather than `f64::clamp`. The clamped
/// value is not itself used, only compared, but the shape matters: `clamp`
/// panics on crossed bounds, which is exactly the input this function exists to
/// answer for.
///
/// The minimum charge power has no other reader. Its whole effect is this
/// guard: the C++ computes it, compares it, and never applies it (verified over
/// the full lambda). So a port that dropped the guard would also have no reason
/// to compute the minimum at all, and the minimum limit set would become dead.
///
/// Two locals in the C++ lambda are dead and are not ported: `energy_flow_changed`
/// (`:744-746`), computed from a function local `static` and never read, and
/// `target_changed` (`:748`), initialized to false and never read. The `static`
/// is also shared across every port instance in a multi EVSE process, which is
/// the sort of thing that would matter if anything read it.
pub fn dynamic_mode_target(
    request: DynamicModeRequest,
    maximum: MaximumLimits,
    minimum: MinimumLimits,
    exporting_to_grid: bool,
    current_demand_active: bool,
    actual_voltage_v: f64,
) -> Option<DcTarget> {
    let mut max_charge_power_w = 0.0;
    let mut min_charge_power_w = 0.0;
    let mut max_charge_current_a = 0.0;

    // `:757`. The import direction needs the site to actually be exporting and
    // a running current demand. Each of the three clamps below needs **both**
    // its own halves present, so a vehicle that names no discharge figure at
    // all leaves the corresponding bound at zero rather than falling back to
    // the charge figure beside it.
    if exporting_to_grid && current_demand_active {
        if let (Some(ev), Some(evse)) = (
            request.max_discharge_power_w,
            maximum.maximum_discharge_power_w,
        ) {
            max_charge_power_w = ev.abs().min(evse.abs());
        }
        if let (Some(ev), Some(evse)) = (
            request.min_discharge_power_w,
            minimum.minimum_discharge_power_w,
        ) {
            min_charge_power_w = ev.abs().max(evse.abs());
        }
        if let (Some(ev), Some(evse)) = (
            request.max_discharge_current_a,
            maximum.maximum_discharge_current_a,
        ) {
            max_charge_current_a = ev.abs().min(evse.abs());
        }
    } else {
        // `:793-801`.
        max_charge_power_w = request.max_charge_power_w.min(maximum.maximum_power_w);
        min_charge_power_w = request.min_charge_power_w.max(minimum.minimum_power_w);
        max_charge_current_a = request.max_charge_current_a.min(maximum.maximum_current_a);
    }

    // `:805-808`. A strict comparison, so bounds that exactly meet are an
    // interval and are accepted.
    if min_charge_power_w > max_charge_power_w {
        log::error!("minimum charge power limit is greater than the maximum charge power limit");
        return None;
    }

    // `:810-812`. Read off `is_actually_exporting_to_grid` alone, without the
    // running current demand the limit branch above also requires. The two
    // conditions genuinely differ in the C++ and the difference is observable:
    // a request arriving while the site exports but before current demand has
    // started takes the import limits and the export voltage rule. Preserved.
    let voltage_v = if exporting_to_grid {
        request.min_voltage_v.max(minimum.minimum_voltage_v)
    } else {
        request.max_voltage_v
    };

    // `:815-820`. The power is converted at the voltage actually on the cable,
    // because converting at the target would ask for a current that never
    // reaches the requested watts.
    //
    // The zero guard is kept for the C++ shape and is **unobservable in Rust**:
    // no test can distinguish it from the bare division, because for any non
    // negative power and current the division gives an infinity or a NaN and
    // `f64::min` answers with the current cap either way. A hand mutation
    // deleting it survives the whole suite, and that is a fact about IEEE 754
    // rather than a gap. It is not deleted, because relying on the NaN
    // propagation rule of `f64::min` to get the right answer is a worse thing
    // to leave for a reader than a branch that reads as the C++ does.
    let current_a = if actual_voltage_v <= 0.0 {
        max_charge_current_a
    } else {
        (max_charge_power_w / actual_voltage_v).min(max_charge_current_a)
    };

    Some(DcTarget {
        voltage_v,
        current_a,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn capabilities() -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional: false,
            max_export_voltage_v: 950.0,
            min_export_voltage_v: 150.0,
            max_export_current_a: 400.0,
            min_export_current_a: 2.0,
            max_export_power_w: 300_000.0,
            current_regulation_tolerance_a: 2.0,
            peak_current_ripple_a: 3.0,
            max_import_voltage_v: None,
            min_import_voltage_v: None,
            ..PowerSupplyCapabilities::sane_default()
        }
    }

    /// `EvseManager.hpp:265-293`, in its own order: the capability report
    /// itself, then the physical setup values, then the minimum limits.
    #[test]
    fn a_changed_capability_report_reaches_the_stack_with_the_values_it_implies() {
        let mut limits = DcLimits::new();
        let caps = capabilities();

        assert_eq!(
            limits.note_capabilities(caps, None),
            vec![
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(Box::new(caps))),
                Effect::HlcUpdate(HlcUpdate::ChargingParameters(PhysicalValues {
                    ac_nominal_voltage_v: None,
                    dc_current_regulation_tolerance_a: Some(2.0),
                    dc_peak_current_ripple_a: Some(3.0),
                    dc_energy_to_be_delivered_wh: Some(10_000.0),
                })),
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits {
                    minimum_current_a: 2.0,
                    minimum_voltage_v: 150.0,
                    minimum_power_w: 300.0,
                    minimum_discharge_current_a: None,
                    minimum_discharge_power_w: None,
                })),
            ]
        );
    }

    /// `update_powersupply_capabilities` returns before it touches anything on
    /// an unchanged report, once something has been pushed
    /// (`EvseManager.cpp:2764-2767`), so a supply that republishes the same
    /// report costs nothing at all.
    ///
    /// This asserted that the two derived emissions were reissued and only the
    /// forward was suppressed, citing a gate in the header. The gate is there,
    /// inside the push, and this early return sits above the push: a report
    /// that changes nothing never reaches it.
    #[test]
    fn an_unchanged_capability_report_reissues_nothing() {
        let mut limits = DcLimits::new();
        let caps = capabilities();
        limits.note_capabilities(caps, None);

        assert_eq!(limits.note_capabilities(caps, None), Vec::new());
    }

    /// `EvseManager.cpp:561-564`, the boot pass. It pushes **directly** rather
    /// than through `update_powersupply_capabilities`, with a comment saying
    /// why: going through the store would record an active derate as the raw
    /// supply capabilities. Nothing has been sent yet, so the forward's gate is
    /// open and the boot seed reaches the stack with the two derived
    /// emissions. Then the present values are zeroed.
    ///
    /// This asserted that the boot pass sent no capability report, on the
    /// reading that the boot pass calls
    /// `update_powersupply_capabilities(get_powersupply_capabilities())` and
    /// its change gate suppresses the forward. That call is not what the boot
    /// block makes, and the gate it describes is against the last report
    /// **sent**, which at boot is none.
    #[test]
    fn the_boot_pass_sends_the_seed_report_and_zeroes_the_present_values() {
        let mut limits = DcLimits::new();

        assert_eq!(
            limits.boot(None),
            vec![
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(Box::new(
                    PowerSupplyCapabilities::sane_default()
                ))),
                Effect::HlcUpdate(HlcUpdate::ChargingParameters(PhysicalValues {
                    ac_nominal_voltage_v: None,
                    // The C++ boot seed, half an ampere each.
                    dc_current_regulation_tolerance_a: Some(0.5),
                    dc_peak_current_ripple_a: Some(0.5),
                    dc_energy_to_be_delivered_wh: Some(10_000.0),
                })),
                Effect::HlcUpdate(HlcUpdate::DcMinimumLimits(MinimumLimits::default())),
                Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                    voltage_v: 0.0,
                    current_a: 0.0,
                }),
            ]
        );
    }

    /// And the report that follows the boot seed is suppressed when it says
    /// the same thing, which is the gate doing its work: the supply's first
    /// report on a port whose seed already described it adds nothing.
    #[test]
    fn a_first_report_equal_to_the_seed_is_not_forwarded_twice() {
        let mut limits = DcLimits::new();
        limits.boot(None);

        let effects = limits.note_capabilities(PowerSupplyCapabilities::sane_default(), None);

        assert!(effects.is_empty(), "{effects:?}");
    }

    /// `EvseManager.cpp:700-716`.
    #[test]
    fn a_supply_reading_reaches_the_stack_as_the_present_values() {
        let limits = DcLimits::new();

        assert_eq!(
            limits.note_present_values(412.5, 63.25),
            vec![Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                voltage_v: 412.5,
                current_a: 63.25,
            })]
        );
    }

    /// `EvseManager.cpp:701`: `(m.voltage_V > 0 ? m.voltage_V : 0.0)`. The wire
    /// type gives `evse_present_voltage` a minimum of zero, so a supply
    /// reporting a small negative offset around zero would otherwise put an out
    /// of range value on the wire. The current is **not** clamped, and that
    /// asymmetry is the C++ one: a negative current is how the import
    /// direction reports itself.
    #[test]
    fn a_negative_supply_voltage_reaches_the_stack_as_zero_and_the_current_unclamped() {
        let limits = DcLimits::new();

        assert_eq!(
            limits.note_present_values(-1.5, -40.0),
            vec![Effect::HlcUpdate(HlcUpdate::DcPresentValues {
                voltage_v: 0.0,
                current_a: -40.0,
            })]
        );
    }

    fn bidirectional_capabilities() -> PowerSupplyCapabilities {
        PowerSupplyCapabilities {
            bidirectional: true,
            max_import_voltage_v: Some(900.0),
            min_import_voltage_v: Some(200.0),
            max_import_current_a: Some(300.0),
            min_import_current_a: Some(4.0),
            max_import_power_w: Some(250_000.0),
            ..capabilities()
        }
    }

    /// `energy_grid/energyImpl.cpp:568-628` with the energy allowance not
    /// binding. The allowance is applied by the enforced limits handler, which
    /// starts from this base; see `core::energy::enforce`.
    #[test]
    fn the_maximum_limits_are_the_supplys_export_ceiling() {
        assert_eq!(
            evse_maximum_limits(&capabilities()),
            MaximumLimits {
                maximum_current_a: 400.0,
                maximum_voltage_v: 950.0,
                maximum_power_w: 300_000.0,
                maximum_discharge_current_a: None,
                maximum_discharge_power_w: None,
            }
        );
    }

    /// The import half is optional on the wire, and the C++ assigns the two
    /// discharge maxima straight across from the optional capability fields
    /// (`energyImpl.cpp:569-570`, `:606`), so an absent capability stays an
    /// absent limit rather than becoming a zero.
    #[test]
    fn an_absent_import_capability_leaves_the_discharge_maxima_absent() {
        assert_eq!(
            evse_maximum_limits(&bidirectional_capabilities()),
            MaximumLimits {
                maximum_current_a: 400.0,
                maximum_voltage_v: 950.0,
                maximum_power_w: 300_000.0,
                maximum_discharge_current_a: Some(300.0),
                maximum_discharge_power_w: Some(250_000.0),
            }
        );
    }

    /// `energyImpl.cpp:572-574` and `:607-612`. Five fields, unlike the three
    /// the header emission carries, and the discharge power is a product of two
    /// optionals each read as zero when absent rather than an optional itself.
    #[test]
    fn the_minimum_limits_the_clamp_reads_carry_the_discharge_pair() {
        assert_eq!(
            evse_minimum_limits(&bidirectional_capabilities()),
            MinimumLimits {
                minimum_current_a: 2.0,
                minimum_voltage_v: 150.0,
                minimum_power_w: 300.0,
                minimum_discharge_current_a: Some(4.0),
                minimum_discharge_power_w: Some(800.0),
            }
        );
    }

    /// `energyImpl.cpp:610-612` reads both import minima through `value_or(0.0)`
    /// and assigns the product unconditionally, so the discharge power minimum
    /// is present and zero for a unidirectional supply rather than absent. The
    /// discharge current minimum beside it stays absent, and that difference is
    /// the C++ difference: one is a product of two `value_or` reads, the other
    /// is an optional copied across.
    #[test]
    fn a_unidirectional_supply_still_names_a_zero_discharge_power_minimum() {
        assert_eq!(
            evse_minimum_limits(&capabilities()),
            MinimumLimits {
                minimum_current_a: 2.0,
                minimum_voltage_v: 150.0,
                minimum_power_w: 300.0,
                minimum_discharge_current_a: None,
                minimum_discharge_power_w: Some(0.0),
            }
        );
    }

    /// A request the export direction accepts without hitting any bound.
    fn request() -> DynamicModeRequest {
        DynamicModeRequest {
            max_charge_power_w: 100_000.0,
            min_charge_power_w: 1_000.0,
            max_charge_current_a: 250.0,
            max_voltage_v: 800.0,
            min_voltage_v: 300.0,
            max_discharge_power_w: None,
            min_discharge_power_w: None,
            max_discharge_current_a: None,
        }
    }

    fn maximum() -> MaximumLimits {
        MaximumLimits {
            maximum_current_a: 400.0,
            maximum_voltage_v: 950.0,
            maximum_power_w: 300_000.0,
            maximum_discharge_current_a: Some(300.0),
            maximum_discharge_power_w: Some(250_000.0),
        }
    }

    fn minimum() -> MinimumLimits {
        MinimumLimits {
            minimum_current_a: 2.0,
            minimum_voltage_v: 150.0,
            minimum_power_w: 300.0,
            minimum_discharge_current_a: Some(4.0),
            minimum_discharge_power_w: Some(800.0),
        }
    }

    mod dynamic_control_mode {
        use super::*;

        /// `EvseManager.cpp:793-816`. The vehicle asks for less than the EVSE
        /// can give on every axis, so its own figures survive: the voltage is
        /// its maximum, and the current is the power it asked for divided by
        /// the voltage actually on the cable.
        #[test]
        fn a_request_inside_every_bound_is_the_power_divided_by_the_present_voltage() {
            assert_eq!(
                dynamic_mode_target(request(), maximum(), minimum(), false, true, 400.0),
                Some(DcTarget {
                    voltage_v: 800.0,
                    current_a: 250.0,
                })
            );
        }

        /// The current the power implies exceeds what the vehicle allows, so
        /// the vehicle's current cap wins (`:812`, the `std::min`).
        #[test]
        fn the_current_cap_wins_when_the_power_implies_more_current() {
            let request = DynamicModeRequest {
                max_charge_power_w: 100_000.0,
                max_charge_current_a: 100.0,
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), false, true, 400.0),
                Some(DcTarget {
                    voltage_v: 800.0,
                    current_a: 100.0,
                })
            );
        }

        /// `:794-799`. The EVSE ceiling is below what the vehicle asked for on
        /// both power and current, so both are cut down to it.
        #[test]
        fn the_evse_maxima_cut_a_request_that_asks_for_more() {
            let request = DynamicModeRequest {
                max_charge_power_w: 500_000.0,
                max_charge_current_a: 800.0,
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), false, true, 400.0),
                Some(DcTarget {
                    voltage_v: 800.0,
                    // 300 kW over 400 V is 750 A, above the 400 A ceiling.
                    current_a: 400.0,
                })
            );
        }

        /// `:810-812`. Nothing is on the cable yet, so there is no voltage to
        /// divide by and the current cap is taken whole.
        #[test]
        fn a_present_voltage_of_zero_takes_the_current_cap_whole() {
            assert_eq!(
                dynamic_mode_target(request(), maximum(), minimum(), false, true, 0.0),
                Some(DcTarget {
                    voltage_v: 800.0,
                    current_a: 250.0,
                })
            );
        }

        /// The mandatory guard. `:805-808`.
        ///
        /// A vehicle asking for a minimum above its own maximum is malformed
        /// but perfectly representable on the wire, and the two bounds are
        /// clamped against different sources, so they can cross. The C++
        /// abandons the whole update. Asserted as an abandoned update and not
        /// as an absence of a panic, because a clamp written as
        /// `f64::clamp(min, max)` would panic here and a test that only watched
        /// for a panic would pass on a port that silently swapped the bounds.
        #[test]
        fn a_vehicle_minimum_above_its_own_maximum_abandons_the_update() {
            let request = DynamicModeRequest {
                max_charge_power_w: 10_000.0,
                min_charge_power_w: 15_000.0,
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), false, true, 400.0),
                None
            );
        }

        /// The other way the bounds cross: the EVSE's own minimum sits above
        /// its own maximum, which a limit update race can produce.
        #[test]
        fn an_evse_minimum_above_the_evse_maximum_abandons_the_update() {
            let maximum = MaximumLimits {
                maximum_power_w: 5_000.0,
                ..maximum()
            };
            let minimum = MinimumLimits {
                minimum_power_w: 20_000.0,
                ..minimum()
            };
            let request = DynamicModeRequest {
                max_charge_power_w: 100_000.0,
                min_charge_power_w: 0.0,
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum, minimum, false, true, 400.0),
                None
            );
        }

        /// Equal bounds are not crossed bounds: the C++ compares with a strict
        /// `>` (`:805`), so a request whose minimum exactly meets its maximum
        /// is accepted.
        #[test]
        fn bounds_that_exactly_meet_are_accepted() {
            let request = DynamicModeRequest {
                max_charge_power_w: 40_000.0,
                min_charge_power_w: 40_000.0,
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), false, true, 400.0),
                Some(DcTarget {
                    voltage_v: 800.0,
                    // 40 kW over 400 V is 100 A, below the 250 A cap.
                    current_a: 100.0,
                })
            );
        }

        /// `:762-791`. Every discharge figure is read through `fabs`, the
        /// maximum takes the smaller magnitude and the minimum the larger, and
        /// the target voltage becomes the vehicle's minimum rather than its
        /// maximum (`:810-812`).
        #[test]
        fn the_export_direction_clamps_on_magnitudes_and_targets_the_minimum_voltage() {
            let request = DynamicModeRequest {
                max_discharge_power_w: Some(-80_000.0),
                min_discharge_power_w: Some(-500.0),
                max_discharge_current_a: Some(-200.0),
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), true, true, 400.0),
                Some(DcTarget {
                    voltage_v: 300.0,
                    // 80 kW over 400 V is 200 A, exactly the current cap.
                    current_a: 200.0,
                })
            );
        }

        /// The EVSE minimum voltage is the floor under the vehicle's own
        /// minimum (`:810-811`, the `std::max`).
        #[test]
        fn the_export_target_voltage_never_falls_below_the_evse_minimum() {
            let request = DynamicModeRequest {
                min_voltage_v: 100.0,
                max_discharge_power_w: Some(-80_000.0),
                min_discharge_power_w: Some(-500.0),
                max_discharge_current_a: Some(-200.0),
                ..request()
            };

            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), true, true, 400.0)
                    .map(|target| target.voltage_v),
                Some(150.0)
            );
        }

        /// The discharge minimum is the larger magnitude, so an EVSE minimum
        /// above the discharge maximum crosses the bounds in the export
        /// direction too.
        #[test]
        fn crossed_bounds_in_the_export_direction_abandon_the_update() {
            let request = DynamicModeRequest {
                max_discharge_power_w: Some(-500.0),
                min_discharge_power_w: Some(-100.0),
                max_discharge_current_a: Some(-200.0),
                ..request()
            };

            // The EVSE discharge minimum of 800 W is above the 500 W the
            // vehicle will accept.
            assert_eq!(
                dynamic_mode_target(request, maximum(), minimum(), true, true, 400.0),
                None
            );
        }

        /// `:762-791` guards each of the three discharge clamps on **both**
        /// halves being present, so a vehicle that names no discharge figure
        /// leaves all three at zero. Nothing crosses, and the supply is asked
        /// for no current at the vehicle's minimum voltage.
        #[test]
        fn an_export_request_naming_no_discharge_figures_resolves_to_no_current() {
            assert_eq!(
                dynamic_mode_target(request(), maximum(), minimum(), true, true, 400.0),
                Some(DcTarget {
                    voltage_v: 300.0,
                    current_a: 0.0,
                })
            );
        }

        /// The two conditions are not the same condition, and the C++ does not
        /// treat them as one: the limit branch is gated on
        /// `is_actually_exporting_to_grid and current_demand_active` (`:757`)
        /// while the target voltage reads `is_actually_exporting_to_grid`
        /// alone (`:810`). A request arriving while the site exports but before
        /// current demand has started therefore takes the **import** limits and
        /// the **export** voltage rule. Preserved rather than tidied, and
        /// pinned here because collapsing the two into one flag is the obvious
        /// simplification and it changes behavior.
        #[test]
        fn exporting_before_current_demand_takes_import_limits_and_the_export_voltage() {
            assert_eq!(
                dynamic_mode_target(request(), maximum(), minimum(), true, false, 400.0),
                Some(DcTarget {
                    // The export rule: the vehicle's minimum, floored at the
                    // EVSE minimum.
                    voltage_v: 300.0,
                    // The import rule: 100 kW over 400 V is 250 A, exactly the
                    // vehicle's own cap.
                    current_a: 250.0,
                })
            );
        }
    }

    /// External derating, at the seam where it meets the capability report.
    ///
    /// `core::derate` owns the derivation and its own tests; what these pin is
    /// the thing that seam gets wrong: which readers see the narrowing and which
    /// see the supply's own report.
    mod derating {
        use super::*;

        fn bidirectional() -> PowerSupplyCapabilities {
            PowerSupplyCapabilities {
                bidirectional: true,
                max_import_current_a: Some(300.0),
                max_import_power_w: Some(250_000.0),
                ..capabilities()
            }
        }

        fn derate_export_current(current_a: f64) -> ExternalDerating {
            ExternalDerating {
                max_export_current_a: Some(current_a),
                ..ExternalDerating::default()
            }
        }

        /// The capability report a push forwarded, or `None` when the gate
        /// suppressed the forward.
        fn forwarded(effects: &[Effect]) -> Option<PowerSupplyCapabilities> {
            effects.iter().find_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::PowerSupplyCapabilities(caps)) => Some(**caps),
                _ => None,
            })
        }

        /// The report the stack is told, and what happens to it when a derate
        /// arrives.
        ///
        /// This asserted the opposite: that the vehicle's limit set narrowed
        /// and the forwarded report did not, on the grounds that the C++
        /// forwards its argument and derates only on read. It does not - and
        /// the citation for that claim, `EvseManager.hpp:269`, is the
        /// declaration comment of `get_powersupply_capabilities`.
        /// `push_powersupply_capabilities_to_hlc` forwards
        /// `apply_powermeter_limits(apply_external_derating(raw))`, so the
        /// vehicle is offered the derated maxima, and a port that forwarded
        /// the raw ones offered maxima its supply would refuse.
        #[test]
        fn a_derate_narrows_the_report_the_stack_is_told() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);

            let (effects, moved) =
                limits.note_external_derating(derate_export_current(100.0), None);

            assert!(moved, "the derated report moved");
            assert_eq!(
                evse_maximum_limits(&limits.derated_capabilities()).maximum_current_a,
                100.0
            );
            assert_eq!(
                forwarded(&effects).map(|caps| caps.max_export_current_a),
                Some(100.0),
                "the vehicle is told the derated ceiling: {effects:?}"
            );
        }

        /// The same rule in the other order: a report arriving while a derate
        /// stands is forwarded derated.
        #[test]
        fn a_report_arriving_under_a_derate_is_forwarded_derated() {
            let mut limits = DcLimits::new();
            limits.note_external_derating(derate_export_current(100.0), None);

            let effects = limits.note_capabilities(capabilities(), None);

            assert_eq!(
                forwarded(&effects).map(|caps| caps.max_export_current_a),
                Some(100.0),
                "{effects:?}"
            );
        }

        /// The request gate, which is the C++'s: an identical request returns
        /// before it touches anything, so nothing is pushed and nothing is
        /// retold.
        #[test]
        fn a_repeated_derate_request_pushes_nothing() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            limits.note_external_derating(derate_export_current(100.0), None);

            let (effects, moved) =
                limits.note_external_derating(derate_export_current(100.0), None);

            assert!(effects.is_empty(), "{effects:?}");
            assert!(!moved);
        }

        /// A derate above the capability changes the request, so it is pushed -
        /// and the forward inside that push is suppressed by its own gate,
        /// because the offered report did not move. The `moved` answer is
        /// false for the same reason, so nothing is retold either.
        #[test]
        fn a_derate_above_the_capability_moves_nothing_and_forwards_nothing() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);

            let (effects, moved) =
                limits.note_external_derating(derate_export_current(600.0), None);

            assert!(!moved);
            assert_eq!(forwarded(&effects), None, "{effects:?}");
            assert!(
                !effects.is_empty(),
                "the derived emissions still go out on every push: {effects:?}"
            );
        }

        /// Relaxing is a request that no longer names the field, and it has to
        /// register as a change or the readers keep the old ceiling forever.
        #[test]
        fn relaxing_a_derate_reports_a_change_and_restores_the_capability() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            limits.note_external_derating(derate_export_current(100.0), None);

            let (effects, moved) =
                limits.note_external_derating(ExternalDerating::default(), None);

            assert!(moved);
            assert_eq!(
                evse_maximum_limits(&limits.derated_capabilities()).maximum_current_a,
                400.0
            );
            assert_eq!(
                forwarded(&effects).map(|caps| caps.max_export_current_a),
                Some(400.0),
                "and the vehicle is told the capability is back: {effects:?}"
            );
        }

        /// A measurement moves the derived half, so it has to register as a
        /// change: the C++ re-derives against the voltage on every read.
        #[test]
        fn a_present_voltage_that_moves_the_derived_cap_reports_a_change() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            limits.note_external_derating(derate_export_current(100.0), None);

            assert!(limits.note_present_voltage(400.0));
            // 100 A at 400 V, which is below the supply's 300 kW.
            assert_eq!(
                evse_maximum_limits(&limits.derated_capabilities()).maximum_power_w,
                40_000.0
            );
        }

        /// The case that keeps this cheap. With no derate set the measurement
        /// arrives several times a second and must move nothing, or every
        /// measurement would retell both readers.
        #[test]
        fn a_present_voltage_with_no_derate_set_reports_no_change() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            assert!(!limits.note_present_voltage(400.0));
            assert!(!limits.note_present_voltage(700.0));
        }

        /// A repeated measurement is not a change either, so a supply holding a
        /// steady voltage under a derate stays quiet.
        #[test]
        fn an_unchanged_present_voltage_reports_no_change() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            limits.note_external_derating(derate_export_current(100.0), None);
            assert!(limits.note_present_voltage(400.0));
            assert!(!limits.note_present_voltage(400.0));
        }

        /// The import direction reaches the optional overload, so a derate can
        /// hand a supply an import ceiling it never named. Pinned here as well
        /// as in `core::derate`, because this is the reader that carries it to
        /// the vehicle.
        #[test]
        fn a_derate_introduces_a_discharge_limit_the_supply_never_named() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(capabilities(), None);
            limits.note_external_derating(ExternalDerating {
                max_import_current_a: Some(50.0),
                ..ExternalDerating::default()
            }, None);

            assert_eq!(
                evse_maximum_limits(&limits.derated_capabilities()).maximum_discharge_current_a,
                Some(50.0)
            );
        }

        #[test]
        fn a_derate_lowers_a_discharge_limit_the_supply_did_name() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(bidirectional(), None);
            limits.note_external_derating(ExternalDerating {
                max_import_current_a: Some(50.0),
                ..ExternalDerating::default()
            }, None);

            assert_eq!(
                evse_maximum_limits(&limits.derated_capabilities()).maximum_discharge_current_a,
                Some(50.0)
            );
        }

        /// The minimum limits and the physical values read fields no derate
        /// touches, so they answer the same under one as without.
        #[test]
        fn derating_leaves_the_minimum_limits_and_the_physical_values_alone() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(bidirectional(), None);
            let minimum = evse_minimum_limits(&limits.derated_capabilities());
            let physical = limits.physical_values();

            limits.note_external_derating(derate_export_current(1.0), None);

            assert_eq!(evse_minimum_limits(&limits.derated_capabilities()), minimum);
            assert_eq!(limits.physical_values(), physical);
        }

        /// Nor the maximum export voltage, which the cable check reads off the
        /// report directly (`EvseManager.cpp:2150-2152`).
        #[test]
        fn derating_leaves_the_maximum_export_voltage_alone() {
            let mut limits = DcLimits::new();
            limits.note_capabilities(bidirectional(), None);
            limits.note_external_derating(ExternalDerating {
                max_export_current_a: Some(1.0),
                max_export_power_w: Some(1.0),
                max_import_current_a: Some(1.0),
                max_import_power_w: Some(1.0),
            }, None);

            assert_eq!(limits.max_export_voltage_v(), 950.0);
        }
    }
}
