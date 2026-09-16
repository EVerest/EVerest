// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Session lifecycle, which is mode independent and therefore lives above
//! `PowerPath`. The trait receives `&Session`; it does not own it.

use crate::core::auth::TariffMessages;
use crate::core::hlc::SelectedService;
use crate::core::token::IdTag;

/// What a stop asks the power path to do, which is a narrower question than why
/// the transaction ended.
///
/// Eight values against the wire's twenty three, because the paths differ on
/// two things only: whether the vehicle is named an error
/// (`Core::stop_error_for`) and, for the AC with SoC mode, whether the stop
/// came from the EVSE. Derived by `StopTransactionReason::narrow` and never
/// written beside a `StopTransactionReason`, so the two cannot disagree about
/// one stop.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StopReason {
    Local,
    /// Availability arbitration took this EVSE out of service mid session
    /// (`Charger.cpp:1765`).
    EvseDisabled,
    Remote,
    EvDisconnected,
    EmergencyStop,
    DeAuthorized,
    Error,
    PowerLoss,
}

/// `types::evse_manager::StopTransactionReason`, the reason a transaction ended
/// as a consumer reads it off `TransactionFinished.reason`.
///
/// Every value, because this module relays the reason rather than deciding it:
/// `evse_managerImpl::handle_stop_transaction` hands
/// `StopTransactionRequest::reason` to `Charger::cancel_transaction`, which
/// stores it verbatim in `shared_context.last_stop_transaction_reason`
/// (`Charger.cpp:1444`) and publishes that same value.
///
/// Carried whole rather than reconstructed from [`StopReason`]. The narrowing
/// is many to one - five wire values collapse to `Remote`, five to `Error` and
/// eight to `Local` - so a reason rebuilt from the narrow form reports `Local`
/// for a `SOCLimitReached` and `Remote` for a `HardReset`. `reason` is optional
/// on the wire and `OCPP201::process_transaction_finished` handles its absence
/// explicitly, so a plausible wrong value there is strictly worse than none.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StopTransactionReason {
    EmergencyStop,
    EvDisconnected,
    HardReset,
    Local,
    Other,
    PowerLoss,
    Reboot,
    Remote,
    SoftReset,
    UnlockCommand,
    DeAuthorized,
    EnergyLimitReached,
    GroundFault,
    LocalOutOfCredit,
    MasterPass,
    OvercurrentFault,
    PowerQuality,
    SocLimitReached,
    StoppedByEv,
    TimeLimitReached,
    Timeout,
    ReqEnergyTransferRejected,
    EvseDisabled,
}

impl StopTransactionReason {
    /// What this reason asks the power path to do.
    ///
    /// Lives here rather than at the boundary so that one stop yields one
    /// narrow reason from one place; it was a boundary function converting the
    /// wire enum straight to [`StopReason`], which is why the wire reason was
    /// unavailable to the payload at all.
    pub fn narrow(self) -> StopReason {
        match self {
            Self::EmergencyStop => StopReason::EmergencyStop,
            Self::EvDisconnected => StopReason::EvDisconnected,
            Self::DeAuthorized => StopReason::DeAuthorized,
            Self::PowerLoss => StopReason::PowerLoss,
            Self::EvseDisabled => StopReason::EvseDisabled,
            Self::Remote
            | Self::HardReset
            | Self::Reboot
            | Self::SoftReset
            | Self::MasterPass => StopReason::Remote,
            Self::GroundFault
            | Self::OvercurrentFault
            | Self::PowerQuality
            | Self::ReqEnergyTransferRejected
            | Self::Timeout => StopReason::Error,
            Self::Local
            | Self::Other
            | Self::UnlockCommand
            | Self::EnergyLimitReached
            | Self::LocalOutOfCredit
            | Self::SocLimitReached
            | Self::StoppedByEv
            | Self::TimeLimitReached => StopReason::Local,
        }
    }
}

/// Why a session started, ported from `Charger::start_session`
/// (`Charger.cpp:1376-1390`). It reaches the wire as
/// `SessionStarted.reason`, which `AuthHandler.cpp:830` reads with an
/// unconditional `.value()`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StartSessionReason {
    /// A vehicle was plugged in first (`Charger.cpp:1387`).
    EvConnected,
    /// An authorization was the first user interaction (`Charger.cpp:1384`).
    Authorized,
}

/// Why the EVSE is holding a charge, as
/// `types::evse_manager::PauseChargingEVSEReasonEnum` names it.
///
/// `Charger`'s `ChargingPausedEVSE` body builds the set on every pass and
/// announces it again whenever it changes (`Charger.cpp:1005-1030`). Declared
/// in that body's push order, because the set reaches the wire as a list and a
/// consumer that reads the first entry reads the same one either side.
///
/// Two of the three the C++ can name. Its first is `Error`, pushed when
/// `stop_charging_on_fatal_error_internal` answers true (`Charger.cpp:1007`),
/// and that predicate polls `shared_context.shutdown_type`, which this module
/// does not hold: a charging preventing fault reaches `FaultSignal` here and
/// the path leaves this state in the same pass, so a port resident in
/// `ChargingPausedEvse` never has a fatal error standing. The reason is
/// therefore unreachable rather than unported, and
/// `a_fatal_fault_leaves_the_paused_state_rather_than_naming_a_reason` in
/// `core` is what says so.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PauseReason {
    /// `Charger.cpp:1011-1013`: no budget a charge could run on.
    NoEnergy,
    /// `Charger.cpp:1015-1017`: `flag_paused_by_evse`, which is
    /// `Command::PauseCharging` still standing.
    UserPause,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SessionEvent {
    Authorized,
    Deauthorized,
    /// The availability announcements. They are the only two session events the
    /// wire attaches the deciding source to, which is why they are published
    /// through their own effect rather than through `PublishSessionEvent`
    /// (`evse/evse_managerImpl.cpp:334-341`).
    Enabled,
    Disabled,
    SessionStarted,
    /// A transaction was found in persistent storage at startup, so the
    /// previous run was interrupted while it was open.
    ///
    /// Announced before the recovery that closes it
    /// (`EvseManager.cpp:1478-1481`, ahead of the cleanup at `:1485`) and
    /// carrying the recovered uuid rather than the current session's, which is
    /// why it does not travel through the ordinary session event route.
    SessionResumed,
    AuthRequired,
    TransactionStarted,
    PrepareCharging,
    ChargingStarted,
    ChargingPausedEv,
    ChargingPausedEvse,
    /// The EVSE is taking the break a phase change needs
    /// (`Charger.cpp:583`).
    SwitchingPhases,
    StoppingCharging,
    /// `Charger::stop_transaction` raises this immediately before the
    /// transaction finished signal (`Charger.cpp:1548`), through
    /// `signal_simple_event` rather than through the transaction signal beside
    /// it, which is why it carries no payload and why the protocol is
    /// republished after it.
    ///
    /// `types/evse_manager.yaml` calls it "essentially the same as
    /// `TransactionFinished`, but emitted for clarity", and the API module's
    /// `SessionInfo` reads it to reach its finished state, so the pair is a
    /// sequence a consumer keys on rather than one event with a synonym.
    ///
    /// Not raised by the startup recovery. `cleanup_transactions_on_startup`
    /// signals the transaction finished event alone (`Charger.cpp:1577`), so a
    /// recovered transaction closes without this.
    ChargingFinished,
    TransactionFinished,
    SessionFinished,
    ReservationStart,
    ReservationEnd,
    /// A vehicle is connected but no authorization arrived in time.
    PluginTimeout,
}

/// Which tier of the identity an `enable_disable` or `force_unlock` request
/// addresses. The `evse_manager` interface carries this as an integer where
/// zero means the whole EVSE and any other value a connector, which is the
/// magic zero this replaces.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EnableScope {
    Evse,
    Connector,
}

impl EnableScope {
    /// Reads the interface integer. Every non-zero value is a connector,
    /// including negative ones, because the wire type is signed and the C++
    /// tests nothing but inequality with zero.
    pub fn from_wire(connector_id: i64) -> Self {
        if connector_id == 0 {
            Self::Evse
        } else {
            Self::Connector
        }
    }

    /// True where the request updates connector enablement. Only a connector
    /// scope does, which is the asymmetry the C++ has: a disable takes effect
    /// regardless of scope, an enable only restores connector state for a
    /// connector scope.
    pub fn updates_connector_state(self) -> bool {
        matches!(self, Self::Connector)
    }
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Limits {
    pub max_current_a: f64,
    pub nr_of_phases_available: i64,
}

/// Energy transfer modes as negotiated over ISO 15118. The advertised set is
/// derived from configuration and live hardware capability; the selected value
/// is per session.
///
/// Every value of `types::iso15118::EnergyTransferMode` is here, not only the
/// ones this module derives, so the conversion at the boundary is total in both
/// directions. That is load bearing rather than tidy:
/// `update_allowed_energy_transfer_modes` hands the module an arbitrary list
/// from another module and the port relays it, so a mode with no local spelling
/// would be silently dropped from a list the vehicle is then offered. The first
/// four AC and DC groups follow `types/iso15118.yaml:14-32`, and the comment
/// there records which apply to DIN 70121 and ISO 15118-2 and which to
/// ISO 15118-20.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EnergyTransferMode {
    AcSinglePhase,
    AcTwoPhase,
    AcThreePhase,
    DcCore,
    DcExtended,
    DcComboCore,
    DcUnique,
    /// The ISO 15118-20 spelling of DC, distinct from `DcExtended`.
    Dc,
    AcBpt,
    AcBptDer,
    /// Advertised when a DER controller has declared itself available and the
    /// hardware can export. The availability half arrives as the
    /// `set_der_available` command, which is not ported yet, so nothing turns
    /// it on in production.
    AcDerIec,
    AcDerSae,
    DcBpt,
    DcAcdp,
    DcAcdpBpt,
    Wpt,
    Mcs,
    McsBpt,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PwmStart {
    /// Nominal duty cycle straight away.
    Nominal,
    /// Five percent duty cycle, falling back to nominal on timeout.
    FivePercent,
    /// Five percent that never falls back. Not IEC compliant, opt in only.
    FivePercentEnforced,
}

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct DerParams {
    pub max_reactive_power_var: f64,
}

/// Facts negotiated or discovered while a session is live.
///
/// These are data rather than types because they change during a session: the
/// power supply can lose its bidirectional capability through derating, and DER
/// availability arrives as an inbound command.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SessionProfile {
    pub transfer: Option<EnergyTransferMode>,
    pub bidirectional: bool,
    pub der: Option<DerParams>,
    pub pwm_start: PwmStart,
}

impl SessionProfile {
    /// `bidirectional` has three independent sources: the
    /// `hack_allow_bpt_with_iso2` config key, the SAE J2847/2 bidirectional
    /// session flag, and the ISO 15118-20 service the vehicle selected. The
    /// C++ recomputes a disjunction over them at three call sites, and the
    /// three are not the same expression; it is computed once here, which is
    /// what ADR-0018 decides.
    ///
    /// The selected service arrives as a `SelectedService`, the wire
    /// `ServiceCategory` the C++ member `selected_d20_energy_service` holds. It
    /// is deliberately not the advertised set's `EnergyTransferMode`: the two
    /// enums are near parallel and a resolution reading the advertised type
    /// would be asking what this EVSE offers rather than what the vehicle
    /// chose.
    ///
    /// The withdrawal veto ADR-0018 also decides is not here, because it is not
    /// a source: see `hlc::bpt::Bpt::bidirectional`, which is the only caller.
    pub fn resolve_bidirectional(
        allow_bpt_with_iso2: bool,
        sae_bidi_active: bool,
        selected: Option<SelectedService>,
    ) -> bool {
        allow_bpt_with_iso2
            || sae_bidi_active
            || selected.is_some_and(|service| service.is_bidirectional())
    }

    pub fn new(pwm_start: PwmStart) -> Self {
        Self {
            transfer: None,
            bidirectional: false,
            der: None,
            pwm_start,
        }
    }
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum SessionPhase {
    Idle,
    /// Availability arbitration says this EVSE is unavailable. `EvseState::Disabled`
    /// in the C++, which Idle enters directly on a winning disable
    /// (`Charger.cpp:236-238`).
    Disabled,
    Reserved,
    WaitingForAuthorization,
    Authorized,
    Charging,
    Stopping,
    Finished,
}

#[derive(Clone, Debug)]
pub struct Session {
    /// `shared_context.session_uuid`. Minted at every session start
    /// (`Charger.cpp:1381`) and released when the session ends
    /// (`Charger.cpp:1399`). The same string is the powermeter transaction id
    /// (`Charger.cpp:1410`), so the transaction lifecycle owner reads this
    /// rather than minting a second identity.
    pub id: Option<String>,
    /// `shared_context.last_start_session_reason`. Held rather than passed,
    /// because the start event is published from one site that reads it, which
    /// is what makes the reason impossible to omit. Overwritten at every start;
    /// the initial value matches the C++ value initialized enum.
    pub last_start_reason: StartSessionReason,
    pub phase: SessionPhase,
    /// `shared_context.session_active`. Tracked rather than derived from the
    /// phase, because a session can be active while the phase is still Idle: an
    /// authorization that is the first user interaction starts the session
    /// before the vehicle is connected (`Charger.cpp:1640-1642`).
    pub session_active: bool,
    /// `shared_context.flag_externally_cancelled`. A stop transaction request
    /// arms it (`Charger.cpp:1372`) and a session start or stop clears it
    /// (`Charger.cpp:1379`, `:1394`). It outlives the stopping route on purpose:
    /// a verdict that arrives late must still be discarded.
    pub externally_cancelled: bool,
    pub profile: SessionProfile,
    pub limits: Limits,
    pub transaction_active: bool,
    /// `shared_context.id_token`, the identity the accepted authorization
    /// carried. Whole, because it reaches the wire whole on three session
    /// events; see [`IdTag`].
    pub authorized_token: Option<IdTag>,
    /// Whether the authorization currently held came over ISO 15118. The C++
    /// `authorized_pnc`. `Auth` is the writer; this is the read model the power
    /// path sees, the same way `limits` is.
    pub authorized_plug_and_charge: bool,
    /// `shared_context.validation_result`, narrowed to the tariff the metering
    /// start is opened under. `Auth` is the writer, through
    /// `Core::mirror_authorization`, which is the only code that assigns any of
    /// these three.
    pub authorized_tariff: TariffMessages,
    /// `shared_context.flag_paused_by_evse`. `Charger::pause_charging`
    /// (`Charger.cpp:1331-1336`) raises it on a live transaction and
    /// `Charger::resume_charging` (`:1339-1345`) lowers it; `Charger::stop_session`
    /// (`:1397`) clears it with the rest of the session.
    ///
    /// Held here rather than on a power path because it is one flag on the
    /// shared state machine that arms both charge modes: the `Charging` arm
    /// leaves for `StoppingCharging` on it (`:782-786`), the
    /// `ChargingPausedEVSE` pass reports it as a reason (`:975-977`) and the
    /// stopping entry reads it to choose between asking the vehicle to pause
    /// and asking it to stop (`:1015`). Only the last of those three is ported.
    pub paused_by_evse: bool,
    /// `shared_context.hlc_d20_active`. The vehicle selected a service in an
    /// ISO 15118-20 `ServiceSelectionReq`, which is the only thing that can
    /// make it true: `Charger::set_hlc_d20_active` (`Charger.cpp:2135-2136`) is
    /// called from `subscribe_selected_service_parameters` alone
    /// (`EvseManager.cpp:961-965`), and the `Idle` entry (`:229`) is the other
    /// writer and clears it.
    ///
    /// The sibling fact that handler records, the selected service itself,
    /// lives on `HlcPort` because its consumers are the two AC limit
    /// emissions and it is forgotten again on a `D-LINK_TERMINATE`
    /// (`EvseManager.cpp:388`). This one outlives that and dies with the
    /// session, so the two are not one field.
    pub iso15118_20_active: bool,
    pub reservation_id: Option<i64>,
    /// `shared_context.last_stop_transaction_reason`, the reason the next
    /// `TransactionFinished` will name.
    ///
    /// An `Option` because absence has its own meaning: `Charger.cpp:1523-1526`
    /// reads an unset reason as `EVDisconnected`, which is the ordinary unplug
    /// nothing else claimed. Only three sites in the C++ set it -
    /// `Charger::cancel_transaction` from the request, `Charger::enable_disable`
    /// on a disable, and the reset at the top of `Charger::start_transaction` -
    /// so it is deliberately not this module's `StopReason`, which every stop
    /// route carries.
    pub stop_reason: Option<StopTransactionReason>,
    /// `shared_context.stop_transaction_id_token`, "only set in case transaction
    /// was stopped locally" (`Charger.hpp:335`). Set and cleared beside
    /// `stop_reason` by the same three sites.
    pub stop_token: Option<IdTag>,
}

impl Session {
    pub fn new(pwm_start: PwmStart, limits: Limits) -> Self {
        Self {
            id: None,
            last_start_reason: StartSessionReason::EvConnected,
            phase: SessionPhase::Idle,
            session_active: false,
            externally_cancelled: false,
            profile: SessionProfile::new(pwm_start),
            limits,
            transaction_active: false,
            authorized_token: None,
            authorized_plug_and_charge: false,
            authorized_tariff: TariffMessages::default(),
            paused_by_evse: false,
            iso15118_20_active: false,
            reservation_id: None,
            stop_reason: None,
            stop_token: None,
        }
    }

    /// Clear the reservation and answer what it was, the only code that lowers
    /// `reservation_id`.
    ///
    /// `EvseManager::cancel_reservation`, minus its announcement: that argument
    /// is the whole reason the C++ has one function for three callers, and it
    /// stays with the callers here because two of them announce
    /// `ReservationEnd` and the one a starting transaction reaches does not.
    /// Its comment says why - "this allows OCPP1.6 to not move back to
    /// available".
    pub fn take_reservation(&mut self) -> Option<i64> {
        self.reservation_id.take()
    }

    /// Set the reason and token the next `TransactionFinished` will name,
    /// `Charger.cpp:1444-1445` and `:1897`.
    pub fn record_stop(&mut self, reason: StopTransactionReason, token: Option<IdTag>) {
        self.stop_reason = Some(reason);
        self.stop_token = token;
    }

    /// Forget a recorded reason, the only code that lowers the pair.
    ///
    /// `Charger::start_transaction` resets both at its first two statements
    /// (`Charger.cpp:1478-1479`), and that position is load bearing rather than
    /// tidy: `Charger::enable_disable` records `EVSEDisabled` with no regard for
    /// whether a transaction is running (`:1897`), so a disable on an idle port
    /// leaves a reason behind that the next transaction would otherwise end
    /// under.
    pub fn clear_stop(&mut self) {
        self.stop_reason = None;
        self.stop_token = None;
    }

    /// The reason and token this transaction ends under, with an unset reason
    /// read as `EVDisconnected`.
    ///
    /// The default is `Charger.cpp:1523-1526` and its comment: "if the stop
    /// transaction reason was already set (e.g. by cancel_transaction), we keep
    /// it, else we know it is EVDisconnected". A read rather than a take,
    /// because the reset above is the one clear; `Core::stop_transaction` is
    /// guarded on `transaction_active` and so reads this at most once per
    /// transaction.
    pub fn stop_named(&self) -> (StopTransactionReason, Option<IdTag>) {
        (
            self.stop_reason
                .unwrap_or(StopTransactionReason::EvDisconnected),
            self.stop_token.clone(),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bidirectional_resolves_from_any_of_its_three_sources() {
        assert!(SessionProfile::resolve_bidirectional(true, false, None));
        assert!(SessionProfile::resolve_bidirectional(false, true, None));
        assert!(SessionProfile::resolve_bidirectional(
            false,
            false,
            Some(SelectedService::DcBpt)
        ));
        assert!(!SessionProfile::resolve_bidirectional(
            false,
            false,
            Some(SelectedService::Dc)
        ));
        assert!(!SessionProfile::resolve_bidirectional(false, false, None));
    }

    #[test]
    fn a_zero_on_the_wire_scopes_to_the_evse() {
        assert_eq!(EnableScope::from_wire(0), EnableScope::Evse);
    }

    #[test]
    fn any_non_zero_wire_value_scopes_to_a_connector() {
        assert_eq!(EnableScope::from_wire(1), EnableScope::Connector);
        assert_eq!(EnableScope::from_wire(7), EnableScope::Connector);
        assert_eq!(EnableScope::from_wire(-1), EnableScope::Connector);
    }

    #[test]
    fn only_a_connector_scope_updates_connector_state() {
        assert!(EnableScope::Connector.updates_connector_state());
        assert!(!EnableScope::Evse.updates_connector_state());
    }
}
