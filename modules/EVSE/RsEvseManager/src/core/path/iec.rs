// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! IEC 61851-1 control pilot reducer, shared by both AC power paths.
//!
//! The signature is deliberately narrow and must stay that way:
//!
//! ```ignore
//! fn handle(&mut self, input: IecInput) -> Vec<IecCommand>
//! ```
//!
//! New behavior becomes an `IecInput` variant. It never becomes a new method.
//! This is the constraint that keeps a shared reducer from widening into a
//! second coordinator as it acquires callers.

use std::time::Duration;

use super::StoppingOutcome;
use crate::core::config::{ReinitMethod, SwitchCpState};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AcState {
    Startup,
    Idle,
    WaitingForAuthentication,
    PrepareCharging,
    Charging,
    ChargingPausedEv,
    ChargingPausedEvse,
    /// The break a phase change takes out of a charging session. The pilot is
    /// dropped so the vehicle stops drawing, the board is told after
    /// `switch_3ph1ph_delay_s`, and the session returns to `PrepareCharging`.
    /// `Charger::EvseState::SwitchPhases` (`Charger.cpp:580-604`).
    SwitchPhases,
    StoppingCharging,
    /// The connection to the vehicle is being reinitialized: the pilot is held
    /// at the configured level long enough for the vehicle to notice, and the
    /// session then restarts from `WaitingForAuthentication`.
    /// `Charger::EvseState::Reinit`, entered by
    /// `Charger::process_pending_reinit_request`.
    ///
    /// The transaction survives it, which is the point: `ac_with_soc` uses the
    /// reinit to reintroduce a vehicle that has just been talking ISO 15118 to
    /// the same port as a basic AC one, without closing the billing record.
    Reinit,
    /// The session is over and may not restart. Only an unplug or a pending
    /// disable leaves this state. `Charger.cpp:1059`.
    Finished,
    Disabled,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IecInput {
    StartupComplete,
    Enable,
    Disable,
    CarPluggedIn,
    CarUnplugged,
    AuthorizationAccepted,
    TransactionStarted,
    CarRequestedPower,
    CarRequestedVentilatedPower,
    CarRequestedStopPower,
    /// The pilot reached state B without coming from C or D, so the reading
    /// asserts the connector lock and nothing else
    /// (`IECStateMachine::state_machine`, `IECStateMachine.cpp:189-193`).
    CpStateB,
    CpStateE,
    CpStateF,
    PowerOn,
    PowerOff,
    StopRequested,
    PauseRequested,
    ResumeRequested,
    EmergencyShutdown,
    ErrorShutdown,
    /// The energy manager's phase count changed and the request reached a
    /// state that owes the vehicle a break first.
    /// `Charger::switch_three_phases_while_charging` (`Charger.cpp:1524-1545`).
    /// The payload is `switch_3ph1ph_threephase`, true for three phases.
    SwitchPhasesRequested(bool),
    /// Timer facts arrive as inputs rather than as separate entry points.
    FaultStateFExpired,
    CpStateFUnlockTimerExpired,
    CpStateC1TimeoutExpired,
    StoppingChargingTimeoutExpired,
    /// The switching break ran its course (`Charger.cpp:597-603`).
    SwitchPhasesDelayExpired,
    /// High level communication failed and the session restarts from waiting
    /// for authentication, which is where `Charger::dlink_error` sends it
    /// (`Charger.cpp:2101`).
    SessionRestart,
    /// Enter the reinitialization state.
    ///
    /// The two halves of `Charger::process_pending_reinit_request` split here:
    /// the decision about *when* the port may enter, which reads the SLAC link
    /// and belongs above this reducer, and the entry itself, which is this. The
    /// entry is the body of the `EvseState::Reinit` `initialize_state` block:
    /// both contactor permissions withdrawn and the configured pilot level
    /// signalled.
    ReinitStarted,
    /// The reinit held its pilot level for the configured duration, so the
    /// session restarts. `Charger.cpp`'s exit from the `Reinit` case: `X1`, then
    /// `WaitingForAuthentication`.
    ReinitFinished,
    /// The budget behind a charge has gone. `Charger::run_state_machine`'s
    /// `Charging` arm tests `power_available()` before anything else it does
    /// and leaves for `StoppingCharging` when it fails (`Charger.cpp:836-871`),
    /// so a charge with no energy behind it stops rather than standing on a
    /// duty cycle nobody can honour.
    ///
    /// An input rather than a read of the stored limit, because *when* the
    /// withdrawal counts is not the reducer's decision: a basic session stops
    /// at once and a high level one is given
    /// `hlc_charge_loop_without_energy_timeout_s` first, which is the split at
    /// `:837-867`.
    EnergyWithdrawn,
    /// The budget came back. The C++ recomputes the pause reasons on every pass
    /// of the `ChargingPausedEVSE` arm and resumes into `PrepareCharging` once
    /// none of the three is left (`Charger.cpp:1004-1032`); this is the energy
    /// one of the three going away.
    EnergyRestored,
}

/// The timers the AC paths arm, named rather than measured.
///
/// The reducer decides which timer a transition needs; `ac::timer_id` and
/// `ac::timer_after` are the single place that says how long each one runs and
/// which identity it carries. That split is what lets the reducer arm a timer
/// without reading a clock or holding a duration.
///
/// `TStepEf`, `TStepEfX1Pause` and `TStepX1` deliberately share one identity:
/// only one pilot detour runs at a time, so a new step supersedes the previous
/// one rather than racing it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AcTimer {
    /// The vehicle kept drawing power after the offer went away.
    /// `IECStateMachine.hpp:159`.
    C1,
    /// Bounds how long the connector stays locked while the port signals state
    /// F. `IECStateMachine.hpp:160`.
    CpStateFUnlock,
    /// Bounds the graceful stop before the hard stop. `Charger.hpp`
    /// `STOPPING_CHARGING_TIMEOUT_MS`.
    StoppingCharging,
    /// Bounds the five percent offer before falling back to nominal.
    /// `Charger.hpp` `AC_X1_FALLBACK_TO_NOMINAL_TIMEOUT_MS`.
    FivePercentFallback,
    /// Bounds the wait for the energy manager to supply power. `Charger.hpp`
    /// `WAIT_FOR_ENERGY_IN_AUTHLOOP_TIMEOUT_MS`.
    WaitForEnergy,
    /// State F held per ISO 15118-3 table 3. `Charger.hpp` `T_STEP_EF`.
    TStepEf,
    /// X1 held after state F. `Charger.hpp` `STAY_IN_X1_AFTER_TSTEP_EF_MS`.
    TStepEfX1Pause,
    /// X1 held per IEC 61851-1. `Charger.hpp` `T_STEP_X1`.
    TStepX1,
    /// How long the reinit holds its pilot level, `reinit_duration_ms`.
    ///
    /// Configured, so it travels on the variant for the same reason
    /// `SwitchPhases` does.
    Reinit(Duration),
    /// How long a high level session with no budget behind it is given before
    /// the charge is stopped, `hlc_charge_loop_without_energy_timeout_s`
    /// (`Charger.cpp:837-861`).
    ///
    /// Configured, so it travels on the variant as `Reinit` does.
    HlcNoEnergy(Duration),
    /// The break a phase change takes, `switch_3ph1ph_delay_s`.
    ///
    /// The only timer here that carries its own duration, because it is the
    /// only one an operator configures rather than a standard fixing. Carried
    /// on the variant rather than looked up so that `timer_after` stays the
    /// single table and does not need a configuration reference.
    SwitchPhases(Duration),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub enum IecCommand {
    Enable(bool),
    CpStateX1,
    CpStateF,
    /// Pilot at 0 V, which reads to the vehicle as an unplug. Only the reinit
    /// signals it, and only where the board has reported it can: the C++ guard
    /// is in `Charger::start_reinit`, which refuses a `CPStateE` reinit on
    /// hardware without `supports_cp_state_E`, so `Charger::cp_state_E`'s own
    /// fallback to X1 is unreachable from that route and is not ported.
    CpStateE,
    AllowPowerOn(bool),
    PwmOn(f64),
    PwmOff,
    SetOvercurrentLimitA(f64),
    /// Move the board's phase relays. `bsp->switch_three_phases_while_charging`,
    /// true for three phases.
    SwitchThreePhases(bool),
    LockConnector,
    UnlockConnector,
    /// Arm a timer whose lifetime belongs to the transition that emitted it.
    /// The C++ does the same, deciding `TimerControl::start` inside the pilot
    /// state handler; here the arming travels out as a command so the reducer
    /// still reads no clock.
    ArmTimer(AcTimer),
    CancelTimer(AcTimer),
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct IecConfig {
    pub initial_current_limit_a: f64,
    pub has_ventilation: bool,
    pub lock_connector_in_state_b: bool,
    /// `switch_3ph1ph_cp_state`, the pilot state the switching break signals.
    /// `Charger.cpp:584-588`, whose `switch_3ph1ph_cp_state_F` is
    /// `_switch_3ph1ph_cp_state == "F"` (`:1571`), so every other value means
    /// X1.
    pub switch_phases_cp_state: SwitchCpState,
    /// `reinit_method`, the pilot level the reinit holds.
    /// `Charger::apply_configured_reinit_method`.
    pub reinit_method: ReinitMethod,
    /// `reinit_duration_ms`, how long it is held. A zero duration means the
    /// reinit does not wait at all, which is the C++ `duration > 0` guard on
    /// arming its deadline.
    pub reinit_duration: Duration,
    /// `switch_3ph1ph_delay_s`, how long the break holds that pilot state
    /// before the board is told (`Charger.cpp:597`).
    pub switch_phases_delay: Duration,
    /// `hlc_charge_loop_without_energy_timeout_s`, how long a high level
    /// session with no budget behind it charges on before it is stopped. Zero
    /// means at once, which is the C++ `> 0` guard at `Charger.cpp:839`.
    pub hlc_no_energy_timeout: Duration,
    /// `connector_type == IEC62196Type2Socket`. On a socket the cable belongs
    /// to the vehicle, so its proximity pilot rating caps what may be offered;
    /// a port with its cable attached has no such reading and the budget
    /// stands alone. The one reader is `max_current_internal`.
    pub type2_socket: bool,
}

/// The budget below which the C++ calls power unavailable, `Charger.cpp:2126`.
/// Not the 6 A minimum of IEC 61851-1 table A.7: a budget reported as exactly
/// 6 A has to count as available, and a float comparison against 6 would make
/// that a coin toss.
pub const MIN_AVAILABLE_CURRENT_A: f64 = 5.9;

/// Duty cycle for a requested current, ported from
/// `Charger::ampere_to_duty_cycle` (`Charger.cpp:1356-1379`) rather than from
/// IEC 61851-1 table A.7 directly, because the C++ departs from the table in
/// three places and each departure is a different pilot signal on the wire:
///
/// - Below 5.9 A and above 80.1 A it signals a **full** duty cycle, which its
///   own comment calls the invalid argument case. This is reachable rather
///   than defensive: the `PrepareCharging` entry offers
///   `get_max_current_internal()` with no availability guard (`:747`), and the
///   expired budget fallback writes a zero allowance (`:2118-2123`).
/// - 5.9 A to 6.1 A is a flat ten percent rather than the scaled value, so a
///   board is offered the table's minimum for the whole band.
/// - 51 A to 52.5 A is the "weird gap in norm" the C++ names at `:1367`. The
///   current is clamped to 51 A there, so the gap signals eighty five percent
///   instead of entering the upper formula early.
///
/// The comparisons are in `f64` where the C++ narrows to `float`, which is the
/// port's width for every current. The literals are the same.
pub fn pwm_duty_for_current_a(current_a: f64) -> f64 {
    if current_a < 5.9 {
        1.0
    } else if current_a <= 6.1 {
        0.1
    } else if current_a < 52.5 {
        current_a.min(51.0) / 0.6 / 100.0
    } else if current_a <= 80.0 {
        ((current_a / 2.5) + 64.0) / 100.0
    } else if current_a <= 80.1 {
        0.97
    } else {
        1.0
    }
}

#[derive(Clone, Debug, PartialEq)]
pub struct Iec {
    config: IecConfig,
    state: AcState,
    current_limit_a: f64,
    /// `shared_context.iec_allow_close_contactor`, the vehicle's standing
    /// request for power rather than the edge that made it: state C or D out of
    /// B sets it (`IECStateMachine.cpp:241-243`) and state B out of either
    /// clears it (`:1186-1190` in `Charger`). It is a **latch**, which is what
    /// lets the `PrepareCharging` arm re-enter `Charging` for a vehicle that
    /// never left state C (`Charger.cpp:763-767`) with no fresh reading to
    /// read.
    iec_allows_close: bool,
    /// `shared_context.max_current_cable`, the cable's own rating as the
    /// proximity pilot reports it. `None` until a reading arrives and again
    /// after an unplug, which is the C++ `reset()` on the `Idle` entry
    /// (`Charger.cpp:235`).
    cable_rating_a: Option<f64>,
    pwm_running: bool,
    ev_plugged_in: bool,
    authorized: bool,
    transaction_active: bool,
    contactor_open: bool,
    disable_requested: bool,
    /// The phase count the board still owes, `switch_3ph1ph_threephase` under
    /// `switch_3ph1ph_threephase_ongoing` (`Charger.hpp:353-354`).
    ///
    /// One `Option` rather than the C++ pair: the value is only meaningful
    /// while a switch is outstanding, and the `ongoing` flag is exactly that
    /// question. `handle` will not let the state leave `SwitchPhases` with one
    /// standing, which is what `Charger.cpp:191-198` is for.
    pending_three_phases: Option<bool>,
    /// Where the stop in flight lands once the relays open. Meaningful only
    /// while resident in `StoppingCharging`; see `StoppingOutcome`.
    stopping: StoppingOutcome,
    /// Every state entered during the current `handle` call, in order.
    ///
    /// One input can carry the session across several states, which is what
    /// `Charger::run_state_machine` does with the `do { } while` at
    /// `Charger.cpp:1092`: it re-runs the switch until the state stops
    /// changing, and every pass runs the entry work of the state it landed in.
    /// An observer that only compared the state before the call with the state
    /// after it would see one edge where there were three and lose the entries
    /// in between.
    transitions: Vec<AcState>,
}

impl Iec {
    pub fn new(config: IecConfig) -> Self {
        Self {
            current_limit_a: config.initial_current_limit_a,
            iec_allows_close: false,
            cable_rating_a: None,
            config,
            state: AcState::Startup,
            pwm_running: false,
            ev_plugged_in: false,
            authorized: false,
            transaction_active: false,
            contactor_open: true,
            disable_requested: false,
            pending_three_phases: None,
            stopping: StoppingOutcome::Finished,
            transitions: Vec::new(),
        }
    }

    /// The states entered since the last call, in order, and clear.
    ///
    /// The state before the `handle` call plus this sequence is the whole route
    /// the input took.
    pub fn take_transitions(&mut self) -> Vec<AcState> {
        std::mem::take(&mut self.transitions)
    }

    /// The only way the state changes, so no route can enter a state without
    /// recording that it did.
    fn set_state(&mut self, next: AcState) {
        self.state = next;
        self.transitions.push(next);
    }

    pub fn state(&self) -> AcState {
        self.state
    }

    /// The one way into `StoppingCharging`, and the one place that says where
    /// the stop lands when the relays are observed open.
    ///
    /// Every route that halts a live session comes through here: the external
    /// cancel, the unplug, the vehicle side fault and the error shutdown all
    /// state `Finished`, and the EVSE pause states `PausedByEvse`. Folding the
    /// deadline in with the state is what makes it impossible to enter the
    /// state without bounding the wait, which `STOPPING_CHARGING_TIMEOUT_MS`
    /// measures.
    fn begin_stopping(&mut self, outcome: StoppingOutcome, commands: &mut Vec<IecCommand>) {
        self.stopping = outcome;
        self.set_state(AcState::StoppingCharging);
        commands.push(IecCommand::ArmTimer(AcTimer::StoppingCharging));
    }

    pub fn contactor_open(&self) -> bool {
        self.contactor_open
    }

    /// Single predicate for whether this state may carry a PWM offer.
    ///
    /// The C++ derives this set twice and the two answers disagree.
    /// `IECStateMachine::state_machine` clears `pwm_running` on entry to the
    /// `Disabled`, `A`, `E` and `F` control pilot states, which says only `B`,
    /// `C` and `D` may carry an offer. `IECStateMachine::set_pwm` gates on no
    /// state at all and calls the port's `pwm_on` from any of them. Because
    /// those clears are entry guarded, a `set_pwm` arriving while already
    /// resident in `F` raises `pwm_running` again and nothing lowers it, so the
    /// port signals fault and duty cycle at once.
    ///
    /// The narrow set wins: a duty cycle is an invitation to draw power, and no
    /// state in which the port is unavailable, faulted or vehicle free may
    /// extend one. Every offer site routes through here, so the two derivations
    /// cannot drift apart again.
    pub fn pwm_eligible(&self) -> bool {
        matches!(
            self.state,
            AcState::PrepareCharging | AcState::Charging | AcState::ChargingPausedEv
        )
    }

    /// The current the pilot is currently offering, as
    /// `Charger::get_max_current_signalled_to_ev_internal` answers it for basic
    /// charging: `internal_context.pwm_set_last_ampere` (`Charger.cpp:1929`).
    ///
    /// **This is the limit, not a separately tracked ampere, and that is a
    /// deliberate simplification of the C++.** `pwm_set_last_ampere` exists
    /// there because `update_pwm_max_every_5seconds_ampere` (`:1216-1226`) may
    /// defer a duty cycle change for up to five seconds, so the signalled
    /// current can lag the stored limit. This port applies a limit change to
    /// the pilot immediately (`set_current_limit_a` below), so the two figures
    /// cannot diverge and there is no second value to track.
    ///
    /// What does have to be modelled is the zero, and `pwm_running` is the
    /// whole of it. `Charger::cp_state_X1` (`:1262`) and `cp_state_F`
    /// (`:1271`) zero the field and clear `pwm_running` in the same call. A
    /// five percent offer is the third case the C++ zeroes for, through
    /// `update_pwm_now_if_changed(PWM_5_PERCENT)` taking a duty rather than an
    /// ampere and so never writing the field, and it needs no test of its own
    /// here: this reducer never issues a five percent offer, `AcHlc` pushes it
    /// as its own command, so `pwm_running` is already false for the whole of
    /// one. Deriving the five percent case a second time above this would be
    /// two answers to one question, which is the shape the C++ gets wrong
    /// about `pwm_eligible`. What holds it together is that a limit change
    /// during a five percent offer must not raise this figure, which
    /// `a_five_percent_offer_signals_no_current_even_after_a_limit_change` in
    /// `path::ac` pins.
    pub fn signalled_current_a(&self) -> f64 {
        if !self.pwm_running {
            return 0.0;
        }
        self.max_current_internal()
    }

    /// `Charger::get_max_current_internal` (`Charger.cpp:2047-2057`), the one
    /// accessor the C++ state machine reads its stored maximum through. Every
    /// figure that leaves this reducer for the board or the pilot comes from
    /// here, which is what keeps the cable's rating from applying to some of
    /// them and not others.
    ///
    /// On a Type 2 socket the cable is the vehicle's and its rating caps the
    /// offer. **A socket that has reported no rating caps at zero**: the C++
    /// compares `max_current_cable.value_or(0.0)` against the budget, so a
    /// socket whose cable has not been measured may not be offered current at
    /// all. `Idle` is excluded there and here, because a port with no vehicle
    /// has no cable to rate.
    fn max_current_internal(&self) -> f64 {
        if !self.config.type2_socket || self.state == AcState::Idle {
            return self.current_limit_a;
        }
        self.current_limit_a.min(self.cable_rating_a.unwrap_or(0.0))
    }

    /// The negotiated limit, which is the second branch of
    /// `get_max_current_signalled_to_ev_internal` (`Charger.cpp:2059-2066`).
    pub fn current_limit_a(&self) -> f64 {
        self.max_current_internal()
    }

    /// Whether the stored budget is one a charge can run on.
    /// `Charger::power_available`'s AC branch (`Charger.cpp:2126`), which reads
    /// the same accessor.
    pub fn power_available(&self) -> bool {
        self.max_current_internal() > MIN_AVAILABLE_CURRENT_A
    }

    /// `shared_context.max_current_cable = bsp->read_pp_ampacity()`
    /// (`Charger.cpp:47` and `:344`). The C++ reads the rating at those two
    /// points; here it arrives as a board reading and is stored, which is the
    /// same answer at every read that follows.
    ///
    /// A zero reading is no reading, which is what `read_pp_ampacity` reports
    /// for one (`EnergyTree::note_pp_ampacity` says the same of its copy).
    pub fn set_cable_rating_a(&mut self, ampacity_a: f64) -> Vec<IecCommand> {
        self.cable_rating_a = (ampacity_a > 0.0).then_some(ampacity_a);
        // The rating caps the offer, so a new one re-derives it exactly as a
        // new budget does.
        self.offer_current_limit()
    }

    /// `Charger::set_max_current` (`Charger.cpp:1381-1402`).
    ///
    /// **The stored limit is a magnitude.** The C++ says so outright at
    /// `:1395-1397`: `max_current` is `fabs(c)` and the sign of the allowance
    /// is used only to say which way the power is flowing, on the signal that
    /// leaves the module. So the board's overcurrent threshold and the duty
    /// cycle on the pilot are both derived from the magnitude, and an
    /// AC_BPT discharge allowance of -20 A is a 20 A threshold and the duty
    /// cycle for 20 A rather than a negative threshold and the invalid
    /// argument duty cycle.
    pub fn set_current_limit_a(&mut self, current_limit_a: f64) -> Vec<IecCommand> {
        self.current_limit_a = current_limit_a.abs();
        self.offer_current_limit()
    }

    /// What a new limit or a new cable rating owes the board and the vehicle:
    /// the overcurrent threshold, and the duty cycle if an offer is standing.
    /// Both from `max_current_internal`, which is where the C++ takes them
    /// from (`:1393-1394` for the threshold, `:908` for the duty cycle).
    ///
    /// An unavailable budget is not offered on the pilot. The C++ updates the
    /// duty cycle from inside the state machine arms, all of which sit
    /// **below** their `power_available()` test (`Charger.cpp:836` before
    /// `:908`, `:961` before `:969`), so a budget that fails it never reaches
    /// `ampere_to_duty_cycle` from a charging session. Offering it here would
    /// put the invalid argument duty cycle on the pilot for the moment before
    /// the withdrawal is acted on.
    fn offer_current_limit(&mut self) -> Vec<IecCommand> {
        let offer_a = self.max_current_internal();
        let mut commands = vec![IecCommand::SetOvercurrentLimitA(offer_a)];
        if self.pwm_eligible() && self.pwm_running && self.power_available() {
            commands.push(IecCommand::PwmOn(pwm_duty_for_current_a(offer_a)));
        }
        commands
    }

    pub fn handle(&mut self, input: IecInput) -> Vec<IecCommand> {
        self.transitions.clear();
        let was_switching = self.state == AcState::SwitchPhases;
        let mut commands = self.dispatch(input);
        // `Charger.cpp:191-198`. Every route out of the break owes the board
        // the phase count that was pending, including the ones that abandon it
        // without waiting, so the relays are never left on the count the
        // session started with while the energy manager believes otherwise.
        // The ordinary exit clears the pending value itself, before it moves
        // the state, so this only fires for the abandoned routes.
        if was_switching && self.state != AcState::SwitchPhases {
            if let Some(three_phases) = self.pending_three_phases.take() {
                commands.push(IecCommand::SwitchThreePhases(three_phases));
            }
        }
        commands
    }

    /// One input, without the switching break cleanup `handle` wraps around it.
    fn dispatch(&mut self, input: IecInput) -> Vec<IecCommand> {
        match input {
            IecInput::StartupComplete => self.startup_complete(),
            IecInput::CarPluggedIn => self.car_plugged_in(),
            IecInput::CarUnplugged => self.car_unplugged(),
            IecInput::AuthorizationAccepted => {
                if self.state == AcState::WaitingForAuthentication && self.ev_plugged_in {
                    self.authorized = true;
                }
                Vec::new()
            }
            IecInput::TransactionStarted => self.transaction_started(),
            IecInput::PowerOn => {
                self.contactor_open = false;
                Vec::new()
            }
            IecInput::PowerOff => {
                self.contactor_open = true;
                // `Charger.cpp:1035-1046`: the stop completes only once the
                // relays are observed open.
                let mut commands = Vec::new();
                self.settle(&mut commands);
                commands
            }
            IecInput::EmergencyShutdown | IecInput::ErrorShutdown => self.shutdown(),
            IecInput::CpStateF => {
                self.pwm_running = false;
                vec![
                    IecCommand::AllowPowerOn(false),
                    IecCommand::CpStateF,
                    // The connector stays locked while the port signals F so a
                    // vehicle cannot be pulled mid fault. This bounds the wait.
                    IecCommand::ArmTimer(AcTimer::CpStateFUnlock),
                ]
            }
            IecInput::FaultStateFExpired => vec![IecCommand::CpStateX1],

            IecInput::CarRequestedPower => self.car_requested_power(false),
            IecInput::CarRequestedVentilatedPower => self.car_requested_power(true),

            IecInput::CarRequestedStopPower => self.car_requested_stop_power(),
            IecInput::CpStateB => self.cp_state_b(),
            IecInput::StopRequested => self.stop_requested(),
            IecInput::PauseRequested => self.pause_requested(),
            IecInput::ResumeRequested => self.resume_requested(),
            IecInput::EnergyWithdrawn => self.energy_withdrawn(),
            IecInput::EnergyRestored => self.energy_restored(),

            IecInput::Enable => self.enable(),
            IecInput::Disable => self.disable(),
            IecInput::CpStateE => self.cp_state_e(),

            // The connector stays locked while the port signals state F so a
            // vehicle cannot be pulled mid fault. The timer bounds that wait.
            IecInput::CpStateFUnlockTimerExpired => vec![IecCommand::UnlockConnector],
            IecInput::CpStateC1TimeoutExpired => self.c1_timeout_expired(),
            IecInput::StoppingChargingTimeoutExpired => self.stopping_charging_timeout_expired(),
            IecInput::SessionRestart => self.session_restart(),
            IecInput::ReinitStarted => self.reinit_started(),
            IecInput::ReinitFinished => self.reinit_finished(),

            IecInput::SwitchPhasesRequested(three_phases) => {
                self.switch_phases_requested(three_phases)
            }
            IecInput::SwitchPhasesDelayExpired => self.switch_phases_delay_expired(),
        }
    }

    /// The two accepted routes of `Charger::switch_three_phases_while_charging`
    /// that do not call the board at once (`Charger.cpp:1531-1541`).
    ///
    /// The refusals and the direct board call are decided above this reducer,
    /// by the enforced limits handler that has the hardware capability and the
    /// high level communication flag. What reaches here is a request the
    /// charger state says owes a break.
    fn switch_phases_requested(&mut self, three_phases: bool) -> Vec<IecCommand> {
        match self.state {
            // `Charger.cpp:1532-1539`: record the value, remember where to come
            // back to, and enter the break. The relays are not touched.
            AcState::Charging => {
                self.pending_three_phases = Some(three_phases);
                self.set_state(AcState::SwitchPhases);
                // Both `cp_state_X1` (`:1262`) and `cp_state_F` (`:1271`) clear
                // `pwm_running` as they signal, so the offer is off for the
                // whole break and the vehicle has the reason it needs to stop
                // drawing before the relays move.
                self.pwm_running = false;
                vec![
                    // `Charger.cpp:1571` asks one question of the setting, so
                    // `E` signals X1 as every value but `F` does there.
                    if self.config.switch_phases_cp_state.signals_state_f() {
                        IecCommand::CpStateF
                    } else {
                        IecCommand::CpStateX1
                    },
                    IecCommand::ArmTimer(AcTimer::SwitchPhases(self.config.switch_phases_delay)),
                ]
            }

            // `Charger.cpp:1540-1541`: a second request during a break already
            // running replaces the pending value and does not restart the
            // break. The deadline is left where it is on purpose, so a stream
            // of requests cannot hold the pilot down indefinitely.
            AcState::SwitchPhases => {
                self.pending_three_phases = Some(three_phases);
                Vec::new()
            }

            // `Charger.cpp:1542` takes the direct board call from every other
            // state, and the handler above this reducer emits it there rather
            // than routing it through the break. Reaching here from one of them
            // is a routing mistake, and moving the relays anyway would do it
            // from a state that never decided to.
            AcState::Startup
            | AcState::Idle
            | AcState::WaitingForAuthentication
            | AcState::PrepareCharging
            | AcState::ChargingPausedEv
            | AcState::ChargingPausedEvse
            | AcState::StoppingCharging
            | AcState::Reinit
            | AcState::Finished
            | AcState::Disabled => {
                log::error!(
                    "a phase switch reached the switching break from {:?}, which takes the \
                     direct board call",
                    self.state
                );
                Vec::new()
            }
        }
    }

    /// `Charger.cpp:597-603`: the break ran its course.
    ///
    /// The board first and the state after, the order the C++ has, so the offer
    /// the `PrepareCharging` entry re-derives cannot go up before the relays
    /// have moved.
    fn switch_phases_delay_expired(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::SwitchPhases {
            return Vec::new();
        }
        let mut commands = Vec::new();
        if let Some(three_phases) = self.pending_three_phases.take() {
            commands.push(IecCommand::SwitchThreePhases(three_phases));
        }
        // `internal_context.switching_phases_return_state`, set to
        // `PrepareCharging` at `Charger.cpp:1538` and never to anything else.
        // Not `Charging`: the vehicle stopped drawing when the pilot dropped,
        // so the session has to be prepared again rather than resumed.
        self.set_state(AcState::PrepareCharging);
        // The `PrepareCharging` entry re-derives the offer
        // (`Charger.cpp:706-711`). Through the single `pwm_eligible` predicate,
        // as every other offer site is.
        if self.pwm_eligible() {
            self.pwm_running = true;
            commands.push(IecCommand::PwmOn(pwm_duty_for_current_a(
                self.max_current_internal(),
            )));
        }
        // `Charger.cpp:725-728`. A vehicle that held control pilot state C
        // through the break never opened S2, so its contactor is still closed
        // and it is still drawing; there is no state C edge left to wait for.
        // The C++ resident pass reads `iec_allow_close_contactor` and charges
        // again without one. Waiting instead leaves a session physically
        // charging under `PrepareCharging`, which runs no soft overcurrent
        // check and announced no `ChargingStarted`.
        //
        // The closed contactor is this port's reading of that flag, and it is
        // the same reading `resume_requested` already uses for the identical
        // case after a pause. Ordered after the switch and the offer, so the
        // relays have moved before the session charges again.
        //
        // The flag is CP derived in the C++ and the contactor is
        // board reported here, so the two disagree for as long as it takes the
        // board to report a contactor it has just been asked to open. Ceiling:
        // a vehicle that opens S2 in the last moments of the break can be read
        // as still drawing and charged again while it sits in state B, which
        // the next control pilot event corrects and during which no current
        // flows. Upgrade path: carry `iec_allow_close_contactor` itself, set
        // from the state C and state B handlers. Owner: RsEvseManager.
        if !self.contactor_open {
            commands.extend(self.grant_power());
        }
        commands
    }

    fn startup_complete(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::Startup {
            return Vec::new();
        }
        self.set_state(AcState::Idle);
        vec![
            IecCommand::Enable(true),
            IecCommand::AllowPowerOn(false),
            IecCommand::CpStateX1,
            IecCommand::SetOvercurrentLimitA(self.max_current_internal()),
        ]
    }

    fn car_plugged_in(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::Idle {
            return Vec::new();
        }
        self.set_state(AcState::WaitingForAuthentication);
        self.ev_plugged_in = true;
        let mut commands = Vec::new();
        if self.config.lock_connector_in_state_b {
            commands.push(IecCommand::LockConnector);
        }
        commands.push(IecCommand::AllowPowerOn(false));
        commands
    }

    /// `Charger.cpp:1203` clears the plug flag; the state the port is in
    /// decides what that fact costs.
    ///
    /// A live session owes a stop, which is why this does not land at rest
    /// directly: `Charger.cpp:690-695`, `:782-792`, `:880-884` and `:942-946`
    /// all route a departed vehicle into `StoppingCharging`, and it is the
    /// entry to that state at `Charger.cpp:1012` that announces the stop. The
    /// route onward from there is the settle loop's, not this function's.
    ///
    /// The connector is released here and only here, which is the one release
    /// path `Charger.cpp:1059-1084` allows. Nothing the settle loop does adds
    /// a second one.
    fn car_unplugged(&mut self) -> Vec<IecCommand> {
        self.ev_plugged_in = false;
        self.authorized = false;
        self.transaction_active = false;
        self.pwm_running = false;
        // `Charger.cpp:225`, the `Idle` entry: the request belongs to the
        // vehicle that made it.
        self.iec_allows_close = false;
        // `Charger.cpp:235`, the same entry. The next vehicle brings its own
        // cable, so the rating is re-read rather than carried: kept, a socket
        // that never reports again would cap the next session at the last
        // cable's rating.
        self.cable_rating_a = None;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::UnlockConnector,
            IecCommand::CpStateX1,
            IecCommand::CancelTimer(AcTimer::C1),
            IecCommand::CancelTimer(AcTimer::CpStateFUnlock),
        ];
        let before = self.state;
        self.settle(&mut commands);
        if self.state == AcState::StoppingCharging {
            // Parked on the relays. `Charger.cpp:1035-1038` waits here rather
            // than declaring the session over, and the `PowerOff` that reports
            // the relays open completes the route. The stopping deadline armed
            // on the way in bounds that wait.
            return commands;
        }
        if self.state == before {
            // No session route owns this state, so there is nothing to stop and
            // the unplug lands the port at rest directly.
            self.enter_resting(&mut commands);
        }
        commands
    }

    /// Re-run the exit conditions of the current state until it stops changing.
    ///
    /// `Charger::run_state_machine` wraps its state switch in a
    /// `do { } while` on the state having changed (`Charger.cpp:1092`), so one
    /// fact can carry the session across several states in a single pass and
    /// each state it lands in runs its own entry work. Jumping straight to the
    /// last state would skip the entries in between, and the stopping
    /// announcement is one of them.
    fn settle(&mut self, commands: &mut Vec<IecCommand>) {
        loop {
            let before = self.state;
            match self.state {
                // The vehicle left a live session. `Charger.cpp:690-695`,
                // `:782-792`, `:880-884` and `:942-946` answer that with the
                // stop route rather than a resting state, so the stop is
                // announced and the relays are given their chance to open.
                _ if !self.ev_plugged_in && self.charging_session_live() => {
                    // The only fact that reaches this arm is the unplug, whose
                    // own commands already withdrew the energy and the offer and
                    // put the pilot back on X1. What is left is the deadline
                    // `STOPPING_CHARGING_TIMEOUT_MS` measures, which `begin_stopping`
                    // arms.
                    self.begin_stopping(StoppingOutcome::Finished, commands);
                }

                // `Charger.cpp:591-596`: an unplug during the switching break
                // abandons it. The break decides nothing about what that costs;
                // it hands the session back to the state it was returning to
                // and that state's own checks tear it down, which is the arm
                // above on the next pass of this loop. The pending relay switch
                // still happens, through the cleanup in `handle`.
                AcState::SwitchPhases if !self.ev_plugged_in => {
                    self.set_state(AcState::PrepareCharging);
                }

                // `Charger.cpp:316-319`: nothing had started, so there is no
                // stop to run and no relays to wait on.
                AcState::WaitingForAuthentication if !self.ev_plugged_in => {
                    self.finish(commands);
                }

                // `Charger::run_state_machine`'s `StoppingCharging` arm: the stop
                // completes on the relays
                // being observed open and on nothing else. A deadline that
                // passed is not that observation, which is why the stopping
                // timeout stays a hard power off and not a state change.
                //
                // Where it completes is that arm's exit condition. Its
                // fatality guard reads three flags this reducer mirrors and one
                // it does not (`flag_authorized`, cleared here by the cancel),
                // and any of them says the session may not restart. Only with
                // all four clear does the branch below it separate the pause,
                // which settles into `ChargingPausedEVSE` with the transaction
                // still open, from everything else. That branch is a
                // disjunction of three (`:1088`) and two of the three are
                // reached here, each as the destination the stop was begun
                // for: the EVSE's own pause and a budget that went away.
                //
                // Ceiling: that branch's `ChargingPausedEV` is not reached. It
                // is the C++ answer for a halt that was not the EVSE's and left
                // the session intact, and the only route that gets here that
                // way is the fault, which this port finishes instead. Pinned by
                // `a_fault_tells_both_modes_why_and_only_ac_also_asks_them_to_stop`
                // in `core`.
                AcState::StoppingCharging if self.contactor_open => {
                    let resumable = matches!(
                        self.stopping,
                        StoppingOutcome::PausedByEvse | StoppingOutcome::NoEnergy
                    )
                        && self.transaction_active
                        && self.ev_plugged_in
                        && self.authorized
                        && !self.disable_requested;
                    if resumable {
                        self.set_state(AcState::ChargingPausedEvse);
                        // The graceful stop completed, so neither deadline is
                        // owed any longer. `finish` cancels the same pair on
                        // the routes that end the session.
                        commands.extend([
                            IecCommand::CancelTimer(AcTimer::C1),
                            IecCommand::CancelTimer(AcTimer::StoppingCharging),
                        ]);
                    } else {
                        self.finish(commands);
                    }
                }

                _ => {}
            }
            if self.state == before {
                return;
            }
        }
    }

    /// The port goes back to advertising availability, unless a disable request
    /// is outstanding, in which case it goes out of service instead.
    ///
    /// `Charger.cpp:237-238`: Idle is re-entered only with the request clear.
    /// Landing in Idle with it still set would let an unplug return a disabled
    /// port to advertising availability with no enable ever issued.
    /// `Charger.cpp:201-205` is the entry it lands on instead.
    fn enter_resting(&mut self, commands: &mut Vec<IecCommand>) {
        if self.disable_requested {
            self.set_state(AcState::Disabled);
            commands.push(IecCommand::CpStateF);
            commands.push(IecCommand::Enable(false));
        } else {
            self.set_state(AcState::Idle);
        }
    }

    /// The record is opened if none is open, and the state moves either way.
    ///
    /// The two are separate in the C++ and have to be:
    /// `WaitingForAuthentication` guards only the opening on
    /// `not flag_transaction_active` (`Charger::run_state_machine`'s `WaitingForAuthentication` external authorization arm and the plug and
    /// charge arm beside it) and then assigns `current_state` unconditionally.
    /// Refusing the whole input on an open record leaves every route that
    /// re-enters this state mid transaction stuck in it: the data link error
    /// restart, which `session_restart` reaches with the record open, and the
    /// reinitialization, which exists precisely to keep it open.
    fn transaction_started(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::WaitingForAuthentication
            || !self.authorized
            || !self.ev_plugged_in
        {
            return Vec::new();
        }
        self.transaction_active = true;
        self.set_state(AcState::PrepareCharging);
        // The state moves before the offer so the offer goes through the single
        // `pwm_eligible` predicate, as every other offer site does.
        if !self.pwm_eligible() {
            return Vec::new();
        }
        self.pwm_running = true;
        vec![IecCommand::PwmOn(pwm_duty_for_current_a(
            self.max_current_internal(),
        ))]
    }

    /// IEC 61851-1 control pilot state C, or state D when `ventilated`.
    ///
    /// The vehicle closes S2 and asks for energy. Power is granted only from a
    /// state that carries a PWM offer, which is the single `pwm_eligible`
    /// predicate rather than a per site state list.
    fn car_requested_power(&mut self, ventilated: bool) -> Vec<IecCommand> {
        // The request stands from here until the vehicle withdraws it, whatever
        // this pass decides to do about it. `IECStateMachine.cpp:241-243` sets
        // the latch on the reading itself, above every refusal below.
        self.iec_allows_close = true;
        // State C and state D both lock the connector, whether or not power
        // follows.
        let mut commands = vec![IecCommand::LockConnector];

        // State D on a port that cannot ventilate: withhold power rather than
        // charging. The vehicle outgasses and the port cannot clear it.
        let ventilation_refused = ventilated && !self.config.has_ventilation;

        if ventilation_refused || !self.pwm_eligible() || !self.pwm_running {
            commands.push(IecCommand::AllowPowerOn(false));
            return commands;
        }

        // `grant_power`'s precondition, stated where it is established: it
        // may only be reached from the preparation, or from a charge that is
        // already running and is re-reading its own state C. Anything else is
        // prepared first.
        //
        // Which is one state, the vehicle's own pause, and it is the arm
        // `Charger::process_cp_events_state`'s `ChargingPausedEV` case has: a
        // fresh state C there assigns `PrepareCharging`, not `Charging`, and
        // `Charger::run_state_machine`'s `PrepareCharging` arm is what reaches
        // `Charging` in the same loop. So a C++ deployment announces the
        // preparation on every EV side resume and anything counting session
        // events sees it. `ChargingPausedEvse` already resumes through the
        // preparation, in `resume_requested`, so both paused states take the
        // one route.
        //
        // Written against the precondition rather than against that state
        // name because the eligible set above is what makes the two
        // equivalent: a state that is neither the preparation nor a live
        // charge and still carries an offer is the vehicle's pause and nothing
        // else.
        if !matches!(self.state, AcState::PrepareCharging | AcState::Charging) {
            self.set_state(AcState::PrepareCharging);
        }

        commands.extend(self.grant_power());
        commands
    }

    /// Close the relays and charge: the only writer of `AcState::Charging`.
    ///
    /// Two callers, and they are the two the C++ has. The vehicle asking for
    /// power is one (`Charger.cpp:763` by way of the state C handler). The
    /// other is the `PrepareCharging` resident pass at `Charger.cpp:725-728`,
    /// which enters `Charging` without any vehicle edge at all when
    /// `iec_allow_close_contactor` is still set, and which is what recovers a
    /// vehicle that held state C through a switching break.
    ///
    /// Extracted rather than duplicated so that the entry conditions cannot
    /// drift between the two: whichever route arrives, the state and the
    /// permission are issued together and in that order.
    ///
    /// Being the only writer says nothing about where the callers arrive from,
    /// and the session events depend on that: `Charger` reaches
    /// `EvseState::Charging` from `Charger::run_state_machine`'s
    /// `PrepareCharging` arm and from nowhere else. Both callers here are
    /// resident in `PrepareCharging` when they arrive, or already charging
    /// from a repeated state C reading, which
    /// `charging_is_only_ever_entered_from_the_preparation` drives every state
    /// and input pair to check.
    fn grant_power(&mut self) -> Vec<IecCommand> {
        self.set_state(AcState::Charging);
        vec![IecCommand::AllowPowerOn(true)]
    }

    /// IEC 61851-1 control pilot state B, whatever preceded it.
    ///
    /// The lock assertion `IECStateMachine::state_machine` makes on every
    /// state B reading (`IECStateMachine.cpp:189-193`), outside the guard that
    /// decides the rest. It is on its own
    /// because state B is three different facts: a vehicle arriving, matching
    /// restarting after E or F, and the pause below. Only the third withdraws
    /// power, and only the previous pilot level tells them apart, which is
    /// `CpEdges::requested_stop_power`.
    fn cp_state_b(&mut self) -> Vec<IecCommand> {
        vec![if self.config.lock_connector_in_state_b {
            IecCommand::LockConnector
        } else {
            IecCommand::UnlockConnector
        }]
    }

    /// IEC 61851-1 control pilot state B reached from C or D.
    ///
    /// The vehicle opened S2. Power must be withdrawn within 100 ms, but the
    /// duty cycle offer stays up so the vehicle can resume without a new
    /// handshake, which is why `ChargingPausedEv` is PWM eligible.
    fn car_requested_stop_power(&mut self) -> Vec<IecCommand> {
        // The vehicle opened S2, so the standing request is gone.
        self.iec_allows_close = false;
        let mut commands = self.cp_state_b();

        // The vehicle did what the withdrawn offer asked of it, so the deadline
        // for removing power under load no longer applies.
        // `IECStateMachine::state_machine` (`IECStateMachine.cpp:203`).
        commands.push(IecCommand::CancelTimer(AcTimer::C1));

        if !self.charging_session_live() {
            return commands;
        }

        self.set_state(AcState::ChargingPausedEv);
        commands.push(IecCommand::AllowPowerOn(false));
        commands
    }

    /// The coordinator ends the session. Energy first, then the offer.
    fn stop_requested(&mut self) -> Vec<IecCommand> {
        // `Charger::cancel_transaction` withdraws the authorization on any open
        // transaction and tests no state at all, and the `StoppingCharging`
        // exit in `Charger::run_state_machine` is what then reads it. That is
        // load bearing on one route: a cancel arriving while an EVSE pause is
        // still waiting on the relays has to keep the session from resuming,
        // and the pause already withdrew the energy and the offer, so the
        // withdrawn authorization is the whole of what the cancel adds.
        if self.transaction_active {
            self.authorized = false;
        }
        if !self.charging_session_live() {
            return Vec::new();
        }
        let was_drawing_power = self.state == AcState::Charging;
        self.pwm_running = false;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateX1,
        ];
        if was_drawing_power {
            commands.push(IecCommand::ArmTimer(AcTimer::C1));
        }
        self.begin_stopping(StoppingOutcome::Finished, &mut commands);
        commands
    }

    /// The EVSE withdraws the offer without ending the session.
    ///
    /// A pause out of `Charging` takes the stop route to get there, which is
    /// the whole of `Charger::run_state_machine`'s `Charging` arm: it leaves
    /// for `StoppingCharging` on `flag_paused_by_evse` among its seven reasons, so
    /// the entry announces the stop and asks the vehicle to pause, and the exit
    /// above settles into `ChargingPausedEVSE` once the relays are open. Going
    /// straight to the paused state skips the entry, and the entry is the only
    /// producer of the ISO 15118-20 pause request.
    ///
    /// From the other three live states the flag is set and nothing moves,
    /// which is also the C++: only the `Charging` arm tests it, and
    /// `PrepareCharging` and the two paused states each act on it when they
    /// next reach `Charging`. Ceiling: this port has no route back through
    /// `Charging` from them, so a pause arriving there enters
    /// `ChargingPausedEvse` directly and the vehicle is told at the next
    /// stopping entry instead. That is the pre-existing behavior, kept because
    /// deferring it needs the arms this reducer does not have.
    fn pause_requested(&mut self) -> Vec<IecCommand> {
        if !self.charging_session_live() {
            return Vec::new();
        }
        let was_drawing_power = self.state == AcState::Charging;
        self.pwm_running = false;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateX1,
        ];
        if was_drawing_power {
            // X2 to C1: the vehicle has a bounded time to stop drawing before
            // power is removed under load. `IECStateMachine.cpp:241-245`.
            commands.push(IecCommand::ArmTimer(AcTimer::C1));
            self.begin_stopping(StoppingOutcome::PausedByEvse, &mut commands);
            // The state just entered runs its own exit conditions, so a vehicle
            // whose relays are already reported open reaches the paused state in
            // this pass rather than waiting for a board fact that has been and
            // gone. `Charger::run_state_machine`'s settle loop re-runs its
            // switch for the same reason.
            self.settle(&mut commands);
        } else {
            self.set_state(AcState::ChargingPausedEvse);
        }
        commands
    }

    /// The budget behind a live charge has gone.
    ///
    /// `Charger::run_state_machine`'s `Charging` arm tests `power_available()`
    /// above everything else it does and leaves for `StoppingCharging`
    /// (`Charger.cpp:836-871`), so the same stop route a pause takes runs and
    /// settles into `ChargingPausedEVSE` once the relays are open. The vehicle
    /// is drawing, so the offer goes away under the C1 deadline rather than the
    /// power being cut from under it.
    ///
    /// Only from `Charging`, which is the only arm that leaves on this fact.
    /// **Ceiling:** the C++ also moves a basic session out of
    /// `ChargingPausedEV` when the budget goes (`:960-966`), which this does
    /// not: no power is flowing there, the mode fact that arm reads lives on
    /// the path rather than here, and the reviewer's finding is the live charge.
    /// A session paused by the vehicle therefore learns of the withdrawal when
    /// it next asks for power.
    fn energy_withdrawn(&mut self) -> Vec<IecCommand> {
        // The budget is re-read rather than taken on trust, because the C++
        // branch runs inside its own `if (not power_available())` and a
        // deadline can fire in the same pass as a budget arriving.
        if self.state != AcState::Charging || self.power_available() {
            return Vec::new();
        }
        self.pwm_running = false;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateX1,
            // X2 to C1, as the EVSE pause does: the vehicle has a bounded time
            // to stop drawing before power is removed under load.
            IecCommand::ArmTimer(AcTimer::C1),
        ];
        self.begin_stopping(StoppingOutcome::NoEnergy, &mut commands);
        // The state just entered runs its own exit conditions, so relays
        // already reported open settle in this pass.
        self.settle(&mut commands);
        commands
    }

    /// The budget came back while the session was paused for the want of it.
    ///
    /// The C++ recomputes the three pause reasons on every pass of the
    /// `ChargingPausedEVSE` arm and resumes into `PrepareCharging` once none is
    /// left (`Charger.cpp:1004-1032`). The reducer holds one reason rather than
    /// three, the one the stop was begun for, so a pause the operator asked for
    /// is not lifted by a budget arriving: that one is resumed by its own
    /// request, and `resume_requested` refuses while the budget is still short.
    fn energy_restored(&mut self) -> Vec<IecCommand> {
        if self.stopping != StoppingOutcome::NoEnergy {
            return Vec::new();
        }
        self.resume_requested()
    }

    /// The EVSE restores the offer it withdrew.
    ///
    /// The state moves first so the PWM offer is issued from a state the single
    /// `pwm_eligible` predicate admits.
    ///
    /// Refused while the budget is short, which is the C++ answer rather than
    /// an addition: the `ChargingPausedEVSE` arm keeps `NoEnergy` in its reason
    /// list and stays paused, so a resume arriving with nothing to offer does
    /// not move the session.
    fn resume_requested(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::ChargingPausedEvse || !self.power_available() {
            return Vec::new();
        }
        self.set_state(AcState::PrepareCharging);
        if !self.pwm_eligible() {
            return Vec::new();
        }
        self.pwm_running = true;
        let mut commands = vec![IecCommand::PwmOn(pwm_duty_for_current_a(
            self.max_current_internal(),
        ))];
        // A vehicle that never left control pilot state C across the pause
        // still has its request standing. Allow power again with the offer
        // rather than waiting for a state B to C transition that will not come,
        // which is what `Charger.cpp:763-767` does with the same latch.
        //
        // The relays are deliberately **not** the question. They were the proxy
        // here before, and it read wrong in both directions: a vehicle holding
        // state C whose relays had been reported open was refused, and a
        // vehicle that had withdrawn its request was allowed power for as long
        // as the board had not yet reported the relays open.
        if self.iec_allows_close {
            commands.push(IecCommand::CancelTimer(AcTimer::C1));
            commands.push(IecCommand::AllowPowerOn(true));
        }
        commands
    }

    /// Availability arbitration above this reducer resolved to available.
    fn enable(&mut self) -> Vec<IecCommand> {
        if !self.disable_requested {
            return Vec::new();
        }
        self.disable_requested = false;
        self.set_state(AcState::Idle);
        // The board first, then availability: `Charger.cpp:1730` starts the
        // output and the Idle entry it hands to signals X1 at `:230`. Emitting
        // only the enable leaves the pilot on the state F the disable set, so
        // the port never invites a vehicle again.
        vec![IecCommand::Enable(true), IecCommand::CpStateX1]
    }

    /// Availability arbitration resolved to unavailable.
    ///
    /// Ported from the control pilot `Disabled` arm at `Charger.cpp:201-205`:
    /// withdraw energy and the offer, signal state F, then stop the port. The
    /// port stops last so it is never disabled while still holding a closed
    /// contactor, and the control pilot state precedes it because the C++ keeps
    /// both statements in that handler for exactly that ordering
    /// (`Charger.cpp:1763`).
    ///
    /// State F rather than X1: X1 offers availability without a duty cycle,
    /// while F says the port is unavailable, which is what a disabled port is.
    ///
    /// The vehicle is deliberately not released. `Charger.cpp` never unlocks on
    /// an availability change, and an operator disabling an occupied port routes
    /// the session through stopping instead of popping the latch.
    fn disable(&mut self) -> Vec<IecCommand> {
        self.disable_requested = true;
        self.pwm_running = false;
        self.authorized = false;
        self.transaction_active = false;
        self.set_state(AcState::Disabled);
        vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateF,
            IecCommand::CancelTimer(AcTimer::C1),
            IecCommand::CancelTimer(AcTimer::CpStateFUnlock),
            IecCommand::CancelTimer(AcTimer::StoppingCharging),
            IecCommand::Enable(false),
        ]
    }

    /// IEC 61851-1 control pilot state E, the diode or short fault.
    ///
    /// The vehicle side is no longer signalling validly, so nothing about it can
    /// be trusted. Energy off, offer down, connector released.
    fn cp_state_e(&mut self) -> Vec<IecCommand> {
        self.pwm_running = false;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateX1,
            IecCommand::UnlockConnector,
            IecCommand::CancelTimer(AcTimer::C1),
        ];
        if self.session_or_break_live() {
            self.begin_stopping(StoppingOutcome::Finished, &mut commands);
        }
        commands
    }

    /// The vehicle kept drawing power after the duty cycle was withdrawn.
    ///
    /// Powering off under load is the last resort, and the offer is deliberately
    /// left alone: dropping it would tell the vehicle nothing it has not already
    /// ignored.
    fn c1_timeout_expired(&mut self) -> Vec<IecCommand> {
        if !self.charging_session_live() {
            return Vec::new();
        }
        vec![IecCommand::AllowPowerOn(false)]
    }

    /// The stop sequence ran out of time to complete gracefully.
    ///
    /// This removes energy under load and nothing else. It is deliberately not
    /// a state change: `Charger.cpp:1027-1032` performs the hard stop and then
    /// keeps waiting on the contactor, because the session ends when the relays
    /// are observed open and not when a deadline passes. Ending the session here
    /// would report a clean stop while the relays may still be closed.
    fn stopping_charging_timeout_expired(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::StoppingCharging {
            return Vec::new();
        }
        vec![IecCommand::AllowPowerOn(false)]
    }

    /// The stop completed: the relays are open. `Charger.cpp:1041-1046`.
    ///
    /// The connector is deliberately not released. A finished session holds the
    /// vehicle until it is unplugged, which is what `Charger.cpp:1059-1084`
    /// does: only the unplug path unlocks.
    fn finish(&mut self, commands: &mut Vec<IecCommand>) {
        self.set_state(AcState::Finished);
        self.authorized = false;
        self.transaction_active = false;
        self.pwm_running = false;
        commands.extend([
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateX1,
            IecCommand::CancelTimer(AcTimer::C1),
            IecCommand::CancelTimer(AcTimer::StoppingCharging),
        ]);
        // `Charger.cpp:1080-1083`: a finished session with no vehicle left to
        // hold, or with the port due out of service, goes to rest at once.
        // Holding here instead would strand a departed vehicle's session open.
        if !self.ev_plugged_in || self.disable_requested {
            self.enter_resting(commands);
        }
    }

    /// High level communication never came up on the five percent offer, so fall
    /// back to the nominal duty cycle for the current limit.
    ///
    /// No power path emits this yet, and that is not an oversight. It re-offers
    /// from a state that already carries an offer, which is what an ISO 15118
    /// data link error needs: `Charger.cpp:365-374` flags that fall back as
    /// unimplemented in the C++. The routes out of five percent that do exist
    /// all withdraw the offer through the pilot detour first, so they offer from
    /// `WaitingForAuthentication` instead. This becomes reachable once a data
    /// link error arrives as an event.
    /// Back to `WaitingForAuthentication` from a live session.
    ///
    /// The route `Charger::dlink_error` builds: `t_step_EF_return_state` is
    /// `EvseState::WaitingForAuthentication` (`Charger.cpp:2101`), reached with
    /// a zero duty cycle, and the transaction and the authorization both stay
    /// as they were, which is what lets the session restart rather than end.
    /// The caller re-derives the offer, because that is a path decision.
    ///
    /// Only a live session can restart. A port at rest is already where this
    /// would send it, and one out of service must not be moved back into a
    /// state that advertises availability.
    fn session_restart(&mut self) -> Vec<IecCommand> {
        if !self.charging_session_live() {
            return Vec::new();
        }
        self.pwm_running = false;
        self.set_state(AcState::WaitingForAuthentication);
        // `Charger.cpp:265`, the entry to that state, withdraws the contactor
        // permission. The relays cannot stay closed across a restart.
        vec![IecCommand::AllowPowerOn(false), IecCommand::PwmOff]
    }

    /// Enter the reinitialization state: withdraw everything the vehicle was
    /// offered and hold the configured pilot level.
    ///
    /// This is the `EvseState::Reinit` `initialize_state` block. Both contactor
    /// permissions are cleared there; the high level half lives on the power
    /// path above this reducer, so what is here is the IEC half, which is the
    /// power grant, plus the offer the pilot level takes with it.
    ///
    /// The two refusals are `Charger::start_reinit`'s own: a port out of
    /// service and a port with no vehicle on it have nothing to reintroduce.
    /// `Startup` joins them because it is the port before its first boot, which
    /// the C++ has no state for. `Reinit` joins them as the `reinit_running`
    /// guard: a second request while the sequence runs is skipped rather than
    /// restarting it, so a stream of requests cannot hold the pilot down.
    ///
    /// A zero duration runs the whole sequence in one call. `Charger` leaves
    /// `reinit_timer_active` false in that case and its exit condition is then
    /// satisfied on the same pass, so the pilot level is signalled and the
    /// session restarts without waiting. Modelling it as an armed timer of zero
    /// would need the loop to come back for a deadline that has already passed.
    fn reinit_started(&mut self) -> Vec<IecCommand> {
        if matches!(
            self.state,
            AcState::Startup | AcState::Idle | AcState::Disabled | AcState::Reinit
        ) {
            return Vec::new();
        }
        self.pwm_running = false;
        self.set_state(AcState::Reinit);
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            match self.config.reinit_method {
                ReinitMethod::CpStateE => IecCommand::CpStateE,
                ReinitMethod::CpStateF => IecCommand::CpStateF,
                ReinitMethod::CpStateX1 => IecCommand::CpStateX1,
            },
        ];
        if self.config.reinit_duration.is_zero() {
            commands.extend(self.reinit_finished());
            return commands;
        }
        commands.push(IecCommand::ArmTimer(AcTimer::Reinit(
            self.config.reinit_duration,
        )));
        commands
    }

    /// The reinit held its pilot level long enough, so the session restarts.
    ///
    /// The exit of the `EvseState::Reinit` case: X1, then
    /// `WaitingForAuthentication`. The contactor permission is withdrawn with
    /// it for the same reason `session_restart` withdraws it, because that state
    /// is what both of them enter and its entry is what clears the permission.
    ///
    /// The transaction is deliberately left open. Nothing in the C++ `Reinit`
    /// case touches `flag_transaction_active` or `flag_authorized`, and that is
    /// the whole reason `ac_with_soc` can reintroduce a vehicle mid session
    /// without closing its billing record.
    fn reinit_finished(&mut self) -> Vec<IecCommand> {
        if self.state != AcState::Reinit {
            return Vec::new();
        }
        self.pwm_running = false;
        self.set_state(AcState::WaitingForAuthentication);
        let mut commands = vec![IecCommand::AllowPowerOn(false), IecCommand::CpStateX1];
        // The state just entered runs its own exit conditions, which is what
        // sends a session whose vehicle left while the pilot was held on to
        // rest rather than leaving it waiting for an authorization nobody will
        // give. `Charger` reaches the same place one pass later.
        self.settle(&mut commands);
        commands
    }

    /// True while a transaction holds the port, whether or not energy flows.
    fn charging_session_live(&self) -> bool {
        matches!(
            self.state,
            AcState::PrepareCharging
                | AcState::Charging
                | AcState::ChargingPausedEv
                | AcState::ChargingPausedEvse
        )
    }

    /// Whether a session is in progress, the switching break included.
    ///
    /// Deliberately a different set from `charging_session_live`, which is what
    /// the stop and pause routes read. The break is not in that one because
    /// `Charger::stop_transaction` and `pause_charging` only set flags
    /// (`Charger.cpp:1330-1336`) and the `SwitchPhases` case reads neither, so
    /// the C++ lets the break finish and the return state acts on them. A fatal
    /// control pilot state is the opposite: the case does test
    /// `stop_charging_on_fatal_error_internal` (`:590`), so it abandons the
    /// break at once.
    fn session_or_break_live(&self) -> bool {
        self.charging_session_live() || self.state == AcState::SwitchPhases
    }

    /// Energy removal precedes vehicle release.
    ///
    /// A fault says nothing about availability. With a disable outstanding the
    /// port stays out of service: `Charger.cpp:201-209` leaves the disabled arm
    /// on an enable and on nothing else, and landing in stopping instead would
    /// report a port back in service that no enable ever reached. No stopping
    /// deadline is armed there either, because a port that is out of service
    /// has no session to stop. The bounded hold on the connector stays, because
    /// it is the fault that justifies it and not the availability change.
    fn shutdown(&mut self) -> Vec<IecCommand> {
        self.pwm_running = false;
        let mut commands = vec![
            IecCommand::AllowPowerOn(false),
            IecCommand::PwmOff,
            IecCommand::CpStateF,
            IecCommand::CancelTimer(AcTimer::C1),
            IecCommand::ArmTimer(AcTimer::CpStateFUnlock),
        ];
        if self.disable_requested {
            self.set_state(AcState::Disabled);
            return commands;
        }
        self.begin_stopping(StoppingOutcome::Finished, &mut commands);
        commands
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn iec() -> Iec {
        Iec::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        })
    }

    fn iec_without_ventilation() -> Iec {
        Iec::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: false,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        })
    }

    /// Drives the reducer to `PrepareCharging`, the state in which the vehicle's
    /// power request is expected.
    fn prepare_charging(iec: &mut Iec) {
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        assert_eq!(iec.state(), AcState::PrepareCharging);
    }

    /// Every branch of `Charger::ampere_to_duty_cycle` and both sides of every
    /// boundary between them, because the C++ answer is not the table's in
    /// three of the six bands and a port that reads the table instead signals
    /// a different pilot for each.
    #[test]
    fn duty_cycle_answers_as_ampere_to_duty_cycle_does() {
        let cases = [
            // The invalid argument band below the minimum, which the C++
            // signals as a full duty cycle. A zero allowance lands here.
            (0.0, 1.0),
            (5.0, 1.0),
            (5.89, 1.0),
            // The flat ten percent band, which the formula would scale.
            (5.9, 0.1),
            (6.0, 0.1),
            (6.1, 0.1),
            // The scaled band.
            (6.2, 0.103_333_333_333_333_33),
            (30.0, 0.5),
            (51.0, 0.85),
            // The gap in the norm: clamped to 51 A, so eighty five percent
            // stands across all of it rather than the upper formula starting
            // early.
            (51.5, 0.85),
            (52.4, 0.85),
            // The upper formula, which the gap ends at rather than 51 A.
            (52.5, 0.85),
            (70.0, 0.92),
            (80.0, 0.96),
            // The single value above the formula that is still not an error.
            (80.1, 0.97),
            (80.2, 1.0),
            (100.0, 1.0),
        ];
        for (current_a, duty) in cases {
            let answer = pwm_duty_for_current_a(current_a);
            assert!(
                (answer - duty).abs() < 1e-9,
                "{current_a} A must signal {duty}, got {answer}"
            );
        }
    }

    #[test]
    fn startup_is_idempotent() {
        let mut iec = iec();
        assert!(!iec.handle(IecInput::StartupComplete).is_empty());
        assert_eq!(iec.state(), AcState::Idle);
        assert!(iec.handle(IecInput::StartupComplete).is_empty());
    }

    #[test]
    fn evse_paused_state_is_not_pwm_eligible() {
        // The single predicate the previous implementation disagreed with itself
        // about across three functions.
        let mut iec = iec();
        iec.state = AcState::ChargingPausedEvse;
        assert!(!iec.pwm_eligible());
        iec.state = AcState::ChargingPausedEv;
        assert!(iec.pwm_eligible());
    }

    /// A vehicle drawing power, which is where a phase change owes a break.
    fn charging_iec() -> Iec {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);
        iec
    }

    /// `Charger::get_max_current_internal` (`Charger.cpp:2047-2057`). On a
    /// socket the cable is the vehicle's and its rating caps every figure the
    /// charger derives from its budget: the board's overcurrent threshold, the
    /// duty cycle on the pilot, and whether power counts as available.
    ///
    /// Before this the rating narrowed only what the energy node republished,
    /// so a 32 A allowance on a 13 A cable drove a 32 A pilot and a 32 A
    /// threshold.
    fn socket_iec() -> Iec {
        let mut iec = Iec::new(IecConfig {
            type2_socket: true,
            ..socket_config()
        });
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);
        iec
    }

    fn socket_config() -> IecConfig {
        IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        }
    }

    #[test]
    fn a_socket_offers_no_more_than_the_cable_is_rated_for() {
        let mut iec = socket_iec();
        iec.set_cable_rating_a(13.0);

        let commands = iec.set_current_limit_a(32.0);

        assert_eq!(
            commands,
            vec![
                IecCommand::SetOvercurrentLimitA(13.0),
                IecCommand::PwmOn(pwm_duty_for_current_a(13.0)),
            ],
            "the cable caps the threshold and the pilot together"
        );
        assert_eq!(iec.current_limit_a(), 13.0);

        // A cable rated above the budget caps nothing.
        iec.set_cable_rating_a(63.0);
        assert_eq!(iec.current_limit_a(), 32.0);
    }

    /// The rating arriving after the budget re-derives the offer, because the
    /// two arrive in either order and whichever is second decides.
    #[test]
    fn a_cable_rating_arriving_second_narrows_the_offer_it_finds() {
        let mut iec = socket_iec();
        iec.set_current_limit_a(32.0);

        let narrowed = iec.set_cable_rating_a(13.0);

        assert_eq!(
            narrowed,
            vec![
                IecCommand::SetOvercurrentLimitA(13.0),
                IecCommand::PwmOn(pwm_duty_for_current_a(13.0)),
            ]
        );
    }

    /// A socket that has reported no rating offers nothing. The C++ compares
    /// `max_current_cable.value_or(0.0)`, so an unmeasured cable is a zero
    /// rating rather than an absent cap, and the port must not charge on one.
    #[test]
    fn a_socket_with_no_cable_rating_offers_nothing() {
        let mut iec = socket_iec();

        iec.set_current_limit_a(32.0);

        assert_eq!(iec.current_limit_a(), 0.0);
        assert!(!iec.power_available(), "nothing to charge on");

        // A zero reading is no reading, which is what `read_pp_ampacity`
        // reports for one.
        iec.set_cable_rating_a(0.0);
        assert_eq!(iec.current_limit_a(), 0.0);
    }

    /// The rating is not carried across an unplug: the next vehicle brings its
    /// own cable and the C++ re-reads it on the `Idle` entry.
    #[test]
    fn an_unplug_forgets_the_cable_that_was_attached() {
        let mut iec = socket_iec();
        iec.set_cable_rating_a(13.0);
        iec.set_current_limit_a(32.0);
        assert_eq!(iec.current_limit_a(), 13.0, "the control");

        iec.handle(IecInput::CarUnplugged);
        // The unplug parks on the relays; the report that they are open is
        // what completes the route to rest.
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::Idle);
        // Idle is outside the cap in the C++ too, so the budget reads whole
        // here and the cap returns with the next vehicle.
        assert_eq!(iec.current_limit_a(), 32.0);

        iec.handle(IecInput::CarPluggedIn);
        assert_eq!(
            iec.current_limit_a(),
            0.0,
            "the new cable has not reported yet"
        );
    }

    /// A port with its cable attached has no rating to read, so the budget
    /// stands whatever the board reports.
    #[test]
    fn a_fixed_cable_port_ignores_a_proximity_pilot_reading() {
        let mut iec = charging_iec();
        iec.set_cable_rating_a(13.0);

        iec.set_current_limit_a(32.0);

        assert_eq!(iec.current_limit_a(), 32.0);
    }

    /// `Charger.cpp:1382` and `:1395-1397`. A discharge allowance is stored as
    /// its magnitude and the sign is kept only for the signal that leaves the
    /// module, so the board's overcurrent threshold is a positive current and
    /// the pilot carries the duty cycle for that current.
    ///
    /// Reachable on AC_BPT alone, which is the one service whose allowance the
    /// energy handler passes through with its sign
    /// (`energyImpl.cpp:556-563`); everything else is already floored at zero
    /// before it gets here.
    #[test]
    fn a_discharge_allowance_is_stored_and_signalled_as_a_magnitude() {
        let mut iec = charging_iec();

        let commands = iec.set_current_limit_a(-20.0);

        assert_eq!(
            commands,
            vec![
                IecCommand::SetOvercurrentLimitA(20.0),
                IecCommand::PwmOn(pwm_duty_for_current_a(20.0)),
            ],
            "a negative threshold is not a threshold, and the pilot carries 20 A"
        );
        assert_eq!(iec.current_limit_a(), 20.0);
        assert!(
            iec.power_available(),
            "a discharge is power available: the C++ asks this of the magnitude"
        );
    }

    /// `Charger.cpp:836-871`. The budget behind a live charge goes, so the
    /// charge stops rather than standing on a duty cycle nothing can honour.
    /// The stop route runs, which is what the pause takes, so the relays are
    /// waited on and the session settles into the paused state with its
    /// transaction still open.
    #[test]
    fn a_budget_that_goes_short_stops_a_live_charge() {
        let mut iec = charging_iec();

        let withdrawn = iec.handle(IecInput::EnergyWithdrawn);
        assert_eq!(
            iec.state(),
            AcState::Charging,
            "a budget that is still there withdraws nothing"
        );
        assert!(withdrawn.is_empty(), "{withdrawn:?}");

        iec.set_current_limit_a(0.0);
        let stopping = iec.handle(IecInput::EnergyWithdrawn);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert_eq!(
            stopping,
            vec![
                IecCommand::AllowPowerOn(false),
                IecCommand::PwmOff,
                IecCommand::CpStateX1,
                IecCommand::ArmTimer(AcTimer::C1),
                IecCommand::ArmTimer(AcTimer::StoppingCharging),
            ],
            "the offer goes away under the C1 deadline, as an EVSE pause does"
        );

        iec.handle(IecInput::PowerOff);
        assert_eq!(
            iec.state(),
            AcState::ChargingPausedEvse,
            "the relays opened, so the stop settles as a pause"
        );
    }

    /// The other half: the budget comes back and the session charges again
    /// within the same plug in, which is the `ChargingPausedEVSE` arm finding
    /// its reason list empty (`Charger.cpp:1004-1032`).
    #[test]
    fn the_budget_returning_resumes_the_charge_it_paused() {
        let mut iec = charging_iec();
        iec.set_current_limit_a(0.0);
        iec.handle(IecInput::EnergyWithdrawn);
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse, "the control");

        let short = iec.handle(IecInput::EnergyRestored);
        assert!(short.is_empty(), "still nothing to offer: {short:?}");
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);

        iec.set_current_limit_a(16.0);
        let resumed = iec.handle(IecInput::EnergyRestored);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert!(
            resumed.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(16.0))),
            "{resumed:?}"
        );
    }

    /// A budget arriving is not an answer to a pause nobody asked the energy
    /// manager about. The C++ keeps three reasons and lifts the pause only with
    /// all three gone; the reducer keeps the one the stop was begun for, so an
    /// operator's pause stands until its own resume.
    #[test]
    fn a_budget_that_returns_does_not_lift_an_operators_pause() {
        let mut iec = charging_iec();
        iec.handle(IecInput::PauseRequested);
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse, "the control");

        let restored = iec.handle(IecInput::EnergyRestored);

        assert!(restored.is_empty(), "{restored:?}");
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);

        let resumed = iec.handle(IecInput::ResumeRequested);
        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert!(resumed.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(16.0))));
    }

    /// And a resume arriving with nothing to offer does not move the session,
    /// because `NoEnergy` is still in the C++ reason list when the arm runs.
    #[test]
    fn a_resume_with_no_budget_is_refused() {
        let mut iec = charging_iec();
        iec.handle(IecInput::PauseRequested);
        iec.handle(IecInput::PowerOff);
        iec.set_current_limit_a(0.0);

        let refused = iec.handle(IecInput::ResumeRequested);

        assert!(refused.is_empty(), "{refused:?}");
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);
    }

    /// The pilot is never offered a budget the session cannot run on. Every
    /// C++ duty cycle update sits below its arm's `power_available()` test, so
    /// the invalid argument duty cycle for a short budget is not a signal a
    /// charging session ever sees.
    #[test]
    fn an_unavailable_budget_is_not_offered_on_the_pilot() {
        let mut iec = charging_iec();

        let short = iec.set_current_limit_a(0.0);

        assert_eq!(
            short,
            vec![IecCommand::SetOvercurrentLimitA(0.0)],
            "the board learns the limit and the vehicle is offered nothing"
        );

        let ample = iec.set_current_limit_a(16.0);
        assert!(
            ample.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(16.0))),
            "{ample:?}"
        );
    }

    fn iec_with_break_in_state_f() -> Iec {
        let mut iec = Iec::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::F,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        });
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        iec.handle(IecInput::CarRequestedPower);
        iec
    }

    /// The reinitialization, which is `EvseState::Reinit` plus the two
    /// transitions into and out of it.
    mod reinit {
        use super::*;

        fn iec_with_reinit(method: ReinitMethod, duration: Duration) -> Iec {
            Iec::new(IecConfig {
                initial_current_limit_a: 16.0,
                has_ventilation: true,
                lock_connector_in_state_b: true,
                switch_phases_cp_state: SwitchCpState::X1,
                switch_phases_delay: Duration::from_secs(10),
                reinit_method: method,
                reinit_duration: duration,
                hlc_no_energy_timeout: Duration::from_secs(5),
                type2_socket: false,
            })
        }

        fn charging_with(method: ReinitMethod, duration: Duration) -> Iec {
            let mut iec = iec_with_reinit(method, duration);
            iec.handle(IecInput::StartupComplete);
            iec.handle(IecInput::CarPluggedIn);
            iec.handle(IecInput::AuthorizationAccepted);
            iec.handle(IecInput::TransactionStarted);
            iec.handle(IecInput::PowerOn);
            iec.handle(IecInput::CarRequestedPower);
            assert_eq!(iec.state(), AcState::Charging);
            iec
        }

        /// The `initialize_state` block of the `Reinit` case: the contactor
        /// permission goes, the offer goes, the configured pilot level is
        /// signalled, and the hold is bounded.
        #[test]
        fn entering_the_reinit_withdraws_power_and_the_offer_before_holding_the_pilot() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));

            let commands = iec.handle(IecInput::ReinitStarted);

            assert_eq!(iec.state(), AcState::Reinit);
            assert_eq!(
                commands,
                vec![
                    IecCommand::AllowPowerOn(false),
                    IecCommand::PwmOff,
                    IecCommand::CpStateF,
                    IecCommand::ArmTimer(AcTimer::Reinit(Duration::from_millis(3000))),
                ]
            );
        }

        /// `Charger::apply_configured_reinit_method`, one row per spelling.
        #[test]
        fn each_configured_method_signals_its_own_pilot_level() {
            for (method, expected) in [
                (ReinitMethod::CpStateE, IecCommand::CpStateE),
                (ReinitMethod::CpStateF, IecCommand::CpStateF),
                (ReinitMethod::CpStateX1, IecCommand::CpStateX1),
            ] {
                let mut iec = charging_with(method, Duration::from_millis(3000));
                let commands = iec.handle(IecInput::ReinitStarted);
                assert!(
                    commands.contains(&expected),
                    "{method:?} must signal {expected:?}, got {commands:?}"
                );
            }
        }

        /// The hold carries the configured duration, so nothing else has to
        /// look it up.
        #[test]
        fn the_hold_carries_the_configured_duration() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(7500));
            let commands = iec.handle(IecInput::ReinitStarted);
            assert!(
                commands.contains(&IecCommand::ArmTimer(AcTimer::Reinit(
                    Duration::from_millis(7500)
                ))),
                "got {commands:?}"
            );
        }

        /// `Charger::start_reinit`'s two refusals, plus the two states the port
        /// has and the C++ does not.
        #[test]
        fn a_reinit_from_a_state_with_nothing_to_reintroduce_moves_nothing() {
            for state in [
                AcState::Startup,
                AcState::Idle,
                AcState::Disabled,
                AcState::Reinit,
            ] {
                let mut iec = iec_with_reinit(ReinitMethod::CpStateF, Duration::from_millis(3000));
                iec.state = state;

                let commands = iec.handle(IecInput::ReinitStarted);

                assert_eq!(iec.state(), state, "{state:?} must not move");
                assert_eq!(commands, Vec::new(), "{state:?} is owed nothing");
            }
        }

        /// The exit: X1 and back to waiting, with the record still open. That
        /// last part is the whole reason `ac_with_soc` can use this at all.
        #[test]
        fn the_hold_ending_restarts_the_session_without_closing_its_record() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));
            iec.handle(IecInput::ReinitStarted);

            let commands = iec.handle(IecInput::ReinitFinished);

            assert_eq!(iec.state(), AcState::WaitingForAuthentication);
            assert_eq!(
                commands,
                vec![IecCommand::AllowPowerOn(false), IecCommand::CpStateX1]
            );
            assert!(iec.transaction_active, "the record stays open");
            assert!(iec.authorized, "the authorization survives");
        }

        /// A configured duration of zero holds nothing.
        /// `Charger` leaves `reinit_timer_active` false and its exit condition
        /// is satisfied on the same pass.
        #[test]
        fn a_zero_duration_runs_the_whole_sequence_in_one_call() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::ZERO);

            let commands = iec.handle(IecInput::ReinitStarted);

            assert_eq!(iec.state(), AcState::WaitingForAuthentication);
            assert!(
                !commands
                    .iter()
                    .any(|c| matches!(c, IecCommand::ArmTimer(AcTimer::Reinit(_)))),
                "nothing to wait for, got {commands:?}"
            );
            assert_eq!(
                commands.last(),
                Some(&IecCommand::CpStateX1),
                "the pilot ends on X1, got {commands:?}"
            );
        }

        #[test]
        fn the_hold_ending_outside_the_reinit_moves_nothing() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));

            let commands = iec.handle(IecInput::ReinitFinished);

            assert_eq!(iec.state(), AcState::Charging);
            assert_eq!(commands, Vec::new());
        }

        /// The pilot is at F, E or X1 for the whole hold, so no route may put an
        /// offer back on it.
        #[test]
        fn the_reinit_carries_no_offer() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));
            iec.handle(IecInput::ReinitStarted);

            assert!(!iec.pwm_eligible());
            assert_eq!(iec.signalled_current_a(), 0.0);
            assert_eq!(
                iec.set_current_limit_a(32.0),
                vec![IecCommand::SetOvercurrentLimitA(32.0)],
                "a limit change must not re-offer during the hold"
            );
        }

        /// An unplug during the hold lands the port at rest rather than leaving
        /// a departed vehicle's session waiting for the deadline. `Charger`
        /// reaches the same place one `Reinit` deadline later, because its
        /// `Reinit` case reads no unplug; the port gets there at once because
        /// the state it lands in runs its own exit conditions.
        #[test]
        fn an_unplug_during_the_hold_ends_the_session() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));
            iec.handle(IecInput::ReinitStarted);

            iec.handle(IecInput::CarUnplugged);

            assert_eq!(iec.state(), AcState::Idle);
            assert!(!iec.transaction_active);
        }

        /// A port taken out of service during the hold does not come back
        /// advertising availability.
        #[test]
        fn a_disable_during_the_hold_leaves_the_port_out_of_service() {
            let mut iec = charging_with(ReinitMethod::CpStateF, Duration::from_millis(3000));
            iec.handle(IecInput::ReinitStarted);

            iec.handle(IecInput::Disable);
            iec.handle(IecInput::CarUnplugged);

            assert_eq!(iec.state(), AcState::Disabled);
        }
    }

    /// `Charger::run_state_machine`'s `WaitingForAuthentication` external authorization arm guards only the record opening on
    /// `not flag_transaction_active` and then assigns the state anyway. The
    /// port refused the whole input, which left every route that re-enters
    /// `WaitingForAuthentication` mid transaction stuck in it.
    #[test]
    fn a_transaction_start_with_a_record_already_open_still_moves_the_state() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        assert!(iec.transaction_active);

        // The data link error route: back to waiting with the record open.
        iec.handle(IecInput::SessionRestart);
        assert_eq!(iec.state(), AcState::WaitingForAuthentication);

        let commands = iec.handle(IecInput::TransactionStarted);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert_eq!(
            commands,
            vec![IecCommand::PwmOn(pwm_duty_for_current_a(16.0))],
            "the session comes back on the nominal offer"
        );
    }

    /// `Charger.cpp:1532-1539`: the relays are not touched on the way in. The
    /// offer comes off the pilot so the vehicle stops drawing, and the deadline
    /// carries the configured delay.
    #[test]
    fn a_phase_change_from_charging_drops_the_offer_before_it_moves_the_relays() {
        let mut iec = charging_iec();

        let commands = iec.handle(IecInput::SwitchPhasesRequested(false));

        assert_eq!(iec.state(), AcState::SwitchPhases);
        assert_eq!(
            commands,
            vec![
                IecCommand::CpStateX1,
                IecCommand::ArmTimer(AcTimer::SwitchPhases(Duration::from_secs(10))),
            ]
        );
        assert_eq!(iec.signalled_current_a(), 0.0);
    }

    /// `switch_3ph1ph_cp_state`, the one choice an operator has over the break.
    #[test]
    fn the_break_signals_the_configured_pilot_state() {
        let mut iec = iec_with_break_in_state_f();
        let commands = iec.handle(IecInput::SwitchPhasesRequested(true));
        assert!(
            commands.contains(&IecCommand::CpStateF),
            "the F configuration signalled something else: {commands:?}"
        );
        assert!(!commands.contains(&IecCommand::CpStateX1), "{commands:?}");
    }

    /// `Charger.cpp:597-603`: the board first, then the return state, then the
    /// offer that state re-derives. An offer emitted before the board call
    /// would invite the vehicle to draw across the switch.
    #[test]
    fn the_break_expiring_moves_the_relays_before_it_restores_the_offer() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));

        let commands = iec.handle(IecInput::SwitchPhasesDelayExpired);

        // `charging_iec` leaves the contactor closed, so this vehicle is one
        // that held state C and the shortcut at the end of the break grants it
        // power again. The order is the whole point: relays, then offer, then
        // permission.
        assert_eq!(iec.state(), AcState::Charging);
        assert_eq!(
            commands,
            vec![
                IecCommand::SwitchThreePhases(false),
                IecCommand::PwmOn(pwm_duty_for_current_a(16.0)),
                IecCommand::AllowPowerOn(true),
            ]
        );
        assert_eq!(iec.signalled_current_a(), 16.0);
    }

    /// `Charger.cpp:1540-1541`: a second request replaces the pending value
    /// and does not restart the break, so a stream of them cannot hold the
    /// pilot down.
    #[test]
    fn a_second_request_during_a_break_replaces_the_pending_value() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));

        let commands = iec.handle(IecInput::SwitchPhasesRequested(true));
        assert!(commands.is_empty(), "the break restarted: {commands:?}");
        assert_eq!(iec.state(), AcState::SwitchPhases);

        let commands = iec.handle(IecInput::SwitchPhasesDelayExpired);
        assert!(
            commands.contains(&IecCommand::SwitchThreePhases(true)),
            "the board got the superseded value: {commands:?}"
        );
    }

    /// `Charger.cpp:191-198`. The pending relay switch happens on every route
    /// out of the break, including the ones that abandon it without waiting.
    /// Leaving the relays on the count the session started with while the
    /// energy manager has already been told otherwise is the failure this
    /// prevents.
    #[test]
    fn every_route_out_of_the_break_still_moves_the_relays() {
        for (label, abandon) in [
            ("unplug", IecInput::CarUnplugged),
            ("disable", IecInput::Disable),
            ("error shutdown", IecInput::ErrorShutdown),
            ("state E", IecInput::CpStateE),
        ] {
            let mut iec = charging_iec();
            iec.handle(IecInput::SwitchPhasesRequested(true));

            let commands = iec.handle(abandon);

            assert_ne!(iec.state(), AcState::SwitchPhases, "{label}");
            assert!(
                commands.contains(&IecCommand::SwitchThreePhases(true)),
                "{label} left the relays behind: {commands:?}"
            );
        }
    }

    /// `Charger.cpp:591-596` then the `PrepareCharging` checks: an unplug
    /// during the break hands the session back to the return state, whose own
    /// checks tear it down through the stop route rather than dropping it at
    /// rest.
    #[test]
    fn an_unplug_during_the_break_stops_the_session_through_the_return_state() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));

        iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::PrepareCharging, AcState::StoppingCharging],
            "the stopping announcement or the return state was skipped"
        );
    }

    /// The board is told once. A second delivery of the deadline, or one that
    /// arrives after the break already ended, moves nothing.
    #[test]
    fn the_board_is_told_once_per_break() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(true));
        iec.handle(IecInput::SwitchPhasesDelayExpired);

        let again = iec.handle(IecInput::SwitchPhasesDelayExpired);
        assert!(again.is_empty(), "{again:?}");
    }

    /// A request from a state the enforced limits handler routes directly is a
    /// routing mistake, and moving the relays anyway would do it from a state
    /// that never decided to.
    #[test]
    fn a_break_request_from_a_state_that_takes_the_direct_call_moves_nothing() {
        for state in [
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::Disabled,
        ] {
            let mut iec = charging_iec();
            iec.state = state;
            let commands = iec.handle(IecInput::SwitchPhasesRequested(true));
            assert!(commands.is_empty(), "{state:?} gave {commands:?}");
            assert_eq!(iec.state(), state, "{state:?} moved");
        }
    }

    /// The break carries no PWM offer, which the single `pwm_eligible`
    /// predicate has to agree with: the pilot is at X1 or F for its whole
    /// length, so a limit change arriving mid break must not re-derive a duty
    /// cycle.
    #[test]
    fn the_break_is_not_pwm_eligible_and_a_limit_change_offers_nothing() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));
        assert!(!iec.pwm_eligible());

        let commands = iec.set_current_limit_a(20.0);
        assert_eq!(commands, vec![IecCommand::SetOvercurrentLimitA(20.0)]);

        // And the offer the break's exit restores is the new limit.
        let commands = iec.handle(IecInput::SwitchPhasesDelayExpired);
        assert!(
            commands.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(20.0))),
            "{commands:?}"
        );
    }

    /// `switch_3ph1ph_delay_s` has no manifest minimum, so zero is a legal
    /// operator setting and means no break at all: the pilot still drops and
    /// the relays still move, but the deadline is due immediately.
    ///
    /// Driven because a zero duration timer is the case an arming site is most
    /// likely to get wrong, and because the whole feature collapses to the
    /// direct board call here without ever taking the direct route.
    #[test]
    fn a_zero_delay_still_takes_the_break_and_moves_the_relays_on_the_first_deadline() {
        let mut iec = Iec::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::ZERO,
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        });
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        iec.handle(IecInput::CarRequestedPower);

        let entered = iec.handle(IecInput::SwitchPhasesRequested(true));
        assert_eq!(iec.state(), AcState::SwitchPhases);
        assert!(
            entered.contains(&IecCommand::ArmTimer(AcTimer::SwitchPhases(Duration::ZERO))),
            "{entered:?}"
        );
        // Still not before the deadline, even at zero: the relays move on the
        // event and never from the arming site.
        assert!(
            !entered.contains(&IecCommand::SwitchThreePhases(true)),
            "{entered:?}"
        );

        let done = iec.handle(IecInput::SwitchPhasesDelayExpired);
        assert!(
            done.contains(&IecCommand::SwitchThreePhases(true)),
            "{done:?}"
        );
        assert_eq!(iec.state(), AcState::PrepareCharging);
    }

    /// A paused vehicle still holds an open transaction, and the break does not
    /// apply to that state.
    ///
    /// `a_break_request_from_a_state_that_takes_the_direct_call_moves_nothing`
    /// forces the state field to reach the same states; this one drives to one
    /// of them properly, so the transaction flag is whatever the real route
    /// leaves rather than whatever the fixture had. `Charger.cpp:1542` reads no
    /// transaction flag, so an open transaction is not what decides the route.
    #[test]
    fn a_paused_vehicle_holds_its_transaction_and_the_break_does_not_apply() {
        let mut iec = charging_iec();
        iec.handle(IecInput::CarRequestedStopPower);
        assert_eq!(iec.state(), AcState::ChargingPausedEv);
        assert!(iec.transaction_active, "the transaction closed");

        let commands = iec.handle(IecInput::SwitchPhasesRequested(true));
        assert!(commands.is_empty(), "{commands:?}");
        assert_eq!(iec.state(), AcState::ChargingPausedEv);
    }

    /// The compliant sequence through a break, which is the one that recovers.
    ///
    /// The vehicle answers the withdrawn offer by opening S2, the board reports
    /// the contactor open, and neither fact cuts the break short: the C++
    /// `SwitchPhases` case reads no control pilot event at all, so the break
    /// runs its configured length whatever the vehicle does.
    #[test]
    fn a_vehicle_that_answers_the_break_does_not_cut_it_short_and_resumes_after_it() {
        let mut iec = charging_iec();
        assert!(!iec.contactor_open());
        iec.handle(IecInput::SwitchPhasesRequested(false));

        // The vehicle opens S2 because the offer went away.
        let paused = iec.handle(IecInput::CarRequestedStopPower);
        assert_eq!(
            iec.state(),
            AcState::SwitchPhases,
            "the break was cut short"
        );
        assert!(
            !paused.contains(&IecCommand::SwitchThreePhases(false)),
            "{paused:?}"
        );

        // The board reports the relays open.
        iec.handle(IecInput::PowerOff);
        assert!(iec.contactor_open());
        assert_eq!(
            iec.state(),
            AcState::SwitchPhases,
            "the break was cut short"
        );

        // Only the deadline ends it.
        let done = iec.handle(IecInput::SwitchPhasesDelayExpired);
        assert!(
            done.contains(&IecCommand::SwitchThreePhases(false)),
            "{done:?}"
        );
        assert_eq!(iec.state(), AcState::PrepareCharging);

        // The vehicle sees the duty cycle again and closes S2.
        let resumed = iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);
        assert!(
            resumed.contains(&IecCommand::AllowPowerOn(true)),
            "{resumed:?}"
        );
    }

    /// A vehicle that ignores the withdrawn offer and stays in control pilot
    /// state C across the whole break resumes charging without waiting for an
    /// edge that will not come.
    ///
    /// It produces no fresh state C, and `Iec::grant_power` is the only route
    /// into `Charging`, so without the shortcut this test now pins the port
    /// settled in `PrepareCharging` with the offer restored, the contactor
    /// still closed and the vehicle still drawing: a session physically
    /// charging under a state in which soft overcurrent detection does not run
    /// and no `ChargingStarted` was ever announced. `Charger.cpp:725-728`
    /// re-enters `Charging` on its next pass by reading
    /// `iec_allow_close_contactor`, which such a vehicle still has set.
    ///
    /// Inverted rather than replaced: it pinned the strand, and now it pins
    /// the recovery.
    #[test]
    fn a_vehicle_that_never_leaves_state_c_resumes_charging_after_the_break() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));

        // No CarRequestedStopPower and no PowerOff: the vehicle ignored the
        // pilot entirely, so the contactor never opened.
        let done = iec.handle(IecInput::SwitchPhasesDelayExpired);

        assert!(
            done.contains(&IecCommand::SwitchThreePhases(false)),
            "{done:?}"
        );
        assert!(!iec.contactor_open(), "the contactor is still closed");
        assert_eq!(
            iec.state(),
            AcState::Charging,
            "the session stranded rather than resuming"
        );
        assert!(
            done.contains(&IecCommand::AllowPowerOn(true)),
            "power was not granted again: {done:?}"
        );

        // The relays move before the session charges again, so neither the
        // offer nor the permission precedes the switch.
        let switch_at = done
            .iter()
            .position(|c| matches!(c, IecCommand::SwitchThreePhases(_)))
            .expect("the relays moved");
        let grant_at = done
            .iter()
            .position(|c| matches!(c, IecCommand::AllowPowerOn(true)))
            .expect("power granted");
        assert!(switch_at < grant_at, "power was granted first: {done:?}");
    }

    /// The other side of the same shortcut: a vehicle that DID answer the break
    /// waits for its own state C, so power is never granted to one sitting in
    /// state B.
    #[test]
    fn a_vehicle_that_opened_s2_during_the_break_waits_for_its_own_state_c() {
        let mut iec = charging_iec();
        iec.handle(IecInput::SwitchPhasesRequested(false));
        iec.handle(IecInput::CarRequestedStopPower);
        iec.handle(IecInput::PowerOff);
        assert!(iec.contactor_open());

        let done = iec.handle(IecInput::SwitchPhasesDelayExpired);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert!(
            !done.contains(&IecCommand::AllowPowerOn(true)),
            "power was granted to a vehicle in state B: {done:?}"
        );

        // Its own state C is what resumes it.
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);
    }

    /// `Charger::get_max_current_signalled_to_ev_internal` (`Charger.cpp:1929`)
    /// for basic charging: what is on the pilot, and zero when nothing is.
    #[test]
    fn the_signalled_current_is_the_offer_and_not_the_stored_limit() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::TransactionStarted);
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);

        assert_eq!(iec.signalled_current_a(), 16.0);
        assert_eq!(iec.current_limit_a(), 16.0);

        // The offer follows the limit while it stands, because this port has no
        // five second update deferral to lag behind it.
        iec.set_current_limit_a(20.0);
        assert_eq!(iec.signalled_current_a(), 20.0);

        // `Charger::cp_state_X1` (`:1262`) and `cp_state_F` (`:1271`) both zero
        // it, and the stored limit is untouched by either.
        iec.handle(IecInput::CpStateF);
        assert_eq!(iec.signalled_current_a(), 0.0);
        assert_eq!(iec.current_limit_a(), 20.0);
    }

    /// A stopped offer signals nothing even from a state that could carry one,
    /// which is the `pwm_running` half of the answer rather than the state half.
    #[test]
    fn a_state_that_could_offer_but_is_not_offering_signals_nothing() {
        let mut iec = iec();
        iec.state = AcState::Charging;
        iec.pwm_running = false;
        assert_eq!(iec.signalled_current_a(), 0.0);

        iec.pwm_running = true;
        assert_eq!(iec.signalled_current_a(), 16.0);
    }

    #[test]
    fn limit_change_does_not_start_a_stopped_pwm_offer() {
        let mut iec = iec();
        iec.state = AcState::Charging;
        iec.pwm_running = false;

        let commands = iec.set_current_limit_a(20.0);

        assert_eq!(commands, vec![IecCommand::SetOvercurrentLimitA(20.0)]);
    }

    #[test]
    fn transaction_start_requires_authorization_and_a_plugged_vehicle() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);

        assert!(
            iec.handle(IecInput::TransactionStarted).is_empty(),
            "unauthorized transaction must not offer power"
        );

        iec.handle(IecInput::AuthorizationAccepted);
        let commands = iec.handle(IecInput::TransactionStarted);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert_eq!(
            commands,
            vec![IecCommand::PwmOn(pwm_duty_for_current_a(16.0))]
        );
    }

    #[test]
    fn shutdown_removes_energy_before_signalling_state_f() {
        let mut iec = iec();
        let commands = iec.handle(IecInput::EmergencyShutdown);

        let allow_off = commands
            .iter()
            .position(|c| *c == IecCommand::AllowPowerOn(false))
            .unwrap();
        let state_f = commands
            .iter()
            .position(|c| *c == IecCommand::CpStateF)
            .unwrap();

        assert!(allow_off < state_f);
    }

    #[test]
    fn unplug_releases_the_connector() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert!(commands.contains(&IecCommand::UnlockConnector));
        assert_eq!(iec.state(), AcState::Idle);
    }

    #[test]
    fn a_vehicle_requesting_power_is_granted_it_from_prepare_charging() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::CarRequestedPower);

        assert_eq!(iec.state(), AcState::Charging);
        assert!(commands.contains(&IecCommand::LockConnector));
        assert!(commands.contains(&IecCommand::AllowPowerOn(true)));
    }

    #[test]
    fn a_vehicle_requesting_power_without_a_pwm_offer_is_refused() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);

        let commands = iec.handle(IecInput::CarRequestedPower);

        assert_eq!(iec.state(), AcState::WaitingForAuthentication);
        assert!(!commands.contains(&IecCommand::AllowPowerOn(true)));
    }

    #[test]
    fn a_ventilated_power_request_is_granted_when_the_port_is_ventilated() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::CarRequestedVentilatedPower);

        assert_eq!(iec.state(), AcState::Charging);
        assert!(commands.contains(&IecCommand::AllowPowerOn(true)));
    }

    #[test]
    fn a_ventilated_power_request_withholds_power_without_ventilation() {
        let mut iec = iec_without_ventilation();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::CarRequestedVentilatedPower);

        assert_ne!(iec.state(), AcState::Charging);
        assert!(commands.contains(&IecCommand::AllowPowerOn(false)));
        assert!(!commands.contains(&IecCommand::AllowPowerOn(true)));
    }

    #[test]
    fn a_vehicle_opening_s2_pauses_the_session_and_keeps_the_pwm_offer() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::CarRequestedStopPower);

        assert_eq!(iec.state(), AcState::ChargingPausedEv);
        assert!(commands.contains(&IecCommand::AllowPowerOn(false)));
        assert!(!commands.contains(&IecCommand::PwmOff));
        assert!(iec.pwm_eligible());
    }

    #[test]
    fn a_vehicle_opening_s2_releases_the_connector_when_state_b_is_not_locked() {
        let mut iec = Iec::new(IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: false,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: ReinitMethod::CpStateF,
            reinit_duration: Duration::from_millis(3000),
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        });
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::CarRequestedStopPower);

        assert!(commands.contains(&IecCommand::UnlockConnector));
        assert!(!commands.contains(&IecCommand::LockConnector));
    }

    #[test]
    fn an_evse_initiated_pause_withdraws_the_pwm_offer() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::PauseRequested);

        assert_eq!(iec.state(), AcState::ChargingPausedEvse);
        assert!(commands.contains(&IecCommand::AllowPowerOn(false)));
        assert!(commands.contains(&IecCommand::PwmOff));
        assert!(commands.contains(&IecCommand::CpStateX1));
        assert!(!iec.pwm_eligible());
    }

    #[test]
    fn a_resume_after_an_evse_pause_re_offers_the_nominal_duty_cycle() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PauseRequested);

        let commands = iec.handle(IecInput::ResumeRequested);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert!(commands.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(16.0))));
    }

    /// A resume reads the vehicle's standing request and not the relays.
    ///
    /// The relays were the proxy for that request here, and a proxy read wrong
    /// in both directions. This drives the direction that mattered most: the
    /// vehicle opened S2, so its request is withdrawn, but the board has not
    /// yet reported the relays open - a pause arriving inside its own reporting
    /// latency. The proxy allowed power to a vehicle that had just asked for
    /// none; `Charger.cpp:763-767` reads the latch, which is clear.
    #[test]
    fn a_resume_does_not_allow_power_to_a_vehicle_that_withdrew_its_request() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::CarRequestedStopPower);
        iec.handle(IecInput::PauseRequested);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);
        assert!(!iec.contactor_open(), "the relays are still reported closed");

        let commands = iec.handle(IecInput::ResumeRequested);

        assert!(
            !commands.contains(&IecCommand::AllowPowerOn(true)),
            "{commands:?}"
        );
        assert!(
            commands.contains(&IecCommand::PwmOn(pwm_duty_for_current_a(16.0))),
            "the offer is restored either way: {commands:?}"
        );
    }

    /// And the direction the finding named: a vehicle that never left state C
    /// keeps its request, so the resume allows power again with no fresh pilot
    /// edge to wait for - which is the whole point of the latch, because no
    /// such edge is coming. The relays here are reported **open**, which is
    /// what the proxy refused on.
    #[test]
    fn a_resume_allows_power_again_to_a_vehicle_still_asking_for_it() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::PauseRequested);
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);
        assert!(iec.contactor_open(), "the relays opened behind the pause");

        let commands = iec.handle(IecInput::ResumeRequested);

        assert!(
            commands.contains(&IecCommand::AllowPowerOn(true)),
            "the vehicle is still asking: {commands:?}"
        );
        assert!(
            commands.contains(&IecCommand::CancelTimer(AcTimer::C1)),
            "and the deadline for removing power under load is done: {commands:?}"
        );
    }

    #[test]
    fn a_resume_is_ignored_unless_the_evse_paused_the_session() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        assert!(iec.handle(IecInput::ResumeRequested).is_empty());
        assert_eq!(iec.state(), AcState::PrepareCharging);
    }

    #[test]
    fn a_stop_request_withdraws_energy_and_the_pwm_offer() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::StopRequested);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert_eq!(
            commands,
            vec![
                IecCommand::AllowPowerOn(false),
                IecCommand::PwmOff,
                IecCommand::CpStateX1,
                IecCommand::ArmTimer(AcTimer::C1),
                IecCommand::ArmTimer(AcTimer::StoppingCharging),
            ]
        );
    }

    #[test]
    fn a_disable_removes_energy_then_signals_state_f_then_disables_the_port() {
        // `Charger.cpp:201-205` signals state F, not X1: X1 says available with
        // no offer, F says unavailable, and the difference is what the vehicle
        // reads off the pilot.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::Disable);

        assert_eq!(iec.state(), AcState::Disabled);
        assert_eq!(
            commands,
            vec![
                IecCommand::AllowPowerOn(false),
                IecCommand::PwmOff,
                IecCommand::CpStateF,
                IecCommand::CancelTimer(AcTimer::C1),
                IecCommand::CancelTimer(AcTimer::CpStateFUnlock),
                IecCommand::CancelTimer(AcTimer::StoppingCharging),
                IecCommand::Enable(false),
            ]
        );
    }

    #[test]
    fn a_disable_holds_the_vehicle_and_only_the_unplug_releases_it() {
        // `grep -c unlock Charger.cpp` is 0: no availability change in the C++
        // releases the connector. Popping the latch on a disable would hand the
        // cable back mid session, which is the same rule the finished state
        // keeps.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let disabled = iec.handle(IecInput::Disable);
        assert!(
            !disabled.contains(&IecCommand::UnlockConnector),
            "a disable must not release the vehicle, got {disabled:?}"
        );

        let unplugged = iec.handle(IecInput::CarUnplugged);
        assert!(
            unplugged.contains(&IecCommand::UnlockConnector),
            "the unplug is the only path that releases the vehicle, got {unplugged:?}"
        );
    }

    #[test]
    fn a_disable_drops_the_authorization_so_a_re_enabled_port_starts_no_transaction() {
        // The permission belonged to the session the disable ended. Carrying it
        // across would start the next vehicle's transaction on a token that was
        // never presented for it.
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        iec.handle(IecInput::Disable);

        iec.handle(IecInput::Enable);
        iec.handle(IecInput::CarPluggedIn);
        let commands = iec.handle(IecInput::TransactionStarted);

        assert!(commands.is_empty(), "got {commands:?}");
        assert_eq!(iec.state(), AcState::WaitingForAuthentication);
    }

    #[test]
    fn a_disable_ends_the_transaction_so_a_re_enabled_port_can_start_a_new_one() {
        // The converse of the above. A transaction left standing blocks the
        // next one, because a transaction already under way is one of the guards
        // the start checks.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::Disable);

        iec.handle(IecInput::Enable);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::AuthorizationAccepted);
        let commands = iec.handle(IecInput::TransactionStarted);

        assert_eq!(iec.state(), AcState::PrepareCharging);
        assert_eq!(
            commands,
            vec![IecCommand::PwmOn(pwm_duty_for_current_a(16.0))]
        );
    }

    #[test]
    fn an_enable_returns_a_disabled_port_to_idle() {
        // `Charger.cpp:1730` starts the board and the Idle entry it hands to
        // signals X1 at `:230`. Starting the board alone would leave the pilot
        // on the state F the disable set, so the port never invites a vehicle
        // again.
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::Disable);

        let commands = iec.handle(IecInput::Enable);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            commands,
            vec![IecCommand::Enable(true), IecCommand::CpStateX1]
        );
    }

    #[test]
    fn an_enable_on_a_port_that_is_already_enabled_changes_nothing() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        assert!(iec.handle(IecInput::Enable).is_empty());
        assert_eq!(iec.state(), AcState::PrepareCharging);
    }

    #[test]
    fn control_pilot_state_e_removes_energy_and_releases_the_connector() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::CpStateE);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert_eq!(
            commands,
            vec![
                IecCommand::AllowPowerOn(false),
                IecCommand::PwmOff,
                IecCommand::CpStateX1,
                IecCommand::UnlockConnector,
                IecCommand::CancelTimer(AcTimer::C1),
                IecCommand::ArmTimer(AcTimer::StoppingCharging),
            ]
        );
    }

    #[test]
    fn the_state_f_unlock_timer_releases_the_connector() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CpStateF);

        let commands = iec.handle(IecInput::CpStateFUnlockTimerExpired);

        assert_eq!(commands, vec![IecCommand::UnlockConnector]);
    }

    #[test]
    fn the_c1_timeout_forces_power_off_under_load_without_dropping_the_offer() {
        // The vehicle kept drawing power after the duty cycle went away. Power
        // off under load is the last resort the standard allows.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::CpStateC1TimeoutExpired);

        assert_eq!(commands, vec![IecCommand::AllowPowerOn(false)]);
        assert!(iec.pwm_eligible());
    }

    #[test]
    fn the_c1_timeout_is_ignored_when_no_session_is_live() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);

        assert!(iec.handle(IecInput::CpStateC1TimeoutExpired).is_empty());
    }

    #[test]
    fn the_stopping_charging_timeout_removes_energy_under_load() {
        // `Charger.cpp:1027-1032`: the timeout performs a hard stop. It does not
        // end the session, which happens once the contactor is observed open.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::StopRequested);

        let commands = iec.handle(IecInput::StoppingChargingTimeoutExpired);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert_eq!(commands, vec![IecCommand::AllowPowerOn(false)]);
    }

    #[test]
    fn an_open_contactor_while_stopping_finishes_the_session() {
        // `Charger.cpp:1035-1046`: the stop completes only once the relays are
        // observed open, and it completes into `Finished` rather than `Idle`.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::StopRequested);

        let commands = iec.handle(IecInput::PowerOff);

        assert_eq!(iec.state(), AcState::Finished);
        assert!(commands.contains(&IecCommand::CancelTimer(AcTimer::StoppingCharging)));
        assert!(
            !commands.contains(&IecCommand::UnlockConnector),
            "a finished session keeps the vehicle held until it is unplugged"
        );
    }

    /// `Charger.cpp:782-792` routes `Charging` to `StoppingCharging` on the
    /// plug flag, `Charger.cpp:1041-1044` carries an open contactor on to
    /// `Finished`, and `Charger.cpp:1080-1083` lands at rest. The reducer owes
    /// the same three steps, because each one carries entry work of its own.
    #[test]
    fn an_unplug_while_charging_routes_through_stopping_and_finished() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);

        iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::StoppingCharging, AcState::Finished, AcState::Idle]
        );
    }

    /// `Charger.cpp:1035-1038` holds in `StoppingCharging` while the relays are
    /// closed. The unplug does not get to skip that wait, because a port that
    /// has not seen its relays open cannot claim the session ended cleanly.
    #[test]
    fn an_unplug_while_charging_waits_for_the_relays_before_finishing() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert!(
            commands.contains(&IecCommand::ArmTimer(AcTimer::StoppingCharging)),
            "the wait on the relays is bounded, got {commands:?}"
        );

        let completed = iec.handle(IecInput::PowerOff);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::Finished, AcState::Idle]
        );
        assert!(
            completed.contains(&IecCommand::CancelTimer(AcTimer::StoppingCharging)),
            "got {completed:?}"
        );
    }

    /// `Charger.cpp:316-319`: a vehicle that leaves before its transaction
    /// started has no stop to run, so the route reaches `Finished` directly and
    /// goes on to rest without ever announcing a stop or arming its deadline.
    #[test]
    fn an_unplug_before_the_transaction_started_finishes_without_stopping() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        assert_eq!(iec.state(), AcState::WaitingForAuthentication);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::Finished, AcState::Idle]
        );
        assert!(
            !commands.contains(&IecCommand::ArmTimer(AcTimer::StoppingCharging)),
            "there was no charge to stop, got {commands:?}"
        );
    }

    /// `Charger.cpp:1041-1042` and `:1080-1083`: a disable outstanding when the
    /// relays open carries the finished session out of service, rather than
    /// leaving it waiting for an unplug that a port already out of service has
    /// no reason to expect. The vehicle is still held, because the unplug is
    /// the only path that releases it.
    #[test]
    fn a_disable_outstanding_when_the_relays_open_takes_the_port_out_of_service() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::StopRequested);
        iec.disable_requested = true;

        let commands = iec.handle(IecInput::PowerOff);

        assert_eq!(iec.state(), AcState::Disabled);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::Finished, AcState::Disabled]
        );
        assert!(
            !commands.contains(&IecCommand::UnlockConnector),
            "the vehicle is still plugged in, got {commands:?}"
        );
    }

    /// `Charger.cpp:942-946`: a pause held by the EVSE is still a live session,
    /// so the vehicle leaving it owes the same stop as one leaving a charge.
    #[test]
    fn an_unplug_while_paused_by_the_evse_routes_through_stopping() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PauseRequested);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);

        iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::StoppingCharging, AcState::Finished, AcState::Idle]
        );
    }

    /// `Charger.cpp:880-884`: so does a pause the vehicle took itself.
    #[test]
    fn an_unplug_while_paused_by_the_vehicle_routes_through_stopping() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::CarRequestedStopPower);
        assert_eq!(iec.state(), AcState::ChargingPausedEv);

        iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            iec.take_transitions(),
            vec![AcState::StoppingCharging, AcState::Finished, AcState::Idle]
        );
    }

    /// An idle port has no session to stop, so the unplug arms no deadline and
    /// crosses no state on the way to a state it is already in.
    #[test]
    fn an_unplug_on_an_idle_port_stops_nothing() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        assert_eq!(iec.state(), AcState::Idle);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert!(
            !commands.contains(&IecCommand::ArmTimer(AcTimer::StoppingCharging)),
            "an idle port has no stop to bound, got {commands:?}"
        );
        assert_eq!(iec.take_transitions(), vec![AcState::Idle]);
    }

    /// An unplug arriving while the port is already stopping changes nothing
    /// about the relays, so it does not advance the route either.
    #[test]
    fn an_unplug_while_already_stopping_still_waits_for_the_relays() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::StopRequested);
        assert_eq!(iec.state(), AcState::StoppingCharging);

        iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::StoppingCharging);
    }

    /// `Charger.cpp:1042` and `:1083`: an outstanding disable request is read
    /// at the end of the route, not at the start of it, so the session still
    /// announces its stop on the way out of service.
    #[test]
    fn an_unplug_with_a_disable_outstanding_still_routes_through_stopping() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.disable_requested = true;

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Disabled);
        assert_eq!(
            iec.take_transitions(),
            vec![
                AcState::StoppingCharging,
                AcState::Finished,
                AcState::Disabled
            ]
        );
        assert_eq!(
            commands.last(),
            Some(&IecCommand::Enable(false)),
            "got {commands:?}"
        );
    }

    #[test]
    fn a_finished_session_returns_to_idle_only_on_unplug() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::StopRequested);
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::Finished);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert!(commands.contains(&IecCommand::UnlockConnector));
    }

    #[test]
    fn withdrawing_the_offer_while_the_vehicle_draws_power_arms_the_c1_timer() {
        // `IECStateMachine.cpp:241-245`: X2 to C1, the vehicle has a bounded
        // time to stop drawing before power is removed under load.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::StopRequested);

        assert!(commands.contains(&IecCommand::ArmTimer(AcTimer::C1)));
    }

    #[test]
    fn withdrawing_the_offer_before_the_vehicle_drew_power_arms_no_c1_timer() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::StopRequested);

        assert!(!commands.contains(&IecCommand::ArmTimer(AcTimer::C1)));
    }

    #[test]
    fn the_vehicle_returning_to_state_b_cancels_the_c1_timer() {
        // `IECStateMachine.cpp:160`.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PauseRequested);

        let commands = iec.handle(IecInput::CarRequestedStopPower);

        assert!(commands.contains(&IecCommand::CancelTimer(AcTimer::C1)));
    }

    /// The request belongs to the vehicle that made it. Carried across an
    /// unplug, it would allow power to a vehicle that has never asked for any:
    /// the next session is paused before its vehicle closes S2 and the resume
    /// then reads the previous vehicle's request.
    ///
    /// `Charger.cpp:225` clears the latch on the `Idle` entry for that reason.
    #[test]
    fn the_next_vehicle_does_not_inherit_the_last_one_s_request() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        assert_eq!(iec.state(), AcState::Charging, "the control");

        iec.handle(IecInput::CarUnplugged);
        iec.handle(IecInput::PowerOff);
        assert_eq!(iec.state(), AcState::Idle);

        // A second vehicle, paused before it asks for power.
        prepare_charging(&mut iec);
        iec.handle(IecInput::PauseRequested);
        assert_eq!(iec.state(), AcState::ChargingPausedEvse);

        let commands = iec.handle(IecInput::ResumeRequested);

        assert!(
            !commands.contains(&IecCommand::AllowPowerOn(true)),
            "this vehicle has asked for nothing: {commands:?}"
        );
    }

    /// The deadline for removing power under load travels **with** the
    /// re-energize rather than with the resume, so a resume that grants no
    /// power cancels nothing either. `IECStateMachine.cpp:247-256`.
    ///
    /// The other half of the pair, a resume that does grant power cancelling
    /// it, is asserted by
    /// `a_resume_allows_power_again_to_a_vehicle_still_asking_for_it`; this is
    /// the case that would go unnoticed if the cancel were moved out of the
    /// grant, because the vehicle here has already had it cancelled by its own
    /// withdrawal and a second cancel looks harmless.
    #[test]
    fn a_resume_that_grants_no_power_cancels_no_deadline() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::PowerOn);
        iec.handle(IecInput::CarRequestedStopPower);
        iec.handle(IecInput::PauseRequested);

        let commands = iec.handle(IecInput::ResumeRequested);

        assert!(
            !commands.contains(&IecCommand::CancelTimer(AcTimer::C1)),
            "{commands:?}"
        );
    }

    #[test]
    fn signalling_state_f_arms_the_unlock_timer() {
        // `IECStateMachine.cpp:304-308`.
        let mut iec = iec();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::CpStateF);

        assert!(commands.contains(&IecCommand::ArmTimer(AcTimer::CpStateFUnlock)));
    }

    #[test]
    fn unplugging_cancels_every_pilot_timer() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        iec.handle(IecInput::StopRequested);

        let commands = iec.handle(IecInput::CarUnplugged);

        for timer in [
            AcTimer::C1,
            AcTimer::CpStateFUnlock,
            AcTimer::StoppingCharging,
        ] {
            assert!(
                commands.contains(&IecCommand::CancelTimer(timer)),
                "{timer:?} must be cancelled on unplug"
            );
        }
    }

    #[test]
    fn a_disable_cancels_every_pilot_timer() {
        // `IECStateMachine.cpp:148-151` stops the unlock timer on the disabled
        // pilot state, and `:200` and `:222` stop the C1 timer.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        let commands = iec.handle(IecInput::Disable);

        for timer in [
            AcTimer::C1,
            AcTimer::CpStateFUnlock,
            AcTimer::StoppingCharging,
        ] {
            assert!(
                commands.contains(&IecCommand::CancelTimer(timer)),
                "{timer:?} must be cancelled on disable"
            );
        }
    }

    #[test]
    fn the_stopping_charging_timeout_is_ignored_while_charging() {
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);

        assert!(iec
            .handle(IecInput::StoppingChargingTimeoutExpired)
            .is_empty());
        assert_eq!(iec.state(), AcState::Charging);
    }

    #[test]
    fn a_session_restart_returns_to_waiting_for_authentication_and_de_energizes() {
        // `Charger.cpp:2101`, the return state `Charger::dlink_error` builds.
        // The authorization and the transaction both stay, which is what makes
        // it a restart rather than an end.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarRequestedPower);
        assert_eq!(iec.state(), AcState::Charging);

        let commands = iec.handle(IecInput::SessionRestart);

        assert_eq!(iec.state(), AcState::WaitingForAuthentication);
        assert_eq!(
            commands,
            vec![IecCommand::AllowPowerOn(false), IecCommand::PwmOff]
        );
        assert!(iec.authorized, "the authorization survives a restart");
        assert!(iec.transaction_active, "the record stays open");
    }

    #[test]
    fn a_session_restart_from_rest_or_out_of_service_moves_nothing() {
        // A port at rest is already where a restart would send it, and one out
        // of service must not be moved into a state that advertises
        // availability.
        for state in [AcState::Idle, AcState::Disabled, AcState::Startup] {
            let mut iec = iec();
            iec.state = state;
            assert!(iec.handle(IecInput::SessionRestart).is_empty(), "{state:?}");
            assert_eq!(iec.state(), state, "{state:?}");
        }
    }

    /// Every entry point that can offer a duty cycle, so the table below is
    /// exhaustive over PWM sources rather than a sample of them.
    #[derive(Clone, Copy, Debug)]
    enum PwmSource {
        CurrentLimitChanged,
        TransactionStarted,
        ResumeRequested,
    }

    fn offers_pwm(state: AcState, source: PwmSource) -> bool {
        let mut iec = iec();
        // The most permissive surroundings the state admits, so a false below is
        // the state's answer and not a missing precondition.
        iec.state = state;
        iec.pwm_running = true;
        iec.ev_plugged_in = true;
        iec.authorized = true;

        let commands = match source {
            PwmSource::CurrentLimitChanged => iec.set_current_limit_a(16.0),
            PwmSource::TransactionStarted => iec.handle(IecInput::TransactionStarted),
            PwmSource::ResumeRequested => iec.handle(IecInput::ResumeRequested),
        };

        commands.iter().any(|c| matches!(c, IecCommand::PwmOn(_)))
    }

    #[test]
    fn every_state_and_entry_point_agrees_on_whether_a_pwm_offer_is_possible() {
        // Columns: current limit change, transaction start, resume. A true
        // where the state itself is not PWM eligible means the
        // entry point transitions into an eligible state before offering, which
        // is the only shape allowed: the offer is always made from a state
        // `pwm_eligible` admits.
        let table = [
            (AcState::Startup, [false, false, false]),
            (AcState::Idle, [false, false, false]),
            (AcState::WaitingForAuthentication, [false, true, false]),
            (AcState::PrepareCharging, [true, false, false]),
            (AcState::Charging, [true, false, false]),
            (AcState::ChargingPausedEv, [true, false, false]),
            (AcState::ChargingPausedEvse, [false, false, true]),
            (AcState::StoppingCharging, [false, false, false]),
            (AcState::Finished, [false, false, false]),
            (AcState::Disabled, [false, false, false]),
        ];

        assert_eq!(
            table.len(),
            10,
            "every AcState variant must appear in the table"
        );

        let sources = [
            PwmSource::CurrentLimitChanged,
            PwmSource::TransactionStarted,
            PwmSource::ResumeRequested,
        ];

        for (state, expected) in table {
            for (source, expected) in sources.iter().zip(expected) {
                assert_eq!(
                    offers_pwm(state, *source),
                    expected,
                    "{state:?} via {source:?}"
                );
            }
        }
    }

    #[test]
    fn a_pwm_offer_is_only_ever_made_from_a_pwm_eligible_state() {
        // The invariant the table encodes, checked on the reducer itself: after
        // any input that produced a PwmOn, the resulting state is eligible.
        let inputs = [
            IecInput::TransactionStarted,
            IecInput::ResumeRequested,
        ];
        let states = [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::StoppingCharging,
            AcState::Finished,
            AcState::Disabled,
        ];

        for state in states {
            for input in inputs {
                let mut iec = iec();
                iec.state = state;
                iec.pwm_running = true;
                iec.ev_plugged_in = true;
                iec.authorized = true;

                let offered = iec
                    .handle(input)
                    .iter()
                    .any(|c| matches!(c, IecCommand::PwmOn(_)));

                if offered {
                    assert!(iec.pwm_eligible(), "{state:?} via {input:?}");
                }
            }
        }
    }

    /// `Charging` has one origin: the preparation.
    ///
    /// `grant_power` is the only writer of the state, but being the only
    /// writer says nothing about where its callers arrive from, and that is
    /// what the emitted session events depend on. `Charger` reaches
    /// `EvseState::Charging` from exactly one place,
    /// `Charger::run_state_machine`'s `PrepareCharging` arm, so every route
    /// this reducer has into `Charging` must be resident in `PrepareCharging`
    /// first, or already charging from a repeated state C reading.
    ///
    /// Checked by driving every state and input pair rather than by naming the
    /// routes, so a new route that skips the preparation fails here instead of
    /// silently emitting `ChargingStarted` on its own.
    #[test]
    fn charging_is_only_ever_entered_from_the_preparation() {
        let inputs = [
            IecInput::StartupComplete,
            IecInput::Enable,
            IecInput::Disable,
            IecInput::CarPluggedIn,
            IecInput::CarUnplugged,
            IecInput::AuthorizationAccepted,
            IecInput::TransactionStarted,
            IecInput::CarRequestedPower,
            IecInput::CarRequestedVentilatedPower,
            IecInput::CarRequestedStopPower,
            IecInput::CpStateE,
            IecInput::CpStateF,
            IecInput::PowerOn,
            IecInput::PowerOff,
            IecInput::StopRequested,
            IecInput::PauseRequested,
            IecInput::ResumeRequested,
            IecInput::EmergencyShutdown,
            IecInput::ErrorShutdown,
            IecInput::SwitchPhasesRequested(true),
            IecInput::FaultStateFExpired,
            IecInput::CpStateFUnlockTimerExpired,
            IecInput::CpStateC1TimeoutExpired,
            IecInput::StoppingChargingTimeoutExpired,
            IecInput::SwitchPhasesDelayExpired,
            IecInput::SessionRestart,
            IecInput::ReinitStarted,
            IecInput::ReinitFinished,
        ];
        let states = [
            AcState::Startup,
            AcState::Idle,
            AcState::WaitingForAuthentication,
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::SwitchPhases,
            AcState::StoppingCharging,
            AcState::Reinit,
            AcState::Finished,
            AcState::Disabled,
        ];
        assert_eq!(
            states.len(),
            12,
            "every AcState variant must be a starting state here"
        );
        assert_eq!(
            inputs.len(),
            28,
            "every IecInput variant must be driven here"
        );

        let mut entries = 0;
        for state in states {
            for input in inputs {
                // Both contactor readings: the switching break and the resume
                // both reach `grant_power` only on a contactor still reported
                // closed, so one reading alone would leave that route undriven.
                for contactor_open in [false, true] {
                    let mut iec = iec();
                    iec.state = state;
                    iec.pwm_running = true;
                    iec.ev_plugged_in = true;
                    iec.authorized = true;
                    iec.transaction_active = true;
                    iec.contactor_open = contactor_open;
                    iec.pending_three_phases = Some(true);

                    iec.handle(input);

                    let mut route = vec![state];
                    route.extend(iec.take_transitions());
                    for (before, after) in route.iter().zip(route.iter().skip(1)) {
                        if *after != AcState::Charging {
                            continue;
                        }
                        entries += 1;
                        assert!(
                            matches!(before, AcState::PrepareCharging | AcState::Charging),
                            "{state:?} via {input:?} entered Charging from {before:?}"
                        );
                    }
                }
            }
        }
        // The drive has to actually reach the state, or the assertion above is
        // vacuous. Four routes do: state C and state D out of the preparation
        // and out of the vehicle's pause, each with the contactor either way.
        assert!(entries >= 8, "only {entries} entries into Charging driven");
    }
    #[test]
    fn an_unplug_leaves_a_disabled_port_disabled() {
        // `Charger.cpp:237-238`: Idle is re-entered only with the disable
        // request clear. Landing in Idle with it still set makes the port
        // advertise availability again with no enable ever issued.
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::Disable);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Disabled);
        assert_eq!(
            commands.last(),
            Some(&IecCommand::Enable(false)),
            "got {commands:?}"
        );
        assert!(commands.contains(&IecCommand::CpStateF), "got {commands:?}");
    }

    #[test]
    fn an_unplug_returns_a_port_in_service_to_idle() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);

        let commands = iec.handle(IecInput::CarUnplugged);

        assert_eq!(iec.state(), AcState::Idle);
        assert!(
            !commands
                .iter()
                .any(|command| matches!(command, IecCommand::Enable(_))),
            "an ordinary unplug does not touch the board output, got {commands:?}"
        );
    }

    #[test]
    fn a_disabled_port_stays_disabled_across_an_unplug_and_a_replug() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::Disable);
        iec.handle(IecInput::CarUnplugged);

        let commands = iec.handle(IecInput::CarPluggedIn);

        assert_eq!(iec.state(), AcState::Disabled);
        assert!(
            commands.is_empty(),
            "a disabled port answers a plug in with nothing, got {commands:?}"
        );
    }

    #[test]
    fn an_unplug_clears_the_authorization_and_the_transaction() {
        // `Charger.cpp:231-232`. Both are per session, and both strand the next
        // one if they survive: a stale authorization starts a transaction the
        // next vehicle never asked for, and a stale transaction blocks the one
        // it does ask for.
        let mut iec = iec();
        prepare_charging(&mut iec);
        iec.handle(IecInput::CarUnplugged);
        iec.handle(IecInput::CarPluggedIn);

        assert!(
            iec.handle(IecInput::TransactionStarted).is_empty(),
            "the next vehicle needs an authorization of its own"
        );
        assert_eq!(iec.state(), AcState::WaitingForAuthentication);

        iec.handle(IecInput::AuthorizationAccepted);
        let commands = iec.handle(IecInput::TransactionStarted);

        assert_eq!(
            iec.state(),
            AcState::PrepareCharging,
            "the previous transaction must not block this one"
        );
        assert!(
            commands
                .iter()
                .any(|command| matches!(command, IecCommand::PwmOn(_))),
            "got {commands:?}"
        );
    }

    #[test]
    fn a_fatal_error_on_an_out_of_service_port_leaves_it_out_of_service() {
        // `Charger.cpp:201-209`: only an enable leaves the disabled arm.
        // Landing in stopping instead would report the port back in service
        // with the request still outstanding, and nothing short of an unplug
        // would return it.
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::CarPluggedIn);
        iec.handle(IecInput::Disable);

        let commands = iec.handle(IecInput::ErrorShutdown);

        assert_eq!(iec.state(), AcState::Disabled);
        assert!(
            !commands.contains(&IecCommand::ArmTimer(AcTimer::StoppingCharging)),
            "there is no session to stop on an out of service port, got {commands:?}"
        );
        assert!(
            commands.contains(&IecCommand::AllowPowerOn(false))
                && commands.contains(&IecCommand::CpStateF),
            "got {commands:?}"
        );
    }

    #[test]
    fn an_emergency_on_an_out_of_service_port_still_needs_an_enable_to_return() {
        let mut iec = iec();
        iec.handle(IecInput::StartupComplete);
        iec.handle(IecInput::Disable);
        iec.handle(IecInput::EmergencyShutdown);

        assert_eq!(iec.state(), AcState::Disabled);

        let commands = iec.handle(IecInput::Enable);

        assert_eq!(iec.state(), AcState::Idle);
        assert_eq!(
            commands,
            vec![IecCommand::Enable(true), IecCommand::CpStateX1],
            "the enable is what returns the port to service"
        );
    }

    #[test]
    fn a_fatal_error_on_a_port_in_service_still_stops_the_session() {
        let mut iec = iec();
        prepare_charging(&mut iec);

        let commands = iec.handle(IecInput::ErrorShutdown);

        assert_eq!(iec.state(), AcState::StoppingCharging);
        assert!(
            commands.contains(&IecCommand::ArmTimer(AcTimer::StoppingCharging)),
            "got {commands:?}"
        );
    }

    #[test]
    fn a_fatal_error_holds_the_vehicle_for_a_bounded_time_and_then_releases_it() {
        // The connector stays latched while the port signals state F so a
        // vehicle cannot be pulled mid fault. The bound is what makes the hold
        // recoverable rather than permanent, and it holds whether or not the
        // port is in service.
        for disabled in [false, true] {
            let mut iec = iec();
            iec.handle(IecInput::StartupComplete);
            iec.handle(IecInput::CarPluggedIn);
            if disabled {
                iec.handle(IecInput::Disable);
            }

            let commands = iec.handle(IecInput::ErrorShutdown);

            assert!(
                commands.contains(&IecCommand::ArmTimer(AcTimer::CpStateFUnlock)),
                "disabled {disabled}, got {commands:?}"
            );
            assert!(
                !commands.contains(&IecCommand::UnlockConnector),
                "the release waits for the bound, got {commands:?}"
            );
            assert_eq!(
                iec.handle(IecInput::CpStateFUnlockTimerExpired),
                vec![IecCommand::UnlockConnector]
            );
        }
    }
}
