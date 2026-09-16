// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The `ac_with_soc` power path: AC hardware that presents itself as DC until
//! the vehicle reports a state of charge.
//!
//! IEC 61851-1 has no way to ask a vehicle how full its battery is. ISO 15118
//! DC charging does, in `DcEvStatus`, so this mode announces a DC port over
//! ISO 15118, lets the vehicle run a DC charge parameter discovery, takes the
//! state of charge out of it and then reintroduces the same vehicle to the same
//! port as a basic AC one. Nothing DC is ever delivered: there is no power
//! supply, the DC envelope the vehicle is told is hardcoded, and the flip
//! happens during the handshake, before any DC power delivery stage.
//!
//! The C++ does this by re-parameterizing one state machine
//! (`EvseManager::setup_fake_DC_mode` and `setup_AC_mode`, each of which calls
//! `Charger::setup` with a different `ChargeMode`). That is the only thing in
//! the C++ that would make a `PowerPath` runtime mutable, which is why the mode
//! is a path of its own here instead: the trait object is still chosen once,
//! and what changes inside it is `hlc::PresentedMode`, a fact about what the
//! vehicle is told rather than about which implementation runs.
//!
//! Underneath, `ac::AcHlc` carries the session in both presented modes, because
//! underneath is where the hardware is and the hardware is AC throughout. Two
//! of its answers read the presented mode, and both are the same C++
//! `charge_mode` branch: whether a five percent offer stands, and how the
//! authorization loop leaves the start. See `ac::AcHlc::set_presented`.

use std::time::{Duration, Instant};

use crate::core::config::ReinitMethod;
use crate::core::effect::{Effect, EffectId, EffectOutcome, HlcUpdate, TimerId};
use crate::core::event::{BspEvent, CpEdges};
use crate::core::hlc::{DataLinkRequest, PresentedMode};
use crate::core::path::ac::AcHlc;
use crate::core::path::iec::{AcState, IecConfig};
use crate::core::path::{PathEvent, PowerPath, SessionDuty};
use crate::core::session::{PwmStart, Session, StopReason};

/// The deadline that takes an AC presented port back to the fake DC mode so a
/// fresh state of charge can be collected.
///
/// **This is a deliberate divergence, and the C++ figure is a defect.**
/// `Charger::setup` seeds `shared_context.ac_with_soc_timer` with `3600000`,
/// which reads as one hour of milliseconds and is plainly what was meant. What
/// the C++ then does with it is subtract `50` once per pass of
/// `Charger::main_thread`, whose `MAINLOOP_UPDATE_RATE` is **100 ms** and whose
/// wait returns early whenever a board support event arrives. So the realized
/// period is about two hours on an idle port, and shrinks towards one hour and
/// below as event traffic rises: it is a function of how busy the port is, not
/// of time. That is a C++ defect, and this port does not reproduce it.
///
/// One real hour is used instead. It is the value the constant names, it is the
/// value the manifest description implies, and it cannot drift with load. The
/// deadline is armed on the flip to AC and cancelled on the flip to DC, which
/// is what `Charger::setup`'s `_ac_with_soc_timeout` argument does: false from
/// `setup_fake_DC_mode` and true from `setup_AC_mode`, so the countdown exists
/// exactly while AC is presented.
pub const DC_REFRESH_AFTER: Duration = Duration::from_secs(3600);

/// Identity of that deadline. The reinitialization hold uses
/// `ac::TIMER_REINIT`, which is the reducer's and is armed by it.
///
/// In the `ac::` block rather than a block of its own, because this path drives
/// an `ac::AcHlc` and every timer that reaches it has to be distinguishable
/// from that path's own. The whole module's identities are one space:
/// `Core::apply` matches its own two before it offers a deadline to the path,
/// so an identity shared with one of those never reaches a path at all. That is
/// what this constant did on its first revision, and
/// `every_timer_identity_in_the_module_is_unique` is what would have caught it.
pub const TIMER_DC_REFRESH: TimerId = TimerId(208);

/// Where the reinitialization sequence has got to.
///
/// It is a sequence rather than a single transition because
/// `Charger::process_pending_reinit_request` cannot break the control pilot
/// while SLAC is still matched: it asks the vehicle to end its ISO 15118
/// session and comes back on a later pass. The port waits on the same fact, and
/// on an event rather than on a poll.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
enum Reinit {
    /// No reinitialization outstanding.
    #[default]
    Idle,
    /// The vehicle has been asked to end its ISO 15118 session and the link is
    /// still matched. `Charger::process_pending_reinit_request`'s
    /// `if (shared_context.slac_matched)` branch, which sets `reinit_running`
    /// and returns.
    AwaitingUnmatched,
    /// The pilot level is being held and `ac::TIMER_REINIT` is armed.
    Holding,
}

pub struct AcWithSoc {
    /// The path that carries the session in both presented modes.
    ac: AcHlc,
    /// What the vehicle is being told this port is.
    ///
    /// The only mode in the module that changes at runtime, and `present` below
    /// is its only writer. Everything that reads it reads it from here: the
    /// inner path is told through `AcHlc::set_presented`, the stack is told
    /// through a `SessionDuty::AnnounceMode`, and the session setup reads it
    /// through `presents_fake_dc`.
    presented: PresentedMode,
    /// Whether the SLAC link is matched. `Charger::shared_context.slac_matched`.
    slac_matched: bool,
    /// Whether the board can drive the control pilot to 0 V. Seeded false, the
    /// value `EvseManager::init`, which seeds its own capability handle seeds its capability handle with, and
    /// replaced by the board's own report.
    supports_cp_state_e: bool,
    reinit: Reinit,
    /// `reinit_method`, read only to answer the refusal
    /// `Charger::start_reinit` makes for the `CPStateE` method on a board that
    /// cannot produce it. The reducer holds the same value and is what acts on
    /// it; a copy here rather than a reach through `AcHlc` into its reducer's
    /// configuration, because `IecConfig` is `Copy` and the alternative is a
    /// second accessor on the path between them.
    reinit_method: ReinitMethod,
    /// Whether `TIMER_DC_REFRESH` is armed, so it is cancelled exactly when it
    /// was armed.
    dc_refresh_armed: bool,
    /// Duties this path raised itself. Kept apart from the inner path's, which
    /// come from its own state edges, and drained with them.
    duties: Vec<SessionDuty>,
}

impl AcWithSoc {
    pub fn new(config: IecConfig) -> Self {
        Self {
            // The offer variant is not read from configuration on this path.
            // the `WaitingForAuthentication` entry of `Charger::run_state_machine` derives it from the charge mode first and
            // its DC branch sets it unconditionally, so the fake DC mode always
            // starts on five percent whatever `ac_hlc_use_5percent` says, and
            // the AC mode never does because the flip's `setup_AC_mode(false)`
            // disables high level communication for the session. `AcHlc` gets
            // told the mode instead, and `derive_five_percent` short circuits
            // on it; the value handed in here is what it would answer for the
            // fake DC mode anyway.
            ac: {
                let mut ac = AcHlc::new(config, PwmStart::FivePercent);
                ac.set_presented(PresentedMode::Dc);
                ac
            },
            presented: PresentedMode::Dc,
            slac_matched: false,
            supports_cp_state_e: false,
            reinit: Reinit::default(),
            reinit_method: config.reinit_method,
            dc_refresh_armed: false,
            duties: Vec::new(),
        }
    }

    /// **The only writer of the presented mode.**
    ///
    /// Every flip in the module goes through here, and there are four routes
    /// into it: the boot, which starts in the fake DC mode
    /// (`EvseManager::ready` calls `setup_fake_DC_mode` in place of
    /// `Charger::setup`); the state of charge, which flips to AC
    /// (`switch_AC_mode`); the deadline and the unplug, which flip back to DC
    /// (`switch_DC_mode`); and the data link error, which flips to AC without a
    /// reinitialization (the `subscribe_dlink_error` arm).
    ///
    /// It does three things and they belong together, which is why they are not
    /// three functions: the mode is recorded, the inner path is told so its two
    /// mode dependent answers move with it, and the stack is owed a
    /// re-announcement. A route that did two of the three would leave the
    /// vehicle being told one thing and the pilot doing another.
    ///
    /// The deadline travels with the mode for the same reason: it is
    /// `Charger::setup`'s `_ac_with_soc_timeout` argument, which every one of
    /// those C++ call sites passes as a function of the mode it is setting up.
    ///
    /// Re-presenting the mode already presented is not a flip and does nothing.
    /// `Charger` would re-run the whole setup; here that would send the vehicle
    /// a second identical announcement and re-arm a deadline that is already
    /// measuring the right interval.
    fn present(&mut self, mode: PresentedMode) -> Vec<Effect> {
        if self.presented == mode {
            return Vec::new();
        }
        self.presented = mode;
        self.ac.set_presented(mode);
        self.duties.push(SessionDuty::AnnounceMode(mode));
        match mode {
            PresentedMode::Ac => self.arm_dc_refresh(),
            PresentedMode::Dc => self.cancel_dc_refresh(),
        }
    }

    fn arm_dc_refresh(&mut self) -> Vec<Effect> {
        self.dc_refresh_armed = true;
        vec![Effect::StartTimer {
            id: TIMER_DC_REFRESH,
            after: DC_REFRESH_AFTER,
        }]
    }

    fn cancel_dc_refresh(&mut self) -> Vec<Effect> {
        if !self.dc_refresh_armed {
            return Vec::new();
        }
        self.dc_refresh_armed = false;
        vec![Effect::CancelTimer {
            id: TIMER_DC_REFRESH,
        }]
    }

    /// The state of charge arrived, so the session becomes a basic AC one.
    ///
    /// `EvseManager::switch_AC_mode`, in its order: the AC setup, then
    /// `Charger::start_reinit`. Only the first flip does anything; a vehicle
    /// that keeps reporting its state of charge through the AC session is
    /// already where those reports would send it, which is what makes
    /// `present` idempotent.
    fn on_state_of_charge(&mut self) -> Vec<Effect> {
        let mut effects = self.present(PresentedMode::Ac);
        effects.extend(self.start_reinit());
        effects
    }

    /// `Charger::start_reinit` plus `process_pending_reinit_request`, which are
    /// one decision split over two passes in the C++ and one decision with a
    /// wait here.
    ///
    /// The three refusals are `start_reinit`'s. The out of service and no
    /// vehicle cases are the reducer's to answer, because the state is what
    /// says so and the reducer owns the state; what is answered here are the
    /// two this path holds the facts for.
    fn start_reinit(&mut self) -> Vec<Effect> {
        if self.reinit != Reinit::Idle {
            // `Charger::start_reinit`'s `reinit_running` guard: a request while
            // a sequence runs is skipped rather than restarting it.
            return Vec::new();
        }
        if !self.supports_cp_state_e && self.reinit_method == ReinitMethod::CpStateE {
            // `Charger::start_reinit` refuses outright rather than falling back
            // to X1, so the flip happens and the vehicle is never reintroduced.
            // Reproduced: substituting a level here would silently change what
            // an operator configured.
            log::warn!(
                "reinit requested with control pilot state E, which the board does not support"
            );
            return Vec::new();
        }
        if self.slac_matched {
            // `process_pending_reinit_request`: ask the vehicle to end its ISO
            // 15118 session and come back when the link is down. Breaking the
            // pilot under a matched link is what this wait exists to avoid.
            self.reinit = Reinit::AwaitingUnmatched;
            return self.ask_vehicle_to_stop_for_reinit();
        }
        self.enter_reinit()
    }

    /// `signal_hlc_stop_charging` as `process_pending_reinit_request` sends it,
    /// which is `call_stop_charging(true)`.
    ///
    /// Sent from here rather than raised as a `SessionDuty::AskVehicleToStop`,
    /// because that duty is the `StoppingCharging` entry's and carries that
    /// entry's ISO 15118-20 pause branch. This one has no branch: the C++ sends
    /// the stop unconditionally, and a pause would leave the link matched,
    /// which is the one thing the wait below cannot tolerate.
    fn ask_vehicle_to_stop_for_reinit(&self) -> Vec<Effect> {
        vec![Effect::HlcUpdate(HlcUpdate::StopCharging(true))]
    }

    /// Enter the hold. The reducer signals the configured pilot level and arms
    /// its own deadline; a zero configured duration runs the whole sequence in
    /// one call, which is why the phase is read back off the state rather than
    /// assumed.
    fn enter_reinit(&mut self) -> Vec<Effect> {
        let effects = self.ac.start_reinit();
        self.reinit = if self.ac.state() == AcState::Reinit {
            Reinit::Holding
        } else {
            // Either the reducer refused, from a resting or out of service
            // state, or a zero duration took the sequence straight back out
            // again. Neither leaves anything outstanding.
            Reinit::Idle
        };
        effects
    }

    /// Every field whose lifetime is one session, cleared in one place, on the
    /// terms `AcHlc::end_session` sets out: the routes that end a session are
    /// the unplug and the port leaving service, and a sequence that survived
    /// one would be acted on against the next vehicle.
    ///
    /// The presented mode is deliberately **not** cleared here. It outlives the
    /// session, exactly as `EvseManager::fake_dc_enabled` does, and the unplug
    /// route sets it itself: `EvseManager` connects `switch_DC_mode` to the
    /// unplug so the next session starts in the fake DC mode again.
    ///
    /// `slac_matched` is not cleared either. It is a fact about the SLAC layer
    /// rather than about this session, it has exactly one writer, and SLAC
    /// reports `UNMATCHED` on its own when the vehicle goes; clearing it here
    /// would be a second writer answering for a layer that had not spoken.
    fn end_session(&mut self) -> Vec<Effect> {
        let holding = self.reinit == Reinit::Holding;
        self.reinit = Reinit::Idle;
        let mut effects = self.cancel_dc_refresh();
        // The hold's deadline is armed by the reducer and cancelled here,
        // because nothing else will: the unplug route leaves `AcState::Reinit`
        // through the resting state, so the reducer never sees the hold end,
        // and a deadline that expired against the next vehicle would take its
        // session out of `WaitingForAuthentication` with no authorization.
        if holding {
            effects.push(Effect::CancelTimer {
                id: crate::core::path::ac::TIMER_REINIT,
            });
        }
        effects
    }
}

impl PowerPath for AcWithSoc {
    fn name(&self) -> &'static str {
        "AcWithSoc"
    }

    /// The fake DC mode, from the first moment.
    ///
    /// `EvseManager::init` seeds `fake_dc_enabled` from `config.ac_with_soc`
    /// during `init`, before the session setup at `:362` reads it, and `ready`
    /// then calls `setup_fake_DC_mode` in place of `Charger::setup`. So a port
    /// in this mode has never been anything but fake DC by the time anything
    /// asks.
    /// AC hardware throughout, whichever mode it is presenting: the DC
    /// envelope the vehicle is told in fake DC mode is hardcoded in
    /// `setup_fake_DC_mode` and no supply is driven, so there is no voltage
    /// target. See `AcBasic::target_voltage_v`.
    fn target_voltage_v(&self) -> f64 {
        0.0
    }

    fn presents_fake_dc(&self) -> bool {
        self.presented == PresentedMode::Dc
    }

    fn take_entered_states(&mut self) -> Vec<AcState> {
        self.ac.take_entered_states()
    }

    fn state(&self) -> AcState {
        self.ac.state()
    }

    /// The inner path's answer, which is the AC one in both presented modes.
    ///
    /// It is **not** the DC path's constant true. the `Idle` entry of `Charger::run_state_machine` decides
    /// this at the `Idle` entry from the charge mode, and a fake DC port
    /// reaches that entry with `ChargeMode::DC` configured, so the C++ does
    /// answer true there. Reproducing that would need this path to hold a
    /// second copy of the fact and keep it in step with the flip, and the copy
    /// would be wrong on the AC side of every flip; the inner path's own
    /// `v2g_setup_finished` writer reaches the same answer for the fake DC
    /// session the moment the vehicle asks for power over ISO 15118, which is
    /// before anything reads it.
    fn hlc_charging_active(&self) -> bool {
        self.ac.hlc_charging_active()
    }

    /// The inner path's, as every mode fact this path does not own is.
    /// `Charger::power_available` branches on `config.charge_mode`, which is
    /// `AC` for this deployment whichever mode is being presented, so the flip
    /// does not reach it.
    fn power_available(&self) -> bool {
        self.ac.power_available()
    }

    fn signalled_current_a(&self) -> f64 {
        self.ac.signalled_current_a()
    }

    /// The boot, which is `EvseManager::ready` taking the `if (config.ac_with_soc)`
    /// branch: `setup_fake_DC_mode` instead of `Charger::setup`.
    ///
    /// The announcement is owed even though the mode has not changed, because
    /// the boot sequence the core has just sent derived its advertised set from
    /// `charge_mode`, which is AC. `present` would see no change and say
    /// nothing, so the duty is raised directly. That is the C++ order too: the
    /// `init` block announces the AC set at `:974` and `ready` overwrites it
    /// with the fake DC one.
    fn on_startup(&mut self) -> Vec<Effect> {
        self.duties
            .push(SessionDuty::AnnounceMode(PresentedMode::Dc));
        self.ac.on_startup()
    }

    fn on_session_start(&mut self, session: &Session, now: Instant) -> Vec<Effect> {
        self.ac.on_session_start(session, now)
    }

    fn on_authorized(&mut self, session: &Session, now: Instant) -> Vec<Effect> {
        self.ac.on_authorized(session, now)
    }

    /// The board support facts, plus the unplug flip back to the fake DC mode.
    ///
    /// The flip runs **before** the inner path sees the pilot event, which is
    /// the C++ order and comes from registration order rather than from a
    /// decision: `EvseManager` connects its `ac_with_soc` unplug handler at
    /// `:1076`, inside the `if (hlc_enabled)` block, and the general forwarding
    /// handler that reaches `Charger` at `:1091`. everest-framework dispatches
    /// one topic's handlers in registration order, so `switch_DC_mode` always
    /// runs first.
    fn on_bsp(
        &mut self,
        session: &Session,
        event: &BspEvent,
        edges: CpEdges,
        now: Instant,
    ) -> Vec<Effect> {
        if let BspEvent::Capabilities(caps) = event {
            self.supports_cp_state_e = caps.supports_cp_state_e;
        }
        let unplugged = matches!(event, BspEvent::Cp(cp) if cp.is_unplug());
        let mut effects = if unplugged {
            // `EvseManager::ready`'s `if (config.ac_with_soc)` unplug arm: configure for the fake DC mode again
            // so the next session starts where this one did.
            let mut effects = self.present(PresentedMode::Dc);
            effects.extend(self.end_session());
            effects
        } else {
            Vec::new()
        };
        effects.extend(self.ac.on_bsp(session, event, edges, now));
        effects
    }

    fn on_limits_changed(&mut self, session: &Session, now: Instant) -> Vec<Effect> {
        self.ac.on_limits_changed(session, now)
    }

    fn on_stop(&mut self, session: &Session, reason: StopReason, now: Instant) -> Vec<Effect> {
        self.ac.on_stop(session, reason, now)
    }

    fn on_timer(&mut self, session: &Session, id: TimerId, now: Instant) -> Vec<Effect> {
        if id == TIMER_DC_REFRESH {
            if !self.dc_refresh_armed {
                return Vec::new();
            }
            self.dc_refresh_armed = false;
            // `Charger::signal_ac_with_soc_timeout` is connected to
            // `switch_DC_mode` and to nothing else, and `switch_DC_mode` is
            // `setup_fake_DC_mode` alone: no reinitialization, no session
            // movement. The port goes back to presenting DC so the vehicle's
            // next ISO 15118 session reports a fresh state of charge, and a
            // charge already running keeps running.
            return self.present(PresentedMode::Dc);
        }
        if id == crate::core::path::ac::TIMER_REINIT {
            // The hold ended. The reducer restarts the session and the inner
            // path runs its authorization loop behind it; what is left here is
            // the phase.
            self.reinit = Reinit::Idle;
        }
        self.ac.on_timer(session, id, now)
    }

    fn on_effect_done(
        &mut self,
        session: &Session,
        id: Option<EffectId>,
        outcome: &EffectOutcome,
        now: Instant,
    ) -> Vec<Effect> {
        self.ac.on_effect_done(session, id, outcome, now)
    }

    fn on_path_event(&mut self, session: &Session, event: PathEvent, now: Instant) -> Vec<Effect> {
        match event {
            // The whole reason the mode exists.
            PathEvent::StateOfCharge { .. } => {
                let mut effects = self.on_state_of_charge();
                effects.extend(self.ac.on_path_event(session, event, now));
                effects
            }

            // The gate the reinitialization waits on. A link that has just gone
            // unmatched releases a sequence that was waiting for exactly that,
            // which is the pass of `process_pending_reinit_request` that finds
            // `slac_matched` false and moves the state.
            PathEvent::SlacMatched(matched) => {
                self.slac_matched = matched;
                let mut effects = if !matched && self.reinit == Reinit::AwaitingUnmatched {
                    self.enter_reinit()
                } else {
                    Vec::new()
                };
                effects.extend(self.ac.on_path_event(session, event, now));
                effects
            }

            // `EvseManager::ready`'s `subscribe_dlink_error` arm: a data link error while the fake DC
            // mode is presented flips back to AC, and does so **before**
            // `charger->dlink_error()` runs. No reinitialization travels with
            // it: `switch_AC_mode` is not called here, only `setup_AC_mode`, so
            // the vehicle is left to the pilot detour the inner path's own data
            // link handling runs.
            //
            // A data link error while AC is presented is not a flip. `present`
            // answers that by being idempotent, so the arm does not have to
            // test the mode.
            PathEvent::DataLink(DataLinkRequest::Error) => {
                let mut effects = self.present(PresentedMode::Ac);
                effects.extend(self.ac.on_path_event(session, event, now));
                effects
            }

            // A disable ends the session under this path the same way an unplug
            // does, and for the same reason: a sequence or a deadline left
            // standing would be acted on against the next vehicle, and the
            // reinitialization's next step signals a pilot level a port out of
            // service must not be deciding about.
            //
            // The mode is deliberately not flipped. The C++ connects
            // `switch_DC_mode` to the unplug and to the deadline, and to
            // nothing else, so a port that leaves service while presenting AC
            // comes back presenting AC.
            PathEvent::Disable => {
                let mut effects = self.end_session();
                effects.extend(self.ac.on_path_event(session, event, now));
                effects
            }

            // Everything else is the inner path's. It is exhaustive over
            // `PathEvent` itself, which is where a new variant becomes a
            // compile error for this path too.
            PathEvent::Enable
            | PathEvent::PauseRequested
            | PathEvent::ResumeRequested
            | PathEvent::SwitchPhases { .. }
            | PathEvent::HlcSessionSetup
            | PathEvent::MatchingStarted(_)
            | PathEvent::SlacErrorRoutine
            | PathEvent::SetupFinished
            | PathEvent::AllowCloseContactor(_)
            | PathEvent::DataLink(DataLinkRequest::Pause | DataLinkRequest::Terminate)
            | PathEvent::OpenContactorDc
            | PathEvent::CableCheckRequired
            | PathEvent::PreChargeStarted
            | PathEvent::CurrentDemandStarted
            | PathEvent::CurrentDemandFinished
            | PathEvent::StopFromEv
            | PathEvent::SupplyVoltage { .. }
            | PathEvent::DcEvTarget { .. }
            | PathEvent::DcDynamicChargeMode(_)
            | PathEvent::DcEvMaximumLimits(_)
            | PathEvent::DcEnforcedLimits { .. }
            | PathEvent::DcExportVoltageRange { .. }
            | PathEvent::Isolation(_)
            | PathEvent::IsolationSelfTest(_)
            | PathEvent::OverVoltageMeasurement { .. }
            | PathEvent::MeterVoltage { .. }
            | PathEvent::BidirectionalWithdrawn => self.ac.on_path_event(session, event, now),
        }
    }

    /// This path's own duties and the inner path's, drained together.
    ///
    /// One drain, because the core takes duties once per pass. Ordered with the
    /// inner path's first, so a flip's announcement follows the session events
    /// of the edges that pass crossed rather than interleaving with them.
    fn take_session_duties(&mut self) -> Vec<SessionDuty> {
        let mut duties = self.ac.take_session_duties();
        duties.append(&mut self.duties);
        duties
    }

    fn to_safe_state(&mut self) -> Vec<Effect> {
        let mut effects = self.ac.to_safe_state();
        effects.extend(self.end_session());
        effects
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::{ReinitMethod, SwitchCpState};
    use crate::core::effect::CpState;
    use crate::core::event::{CpEvent, HardwareCapabilities};
    use crate::core::path::ac::{PWM_5_PERCENT, TIMER_REINIT};
    use crate::core::session::{Limits, SessionEvent};

    fn config_with(method: ReinitMethod, reinit_duration: Duration) -> IecConfig {
        IecConfig {
            initial_current_limit_a: 16.0,
            has_ventilation: true,
            lock_connector_in_state_b: true,
            switch_phases_cp_state: SwitchCpState::X1,
            switch_phases_delay: Duration::from_secs(10),
            reinit_method: method,
            reinit_duration,
            hlc_no_energy_timeout: Duration::from_secs(5),
            type2_socket: false,
        }
    }

    fn config() -> IecConfig {
        config_with(ReinitMethod::CpStateF, Duration::from_millis(3000))
    }

    fn now() -> Instant {
        Instant::now()
    }

    /// The session an `ac_with_soc` port is handed. `pwm_start` is deliberately
    /// nominal: the mode ignores it, and a test that handed it the five percent
    /// spelling could not tell the mode's own answer from the setting's.
    fn session() -> Session {
        Session::new(
            PwmStart::Nominal,
            Limits {
                max_current_a: 16.0,
                nr_of_phases_available: 3,
            },
        )
    }

    fn booted(config: IecConfig) -> AcWithSoc {
        let mut path = AcWithSoc::new(config);
        path.on_startup();
        path.take_session_duties();
        path
    }

    fn caps(supports_cp_state_e: bool) -> BspEvent {
        BspEvent::Capabilities(HardwareCapabilities {
            max_current_a_import: 32.0,
            min_current_a_import: 6.0,
            max_phase_count_import: 3,
            min_phase_count_import: 1,
            max_current_a_export: 0.0,
            min_current_a_export: 0.0,
            max_phase_count_export: 0,
            min_phase_count_export: 0,
            supports_changing_phases_during_charging: false,
            supports_cp_state_e,
        })
    }

    /// A session running in the fake DC mode: the vehicle is plugged in, SLAC
    /// is matched, the stack has set the session up and authorization has
    /// arrived, which is where a state of charge shows up.
    fn presenting_dc(config: IecConfig) -> (AcWithSoc, Session) {
        let mut path = booted(config);
        let s = session();
        path.on_bsp(&s, &caps(true), CpEdges::default(), now());
        path.on_session_start(&s, now());
        path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
        path.on_path_event(&s, PathEvent::SlacMatched(true), now());
        path.on_path_event(&s, PathEvent::SetupFinished, now());
        path.on_authorized(&s, now());
        path.take_session_duties();
        (path, s)
    }

    fn soc() -> PathEvent {
        PathEvent::StateOfCharge { percent: 42.0 }
    }

    /// The far side of a completed flip: presenting AC, the session back in
    /// `PrepareCharging` on nominal signalling, the link unmatched.
    fn flipped_to_ac() -> (AcWithSoc, Session) {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        path.on_timer(&s, TIMER_REINIT, now());
        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(!path.presents_fake_dc());
        (path, s)
    }

    fn arms(effects: &[Effect], timer: TimerId) -> bool {
        effects
            .iter()
            .any(|e| matches!(e, Effect::StartTimer { id, .. } if *id == timer))
    }

    fn cancels(effects: &[Effect], timer: TimerId) -> bool {
        effects
            .iter()
            .any(|e| matches!(e, Effect::CancelTimer { id } if *id == timer))
    }

    fn announced(path: &mut AcWithSoc) -> Vec<PresentedMode> {
        path.take_session_duties()
            .into_iter()
            .filter_map(|duty| match duty {
                SessionDuty::AnnounceMode(mode) => Some(mode),
                _ => None,
            })
            .collect()
    }

    // The mode, and where it is written.

    /// `EvseManager::init` seeds `fake_dc_enabled` in `init`, before the
    /// session setup at `:362` reads it, and `ready` then calls
    /// `setup_fake_DC_mode` instead of `Charger::setup`.
    #[test]
    fn the_port_comes_up_presenting_the_fake_dc_mode_and_says_so() {
        let mut path = AcWithSoc::new(config());

        assert!(
            path.presents_fake_dc(),
            "the mode is seeded before anything reads it"
        );
        path.on_startup();
        assert_eq!(announced(&mut path), vec![PresentedMode::Dc]);
    }

    /// The offer goes up **on the plug in**, before any authorization, and that
    /// is the whole point of it: `Charger::run_state_machine`'s `WaitingForAuthentication` entry raises it from the
    /// `WaitingForAuthentication` entry because five percent is what invites
    /// SLAC to start at all. An offer that waited for the authorization would
    /// leave a vehicle that needs it unable to match, and the mode would never
    /// see a state of charge.
    ///
    /// The session handed in asks for the nominal start, so this is the mode's
    /// own answer and not the setting's.
    #[test]
    fn plugging_a_vehicle_into_a_fake_dc_port_offers_five_percent_at_once() {
        let mut path = booted(config());
        let s = session();

        let effects = path.on_session_start(&s, now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
        assert!(
            effects.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "the offer that invites SLAC, got {effects:?}"
        );
    }

    /// And the AC side of a flip does not re-derive one.
    ///
    /// The route is the data link error, which is both a flip to AC and, on a
    /// running five percent offer, the `[V2G3-M07-05]` pilot detour back to
    /// `WaitingForAuthentication`. That state's entry re-derives the offer, and
    /// presenting AC means `ac_hlc_enabled_current_session` is false, so there
    /// is none to re-derive. An offer put back here would leave a port that has
    /// just given up on ISO 15118 still inviting it.
    #[test]
    fn a_restart_that_flips_to_ac_comes_back_without_a_five_percent_offer() {
        let (mut path, s) = presenting_dc(config());

        path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());
        assert!(!path.presents_fake_dc(), "the error is also the flip");

        path.on_timer(&s, crate::core::path::ac::TIMER_T_STEP, now());
        path.on_timer(&s, crate::core::path::ac::TIMER_T_STEP, now());
        let back = path.on_timer(&s, crate::core::path::ac::TIMER_T_STEP, now());

        assert!(
            !back.contains(&Effect::PwmOn(PWM_5_PERCENT)),
            "an AC presented restart offers no five percent, got {back:?}"
        );
        assert!(
            back.contains(&Effect::PwmOff),
            "the pilot comes back at a zero duty cycle first, got {back:?}"
        );
    }

    /// The fake DC session is the DC branch of the C++
    /// `WaitingForAuthentication` case: five percent whatever
    /// `ac_hlc_use_5percent` says, and straight to preparing to charge with no
    /// pilot detour.
    #[test]
    fn a_fake_dc_session_offers_five_percent_and_proceeds_without_a_detour() {
        let (mut path, s) = presenting_dc(config());

        assert_eq!(path.state(), AcState::PrepareCharging);
        let offer = path.on_limits_changed(&s, now());
        assert!(
            offer
                .iter()
                .any(|e| matches!(e, Effect::PwmOn(duty) if *duty == PWM_5_PERCENT)),
            "the offer stays at five percent, got {offer:?}"
        );
    }

    /// `EvseManager::switch_AC_mode`: the AC setup, then the reinit. The
    /// deadline that brings the port back to the fake DC mode is
    /// `Charger::setup`'s `_ac_with_soc_timeout`, which only `setup_AC_mode`
    /// passes true.
    #[test]
    fn the_state_of_charge_flips_to_ac_announces_it_and_arms_the_deadline() {
        let (mut path, s) = presenting_dc(config());

        let effects = path.on_path_event(&s, soc(), now());

        assert!(!path.presents_fake_dc(), "the mode flipped");
        assert_eq!(announced(&mut path), vec![PresentedMode::Ac]);
        assert!(arms(&effects, TIMER_DC_REFRESH), "got {effects:?}");
    }

    /// `Charger::process_pending_reinit_request` will not break the pilot while
    /// SLAC is matched: it asks the vehicle to end its ISO 15118 session and
    /// comes back when the link is down.
    #[test]
    fn the_flip_asks_a_matched_vehicle_to_stop_before_it_touches_the_pilot() {
        let (mut path, s) = presenting_dc(config());

        let effects = path.on_path_event(&s, soc(), now());

        assert!(
            effects.contains(&Effect::HlcUpdate(HlcUpdate::StopCharging(true))),
            "the vehicle is asked to end the session, got {effects:?}"
        );
        assert!(
            !effects.iter().any(|e| matches!(e, Effect::SetCpState(_)))
                && !arms(&effects, TIMER_REINIT),
            "the pilot is not touched while the link is matched, got {effects:?}"
        );
        assert_ne!(path.state(), AcState::Reinit);
    }

    #[test]
    fn the_link_going_unmatched_releases_the_reinit_onto_the_pilot() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());

        let effects = path.on_path_event(&s, PathEvent::SlacMatched(false), now());

        assert_eq!(path.state(), AcState::Reinit);
        assert!(
            effects.contains(&Effect::SetCpState(CpState::F)),
            "the configured level is signalled, got {effects:?}"
        );
        assert!(arms(&effects, TIMER_REINIT), "got {effects:?}");
    }

    /// The whole flip, end to end: the session comes back on nominal signalling
    /// in the same transaction.
    #[test]
    fn the_reinit_hands_the_same_session_back_as_a_basic_ac_one() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        path.take_session_duties();

        let back = path.on_timer(&s, TIMER_REINIT, now());

        assert_eq!(path.state(), AcState::PrepareCharging);
        assert!(
            back.contains(&Effect::SetCpState(CpState::X1)),
            "the hold ends on X1, got {back:?}"
        );
        assert!(
            back.iter()
                .any(|e| matches!(e, Effect::PwmOn(duty) if *duty > PWM_5_PERCENT)),
            "and the session resumes on nominal signalling, got {back:?}"
        );
        assert!(
            !path
                .take_session_duties()
                .iter()
                .any(|duty| matches!(duty, SessionDuty::StopTransaction | SessionDuty::EndSession)),
            "the billing record is not split in two"
        );
    }

    /// A vehicle already on the AC side of a flip is not announced twice.
    /// `present` is idempotent, which is what keeps the mode's writer single:
    /// every route into it may state its destination without first testing
    /// where the port is.
    #[test]
    fn a_second_state_of_charge_after_the_flip_announces_nothing_further() {
        let (mut path, s) = flipped_to_ac();
        path.take_session_duties();

        path.on_path_event(&s, soc(), now());

        assert_eq!(announced(&mut path), Vec::new());
    }

    /// It does reinitialize again, and that is the C++ behaviour rather than an
    /// oversight: `EvseManager::switch_AC_mode` calls `Charger::start_reinit`
    /// unconditionally, and the only guard on the far side is `reinit_running`,
    /// which a finished sequence has already cleared.
    ///
    /// It costs nothing in practice, because the reinitialization is what tore
    /// the ISO 15118 session down and a torn down session reports no further
    /// state of charge. Pinned rather than corrected: a port that swallowed the
    /// second report would leave a vehicle that somehow kept an ISO session
    /// alive charging as DC on AC hardware.
    #[test]
    fn a_second_state_of_charge_after_the_flip_reinitializes_again() {
        let (mut path, s) = flipped_to_ac();

        let effects = path.on_path_event(&s, soc(), now());

        assert_eq!(path.state(), AcState::Reinit);
        assert!(
            effects.contains(&Effect::SetCpState(CpState::F)),
            "got {effects:?}"
        );
    }

    /// `Charger::signal_ac_with_soc_timeout` is connected to `switch_DC_mode`
    /// and to nothing else, and `switch_DC_mode` is `setup_fake_DC_mode` alone:
    /// no reinit, no session movement.
    #[test]
    fn the_deadline_flips_back_to_the_fake_dc_mode_and_moves_no_session() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        path.on_timer(&s, TIMER_REINIT, now());
        path.take_session_duties();
        let before = path.state();

        let effects = path.on_timer(&s, TIMER_DC_REFRESH, now());

        assert!(path.presents_fake_dc());
        assert_eq!(announced(&mut path), vec![PresentedMode::Dc]);
        assert_eq!(path.state(), before, "the charge keeps running");
        assert!(
            !effects.iter().any(|e| matches!(e, Effect::SetCpState(_))),
            "the pilot is not touched, got {effects:?}"
        );
    }

    /// A deadline that fired once does not fire again: it is not re-armed while
    /// DC is presented, which is `setup_fake_DC_mode` passing
    /// `_ac_with_soc_timeout` false.
    #[test]
    fn the_deadline_does_not_run_while_the_fake_dc_mode_is_presented() {
        let (mut path, s) = presenting_dc(config());

        let effects = path.on_timer(&s, TIMER_DC_REFRESH, now());

        assert_eq!(effects, Vec::new(), "got {effects:?}");
        assert!(path.presents_fake_dc());
    }

    /// `EvseManager::ready`'s `if (config.ac_with_soc)` unplug arm: configure for the fake DC mode again so the
    /// next session starts where this one did.
    #[test]
    fn an_unplug_returns_the_port_to_the_fake_dc_mode_and_cancels_the_deadline() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.take_session_duties();

        let effects = path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(path.presents_fake_dc());
        assert_eq!(announced(&mut path), vec![PresentedMode::Dc]);
        assert!(cancels(&effects, TIMER_DC_REFRESH), "got {effects:?}");
    }

    /// A hold left armed would take the next vehicle's session out of
    /// `WaitingForAuthentication` with no authorization behind it.
    #[test]
    fn an_unplug_during_the_hold_cancels_it() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        assert_eq!(path.state(), AcState::Reinit);

        let effects = path.on_bsp(&s, &BspEvent::Cp(CpEvent::A), CpEdges::default(), now());

        assert!(cancels(&effects, TIMER_REINIT), "got {effects:?}");
        assert_eq!(path.state(), AcState::Idle);
    }

    /// `EvseManager::ready`'s `subscribe_dlink_error` arm. The flip runs before `charger->dlink_error()`
    /// and carries no reinit.
    #[test]
    fn a_data_link_error_in_the_fake_dc_mode_flips_to_ac_without_a_reinit() {
        let (mut path, s) = presenting_dc(config());

        let effects = path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

        assert!(!path.presents_fake_dc());
        assert_eq!(announced(&mut path), vec![PresentedMode::Ac]);
        assert!(arms(&effects, TIMER_DC_REFRESH), "got {effects:?}");
        assert!(
            !effects.contains(&Effect::HlcUpdate(HlcUpdate::StopCharging(true))),
            "no reinit travels with it, got {effects:?}"
        );
    }

    /// The other three data link requests are not flips. `EvseManager` installs
    /// the `setup_AC_mode` call in the error handler alone.
    #[test]
    fn the_other_data_link_requests_do_not_flip_the_mode() {
        for request in [DataLinkRequest::Pause, DataLinkRequest::Terminate] {
            let (mut path, s) = presenting_dc(config());

            path.on_path_event(&s, PathEvent::DataLink(request), now());

            assert!(path.presents_fake_dc(), "{request:?} must not flip");
            assert_eq!(announced(&mut path), Vec::new(), "{request:?}");
        }
    }

    /// `Charger::start_reinit` refuses the `CPStateE` method outright on a board
    /// that has not reported support, rather than substituting a level the
    /// operator did not configure. The flip still happens.
    #[test]
    fn a_state_e_reinit_is_refused_on_a_board_that_cannot_signal_it() {
        let mut path = booted(config_with(
            ReinitMethod::CpStateE,
            Duration::from_millis(3000),
        ));
        let s = session();
        path.on_bsp(&s, &caps(false), CpEdges::default(), now());
        path.on_session_start(&s, now());
        path.on_path_event(&s, PathEvent::MatchingStarted(true), now());
        path.on_path_event(&s, PathEvent::SlacMatched(true), now());
        path.on_authorized(&s, now());
        path.take_session_duties();

        let effects = path.on_path_event(&s, soc(), now());

        assert!(!path.presents_fake_dc(), "the flip still happens");
        assert!(
            !effects.contains(&Effect::HlcUpdate(HlcUpdate::StopCharging(true))),
            "and nothing is asked of the vehicle, got {effects:?}"
        );
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        assert_ne!(path.state(), AcState::Reinit);
    }

    /// The same method on a board that does report support.
    #[test]
    fn a_state_e_reinit_runs_where_the_board_reported_it_can() {
        let (mut path, s) = presenting_dc(config_with(
            ReinitMethod::CpStateE,
            Duration::from_millis(3000),
        ));
        path.on_path_event(&s, soc(), now());

        let effects = path.on_path_event(&s, PathEvent::SlacMatched(false), now());

        assert!(
            effects.contains(&Effect::SetCpState(CpState::E)),
            "got {effects:?}"
        );
    }

    /// `Charger::start_reinit`'s `reinit_running` guard.
    #[test]
    fn a_second_flip_does_not_restart_a_reinit_already_waiting() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());

        // A data link error arriving while the wait is outstanding is another
        // route into the flip, and it must not re-ask the vehicle.
        let again = path.on_path_event(&s, PathEvent::DataLink(DataLinkRequest::Error), now());

        assert!(
            !again.contains(&Effect::HlcUpdate(HlcUpdate::StopCharging(true))),
            "got {again:?}"
        );
    }

    /// A configured duration of zero holds nothing, so the session is back on
    /// nominal signalling in the call that started the reinit.
    #[test]
    fn a_zero_hold_returns_the_session_at_once() {
        let (mut path, s) = presenting_dc(config_with(ReinitMethod::CpStateF, Duration::ZERO));
        path.on_path_event(&s, soc(), now());

        path.on_path_event(&s, PathEvent::SlacMatched(false), now());

        assert_eq!(path.state(), AcState::WaitingForAuthentication);
    }

    /// A state of charge from a port with no vehicle on it. The mode still
    /// flips, because `EvseManager` flips it from the subscription with no
    /// state test at all, and the reinit is refused by the state.
    #[test]
    fn a_state_of_charge_with_no_session_flips_the_mode_and_reinitializes_nothing() {
        let mut path = booted(config());
        let s = session();
        path.take_session_duties();

        let effects = path.on_path_event(&s, soc(), now());

        assert!(!path.presents_fake_dc());
        assert_eq!(announced(&mut path), vec![PresentedMode::Ac]);
        assert_eq!(path.state(), AcState::Idle);
        assert!(
            !effects.iter().any(|e| matches!(e, Effect::SetCpState(_))),
            "an idle port's pilot is not broken, got {effects:?}"
        );
    }

    /// A port leaving service does not flip. `EvseManager` connects
    /// `switch_DC_mode` to the unplug and to the deadline, and to nothing else.
    #[test]
    fn a_disable_cancels_the_deadline_and_the_hold_without_flipping() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        path.take_session_duties();

        let effects = path.on_path_event(&s, PathEvent::Disable, now());

        assert!(!path.presents_fake_dc(), "the mode survives");
        assert_eq!(announced(&mut path), Vec::new());
        assert!(cancels(&effects, TIMER_DC_REFRESH), "got {effects:?}");
        assert!(cancels(&effects, TIMER_REINIT), "got {effects:?}");
    }

    /// Safe state stops the sequence with the energy, so nothing left over can
    /// put the pilot back where a fault took it from.
    #[test]
    fn safe_state_cancels_the_deadline_and_the_hold() {
        let (mut path, s) = presenting_dc(config());
        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());

        let effects = path.to_safe_state();

        assert!(cancels(&effects, TIMER_DC_REFRESH), "got {effects:?}");
        assert!(cancels(&effects, TIMER_REINIT), "got {effects:?}");
    }

    /// Cancelled exactly where it was armed. A cancel for a deadline nobody
    /// armed reads to whoever traces the effects as a deadline that existed.
    #[test]
    fn nothing_is_cancelled_that_was_never_armed() {
        let (mut path, s) = presenting_dc(config());

        let effects = path.on_path_event(&s, PathEvent::Disable, now());

        assert!(!cancels(&effects, TIMER_DC_REFRESH), "got {effects:?}");
        assert!(!cancels(&effects, TIMER_REINIT), "got {effects:?}");
    }

    /// The session events of the pass come before this path's own duty, so a
    /// consumer reading the sequence sees the session move and then the
    /// announcement rather than the announcement wrapped inside it.
    #[test]
    fn the_inner_paths_duties_are_reported_ahead_of_the_announcement() {
        let mut path = booted(config());
        let s = session();
        path.on_session_start(&s, now());
        path.on_path_event(&s, PathEvent::SlacMatched(true), now());
        path.on_authorized(&s, now());
        path.take_session_duties();

        path.on_path_event(&s, soc(), now());
        path.on_path_event(&s, PathEvent::SlacMatched(false), now());
        path.on_timer(&s, TIMER_REINIT, now());
        let duties = path.take_session_duties();

        let announce = duties
            .iter()
            .position(|d| matches!(d, SessionDuty::AnnounceMode(_)));
        let event = duties
            .iter()
            .position(|d| matches!(d, SessionDuty::Publish(SessionEvent::PrepareCharging)));
        if let (Some(announce), Some(event)) = (announce, event) {
            assert!(event < announce, "got {duties:?}");
        }
    }

    /// Every session the mode carries reports `fake_dc_enabled` as the mode it
    /// is actually presenting, which is the argument
    /// `call_session_setup` takes and the one the port used to pass as a
    /// constant false.
    #[test]
    fn the_session_setup_flag_follows_the_presented_mode() {
        let (mut path, s) = presenting_dc(config());
        assert!(path.presents_fake_dc());

        path.on_path_event(&s, soc(), now());
        assert!(!path.presents_fake_dc());

        path.on_timer(&s, TIMER_DC_REFRESH, now());
        assert!(path.presents_fake_dc());
    }

    /// The board capability report is the only writer of the control pilot
    /// state E fact, and the seed before one arrives is the false
    /// `EvseManager::init`, which seeds its own capability handle seeds its own handle with.
    #[test]
    fn the_control_pilot_state_e_fact_comes_from_the_board_and_starts_false() {
        let mut path = booted(config_with(
            ReinitMethod::CpStateE,
            Duration::from_millis(3000),
        ));
        let s = session();
        assert!(!path.supports_cp_state_e);

        path.on_bsp(&s, &caps(true), CpEdges::default(), now());
        assert!(path.supports_cp_state_e);

        path.on_bsp(&s, &caps(false), CpEdges::default(), now());
        assert!(!path.supports_cp_state_e, "a later report replaces it");
    }
}
