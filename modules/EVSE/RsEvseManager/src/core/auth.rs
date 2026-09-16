// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Authorization: whether this EVSE currently holds permission to charge, how
//! that permission was obtained, and what its arrival or loss asks for.
//!
//! A port of `Charger::authorize`, `Charger::deauthorize` and
//! `Charger::deauthorize_internal` at
//! `modules/EVSE/EvseManager/Charger.cpp:1624-1683`, plus the per state look at
//! `flag_authorized` that the C++ state machine performs at `Charger.cpp:690`,
//! `:781`, `:880`, `:942` and `:1042`.
//!
//! `Auth` performs no I/O, reads no clock, spawns nothing and locks nothing. The
//! authorization timeout is not a timer here, and the C++ has none either: it
//! reacts to the `withdraw_authorization` command the `Auth` module sends when
//! its own connection timeout expires (`evse/evse_managerImpl.cpp:457-464`). A
//! timeout is therefore a condition read off the state a withdraw arrives in,
//! and there is nothing to arm.

use super::path::iec::AcState;
use super::session::SessionPhase;
use super::token::IdTag;

/// How the permission was obtained. Derived per call from the token that
/// carried it, never from configuration: there is no `authorization_mode` config
/// key, and `Charger.cpp:1645-1646` reads the token's authorization type on
/// every accepted authorization.
///
/// Named separately from `token::AuthorizationType` because the distinction it
/// draws is this module's: five means of arrival collapse to two answers, and
/// which of the stack's two outstanding authorization requests a verdict
/// answers is the only question `hlc::authz` asks of it.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AuthorizationKind {
    /// External identification means, a swiped card or a remote start.
    Eim,
    /// Negotiated over ISO 15118. The C++ `authorized_pnc`.
    PlugAndCharge,
}

impl AuthorizationKind {
    /// The kind an identity declares, read off the token rather than computed
    /// beside it at the boundary. Two derivations of one input could disagree
    /// and nothing would say so.
    pub fn of(token: &IdTag) -> Self {
        if token.is_plug_and_charge() {
            Self::PlugAndCharge
        } else {
            Self::Eim
        }
    }
}

/// `types::authorization::AuthorizationStatus`, the verdict `Auth` returns.
///
/// Every value, because the module does not decide them: it originates two of
/// them and relays the rest to the vehicle unchanged
/// (`evse/evse_managerImpl.cpp:448-454`).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AuthorizationStatus {
    Accepted,
    Blocked,
    ConcurrentTx,
    Expired,
    Invalid,
    NoCredit,
    NotAllowedTypeEvse,
    NotAtThisLocation,
    NotAtThisTime,
    Unknown,
    PinRequired,
    Timeout,
}

/// `ValidationResult::tariff_messages`, the tariff the authorizing party said
/// this session would be billed under.
///
/// One message per language, in the order the validator sent them, and the
/// interface says what the order means: "The first message in this array shall
/// be used to start the transaction at the powermeter (for OCMF)."
/// `Charger::start_transaction` does exactly that, reading
/// `validation_result.tariff_messages.at(0).content` and leaving `tariff_text`
/// unset when the vector is empty.
///
/// Only the content is carried. The wire record's other two fields, `format`
/// and `language`, reach nothing: `tariff_text` on `powermeter.yaml` is a bare
/// string, so there is nowhere for either to go, and nothing in this module
/// selects by language - the C++ has a standing `TODO` to, and closing that
/// would have to widen this type rather than find a value it silently dropped.
/// What is carried is the validator's own text, verbatim; there is no source in
/// this module for a tariff and none is invented when the list is empty.
///
/// A type rather than a bare `Vec<String>` so that "the first one" is decided
/// in one place. Two readers picking a message out of the list independently is
/// how a metering record and a receipt end up disagreeing about the price.
#[derive(Clone, Debug, Default, Eq, PartialEq)]
pub struct TariffMessages {
    contents: Vec<String>,
}

impl TariffMessages {
    /// The one constructor: the contents of `tariff_messages`, in wire order.
    pub fn new(contents: Vec<String>) -> Self {
        Self { contents }
    }

    /// The message the metering transaction is opened under, or `None` when the
    /// authorization carried no tariff at all.
    ///
    /// The only reader. `Charger::start_transaction`'s `at(0)` behind its
    /// `empty()` guard, which is the same answer for a list of one and a list
    /// of several.
    pub fn text(&self) -> Option<&str> {
        self.contents.first().map(String::as_str)
    }
}

/// `types::authorization::CertificateStatus`, the contract certificate half of
/// the answer the vehicle gets.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum CertificateStatus {
    Accepted,
    SignatureError,
    CertificateExpired,
    CertificateRevoked,
    NoCertificateAvailable,
    CertChainError,
    ContractCancelled,
}

/// The facts outside authorization that decide whether an accepted
/// authorization is still wanted, and whether there is a session to act on.
///
/// Passed in rather than held, because none of the three is authorization's to
/// own: the session lifecycle owns the first two and availability arbitration
/// owns the third.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct AuthContext {
    /// `shared_context.session_active`.
    pub session_active: bool,
    /// `shared_context.flag_externally_cancelled`, armed by a stop transaction
    /// request (`Charger.cpp:1372`).
    pub externally_cancelled: bool,
    /// `shared_context.flag_disable_requested`.
    pub disable_requested: bool,
}

/// What an authorization change asks the rest of the module to do.
///
/// Signals rather than effects, so this module stays independent of the outbound
/// surface, the same shape `Faults` uses.
#[derive(Clone, Debug, Eq, PartialEq)]
pub enum AuthSignal {
    /// The authorization was the first user interaction, so the session starts
    /// here rather than at plug in (`Charger.cpp:1640-1642`).
    StartSession,
    Authorized,
    Deauthorized,
    StopSession,
    /// Publish `PluginTimeout`: an authorization was expected and none came.
    AuthorizationTimeout,
    /// Raise `evse_manager/MREC9AuthorizationTimeout`. Emitted only when the
    /// `raise_mrec9` setting is on (`Charger.cpp:1670-1672`).
    RaiseAuthorizationTimeout,
    /// Answer the vehicle so it escapes its authorization loop, which the C++
    /// does through `signal_hlc_plug_in_timeout` (`Charger.cpp:1674`).
    HlcAuthorizationTimeout,
    /// A reserved token was authorized and then withdrawn, so the reservation
    /// counts as consumed (`evse/evse_managerImpl.cpp:459-462`). The consumer
    /// applies this only when a reservation is actually held, which is the
    /// second half of the C++ conjunct.
    ReservationConsumed,
}

/// What a per state look at the authorization state asks for.
///
/// The C++ re-reads `flag_authorized` in every active charging state rather than
/// acting where the loss happens, so losing the authorization mid charge is a
/// poll and not a call. That is kept, because the states differ in what they do
/// about it: four route to stopping, one completes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum AuthPoll {
    Continue,
    /// Route to stopping rather than dropping power, ported from
    /// `Charger.cpp:690`, `:781`, `:880` and `:942`.
    Stop,
    /// The stopping route is already running, and losing the authorization there
    /// cannot be recovered from without a replug, so it completes
    /// (`Charger.cpp:1042-1043`).
    Finish,
}

#[derive(Debug, Default)]
pub struct Auth {
    raise_mrec9: bool,
    authorized: bool,
    /// Held rather than derived from `token`, because it outlives it by one
    /// statement: `deauthorize` lowers both, and reading it off an absent token
    /// would make an unauthorized port claim external identification.
    plug_and_charge: bool,
    /// The identity the authorization carried, whole. `Charger.cpp:1645-1646`
    /// reads its authorization type on every accepted authorization, which is
    /// where `plug_and_charge` comes from.
    token: Option<IdTag>,
    /// The tariff that arrived with the identity, `shared_context.validation_result`
    /// narrowed to the one field `Charger::start_transaction` reads off it.
    ///
    /// Beside the token because `Charger::authorize` assigns
    /// `shared_context.id_token` and `shared_context.validation_result` in two
    /// adjacent statements from one verdict, and because the tariff is only
    /// meaningful as the terms *that* identity was admitted under. Written and
    /// cleared wherever the token is, in the same statement, so a transaction
    /// cannot be billed under terms the token it names never carried.
    ///
    /// The C++ never clears its copy - neither `stop_session` nor
    /// `stop_transaction` touches it - and clearing here is not observable
    /// either way, because a metering transaction only opens out of
    /// `AuthSignal::Authorized` and `authorize` assigns this on the way to
    /// producing it. Cleared regardless, so that the invariant holds
    /// structurally rather than by that argument.
    tariff: TariffMessages,
    /// The identity a free charging deployment grants itself, or `None` where a
    /// vehicle has to present one. `config.disable_authentication` as the thing
    /// it produces rather than as a flag to test: the C++ reads the setting in
    /// one place and what it does there is build this token
    /// (`evse/evse_managerImpl.cpp:149-158`).
    free_service: Option<IdTag>,
}

impl Auth {
    pub fn new(raise_mrec9: bool) -> Self {
        Self {
            raise_mrec9,
            ..Default::default()
        }
    }

    /// `disable_authentication`. A deployment that charges for free admits
    /// every vehicle under one local identity, so it is handed that identity
    /// once at construction and nothing tests the setting again.
    pub fn free_charging(mut self) -> Self {
        self.free_service = Some(crate::core::token::free_service_token());
        self
    }

    /// The identity to grant on a plug in, on a port that grants one at all.
    pub fn free_service_token(&self) -> Option<IdTag> {
        self.free_service.clone()
    }

    pub fn authorized(&self) -> bool {
        self.authorized
    }

    /// `Charger::get_authorized_pnc` at `Charger.cpp:1589-1591`.
    pub fn authorized_plug_and_charge(&self) -> bool {
        self.authorized && self.plug_and_charge
    }

    /// `Charger::get_authorized_eim` at `Charger.cpp:1594-1596`.
    pub fn authorized_eim(&self) -> bool {
        self.authorized && !self.plug_and_charge
    }

    pub fn token(&self) -> Option<&IdTag> {
        self.token.as_ref()
    }

    /// The tariff the held authorization arrived with, empty when none is held.
    pub fn tariff(&self) -> &TariffMessages {
        &self.tariff
    }

    /// The session ended, so the next one starts expecting its own
    /// authorization. Ported from `Charger::stop_session` and
    /// `Charger::start_session`, which between them clear the same three fields
    /// (`Charger.cpp:1376-1381` and `:1392-1396`).
    ///
    /// A reset rather than three assignments, because a fresh session is exactly
    /// the initial state, so there is no field a later addition can forget.
    pub fn clear(&mut self) {
        *self = Self::new(self.raise_mrec9);
    }

    /// The transaction was cancelled from outside, so the permission goes with
    /// it. Ported from `Charger::stop_transaction` at `Charger.cpp:1373-1374`.
    ///
    pub fn revoke(&mut self) {
        self.authorized = false;
        self.plug_and_charge = false;
        self.token = None;
        self.tariff = TariffMessages::default();
    }

    /// An authorization was granted. Ported from the `a == true` branch of
    /// `Charger::authorize` at `Charger.cpp:1624-1651`.
    ///
    /// There is no refusal to take, because the C++ never takes one either:
    /// both production call sites pass a hardcoded `true`
    /// (`evse/evse_managerImpl.cpp:151` and `:436`), so the `a == false` branch
    /// that stops a live session is unreachable from this module. Carrying no
    /// verdict flag is what keeps it unreachable from this one, and what lets a
    /// refused authorization leave a live session charging until a later grant
    /// arrives.
    pub fn authorize(
        &mut self,
        token: IdTag,
        tariff: TariffMessages,
        context: AuthContext,
    ) -> Vec<AuthSignal> {
        if context.externally_cancelled || context.disable_requested {
            // Ported from `Charger.cpp:1629-1636`. Without this a delayed
            // verdict restores the permission and the state machine never
            // reaches Finished.
            return Vec::new();
        }

        let mut signals = Vec::new();
        if !context.session_active {
            // The authorization was the first user interaction, so the session
            // starts here (`Charger.cpp:1640-1642`).
            // Nothing is reset for it: every field a session start clears is
            // assigned just below.
            signals.push(AuthSignal::StartSession);
        }

        self.plug_and_charge = token.is_plug_and_charge();
        self.token = Some(token);
        // `Charger::authorize`'s two adjacent assignments (`Charger.cpp:1768-1769`).
        // Unconditional, so a verdict carrying no tariff replaces one that did
        // rather than letting the previous session's terms outlive it.
        self.tariff = tariff;
        self.authorized = true;
        signals.push(AuthSignal::Authorized);
        signals
    }

    /// Drop the authorization if it is not in use, ported from
    /// `Charger::deauthorize_internal` at `Charger.cpp:1660-1683`.
    pub fn deauthorize(&mut self, state: AcState, context: AuthContext) -> Vec<AuthSignal> {
        if !context.session_active {
            return Vec::new();
        }

        let mut signals = vec![AuthSignal::Deauthorized];
        if !matches!(
            state,
            AcState::Startup
                | AcState::Idle
                | AcState::Disabled
                | AcState::WaitingForAuthentication
        ) {
            // The authorization is in use. Removing it here would drop power
            // under the vehicle, so the per state poll routes to stopping
            // instead. The state guard is `Charger.cpp:1665`.
            return signals;
        }

        if !self.authorized {
            // `Charger::deauthorize_internal` reaches this branch on any
            // withdraw that finds no authorization, whether one was outstanding
            // or not, and reports a plug in timeout every time. A second
            // withdraw is a second timeout, not a suppressed one.
            signals.push(AuthSignal::AuthorizationTimeout);
            if self.raise_mrec9 {
                signals.push(AuthSignal::RaiseAuthorizationTimeout);
            }
            signals.push(AuthSignal::HlcAuthorizationTimeout);
            // No stop. `Charger.cpp:1675` returns with the session still
            // running, which is what lets a second withdraw arrive at all.
            return signals;
        }

        self.authorized = false;
        self.plug_and_charge = false;
        self.token = None;
        self.tariff = TariffMessages::default();
        signals.push(AuthSignal::StopSession);
        signals
    }

    /// The inbound `withdraw_authorization` command, ported from
    /// `evse_managerImpl::handle_withdraw_authorization` at
    /// `evse/evse_managerImpl.cpp:457-464`.
    pub fn withdraw(&mut self, state: AcState, context: AuthContext) -> Vec<AuthSignal> {
        let mut signals = Vec::new();
        if self.authorized_eim() {
            // The reserved token was authorized and is now being taken back, so
            // the reservation is spent whatever happens below.
            signals.push(AuthSignal::ReservationConsumed);
        }
        signals.extend(self.deauthorize(state, context));
        signals
    }

    /// The per state look at the authorization state.
    pub fn poll(&self, phase: SessionPhase) -> AuthPoll {
        if self.authorized {
            return AuthPoll::Continue;
        }

        match phase {
            SessionPhase::Authorized | SessionPhase::Charging => AuthPoll::Stop,
            SessionPhase::Stopping => AuthPoll::Finish,
            SessionPhase::Idle
            | SessionPhase::Disabled
            | SessionPhase::Reserved
            | SessionPhase::WaitingForAuthorization
            | SessionPhase::Finished => AuthPoll::Continue,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::token::id_tag_for_tests;

    fn in_session() -> AuthContext {
        AuthContext {
            session_active: true,
            externally_cancelled: false,
            disable_requested: false,
        }
    }

    fn idle() -> AuthContext {
        AuthContext::default()
    }

    /// An EVSE waiting for authentication with a session running, which is the
    /// state every timeout and every withdraw is observed in.
    fn waiting() -> Auth {
        Auth::new(true)
    }

    fn no_tariff() -> TariffMessages {
        TariffMessages::default()
    }

    fn eim(auth: &mut Auth, context: AuthContext) -> Vec<AuthSignal> {
        auth.authorize(id_tag_for_tests("TOKEN", false), no_tariff(), context)
    }

    #[test]
    fn an_accepted_authorization_is_recorded_with_its_token() {
        let mut auth = waiting();

        let signals = eim(&mut auth, in_session());

        assert_eq!(signals, vec![AuthSignal::Authorized]);
        assert!(auth.authorized());
        assert_eq!(auth.token().map(IdTag::value), Some("TOKEN"));
    }

    #[test]
    fn an_external_token_is_not_plug_and_charge() {
        let mut auth = waiting();

        eim(&mut auth, in_session());

        assert!(auth.authorized_eim());
        assert!(!auth.authorized_plug_and_charge());
    }

    #[test]
    fn a_token_carried_over_iso_is_plug_and_charge() {
        // The kind is read off the token on every call, so the same instance
        // reports differently for two successive authorizations.
        let mut auth = waiting();
        eim(&mut auth, in_session());
        assert!(auth.authorized_eim());

        auth.authorize(id_tag_for_tests("EMAID", true), no_tariff(), in_session());

        assert!(auth.authorized_plug_and_charge());
        assert!(!auth.authorized_eim());
    }

    #[test]
    fn an_authorization_arriving_after_an_external_cancellation_is_discarded() {
        let mut auth = waiting();

        let signals = eim(
            &mut auth,
            AuthContext {
                session_active: true,
                externally_cancelled: true,
                disable_requested: false,
            },
        );

        assert!(signals.is_empty(), "{signals:?}");
        assert!(
            !auth.authorized(),
            "a delayed authorization must not restore permission after a cancel"
        );
    }

    #[test]
    fn an_authorization_arriving_while_a_disable_is_requested_is_discarded() {
        let mut auth = waiting();

        let signals = eim(
            &mut auth,
            AuthContext {
                session_active: true,
                externally_cancelled: false,
                disable_requested: true,
            },
        );

        assert!(signals.is_empty(), "{signals:?}");
        assert!(!auth.authorized());
    }

    #[test]
    fn an_authorization_that_is_the_first_user_interaction_starts_the_session() {
        let mut auth = Auth::new(false);

        let signals = eim(&mut auth, idle());

        assert_eq!(
            signals,
            vec![AuthSignal::StartSession, AuthSignal::Authorized],
            "the session starts before the authorization is announced"
        );
        assert!(auth.authorized());
    }

    #[test]
    fn deauthorizing_a_held_authorization_stops_the_session() {
        let mut auth = waiting();
        eim(&mut auth, in_session());

        let signals = auth.deauthorize(AcState::WaitingForAuthentication, in_session());

        assert_eq!(
            signals,
            vec![AuthSignal::Deauthorized, AuthSignal::StopSession]
        );
        assert!(!auth.authorized());
    }

    /// The C++ value initializes `current_state`, so a port that has not yet
    /// reached its first `Idle` reads as `EvseState::Disabled` there and the
    /// authorization is removable. `AcState::Startup` is that state here.
    #[test]
    fn deauthorizing_acts_in_the_resting_states_too() {
        for state in [AcState::Startup, AcState::Idle, AcState::Disabled] {
            let mut auth = waiting();
            eim(&mut auth, in_session());

            let signals = auth.deauthorize(state, in_session());

            assert!(
                signals.contains(&AuthSignal::StopSession),
                "{state:?} is one of the states the authorization can be \
                 removed in, got {signals:?}"
            );
        }
    }

    /// The guard reads the charger state and not the session phase, which is
    /// the whole of cause B's second half: `SessionPhase` carries an
    /// `Authorized` value `EvseState` has none of, and an authorization that
    /// arrived before the vehicle sets exactly that value. Reading the phase
    /// refused the withdrawal in a window the C++ allows it in, so the
    /// connector never returned to available.
    #[test]
    fn deauthorizing_mid_charge_neither_drops_the_authorization_nor_stops() {
        // The state guard at `Charger.cpp:1665`. Losing the authorization while
        // charging is handled by the per state poll instead. Every state that
        // is not one of the three above, so a state added later has to be
        // placed on one side or the other rather than defaulting.
        for state in [
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::SwitchPhases,
            AcState::StoppingCharging,
            AcState::Reinit,
            AcState::Finished,
        ] {
            let mut auth = waiting();
            eim(&mut auth, in_session());

            let signals = auth.deauthorize(state, in_session());

            assert_eq!(
                signals,
                vec![AuthSignal::Deauthorized],
                "{state:?} announces but does not act"
            );
            assert!(auth.authorized(), "{state:?} keeps the authorization");
        }
    }

    #[test]
    fn deauthorizing_outside_a_session_reports_nothing() {
        let mut auth = Auth::new(true);

        let signals = auth.deauthorize(AcState::Idle, idle());

        assert!(signals.is_empty(), "{signals:?}");
    }

    #[test]
    fn a_withdraw_after_a_revoked_authorization_still_reports_a_timeout() {
        // An external cancel takes the permission down with the transaction, but
        // `Charger::deauthorize_internal` tests only `flag_authorized`, so the
        // withdraw that follows reaches the timeout branch like any other. The
        // report is not evidence that anything timed out. Matching it is.
        let mut auth = waiting();
        eim(&mut auth, in_session());

        auth.revoke();
        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(!auth.authorized());
        assert!(
            signals.contains(&AuthSignal::AuthorizationTimeout),
            "{signals:?}"
        );
    }

    #[test]
    fn a_withdraw_with_no_authorization_reports_an_authorization_timeout() {
        let mut auth = waiting();

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert_eq!(
            signals,
            vec![
                AuthSignal::Deauthorized,
                AuthSignal::AuthorizationTimeout,
                AuthSignal::RaiseAuthorizationTimeout,
                AuthSignal::HlcAuthorizationTimeout,
            ]
        );
    }

    #[test]
    fn an_authorization_timeout_raises_mrec9_only_when_it_is_configured() {
        let mut auth = Auth::new(false);

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(signals.contains(&AuthSignal::AuthorizationTimeout));
        assert!(
            !signals.contains(&AuthSignal::RaiseAuthorizationTimeout),
            "the error is gated on raise_mrec9, got {signals:?}"
        );
        assert!(
            signals.contains(&AuthSignal::HlcAuthorizationTimeout),
            "the vehicle is answered regardless of the error setting, got {signals:?}"
        );
    }

    #[test]
    fn a_timeout_does_not_end_the_session_by_itself() {
        // `Charger.cpp:1675` returns without calling `stop_session`, which is
        // exactly why a second withdraw can reach the same branch.
        let mut auth = waiting();

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(!signals.contains(&AuthSignal::StopSession), "{signals:?}");
    }

    #[test]
    fn two_consecutive_withdrawals_report_two_timeouts() {
        // `Charger::deauthorize_internal` tests only `flag_authorized`, and the
        // first withdraw leaves both the session and the state untouched, so the
        // second reaches the same branch and reports a second plug in timeout
        // that no timeout caused. The C++ arguably mislabels it; this module
        // reproduces it rather than correcting it.
        let mut auth = waiting();

        let first = auth.withdraw(AcState::WaitingForAuthentication, in_session());
        let second = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        let timeouts = [&first, &second]
            .iter()
            .flat_map(|signals| signals.iter())
            .filter(|signal| **signal == AuthSignal::AuthorizationTimeout)
            .count();
        assert_eq!(timeouts, 2, "first {first:?}, second {second:?}");
        assert!(
            second.contains(&AuthSignal::Deauthorized),
            "the announcement itself is not the divergence, got {second:?}"
        );
    }

    #[test]
    fn two_consecutive_withdrawals_raise_mrec9_twice() {
        // Only a deployment with raise_mrec9 on can observe the mislabel as an
        // error rather than as a session event, and the error is what an OCPP
        // backend acts on. It is raised once per withdraw, as in the C++.
        let mut auth = waiting();
        assert!(auth.raise_mrec9, "the error must be reachable at all");

        let first = auth.withdraw(AcState::WaitingForAuthentication, in_session());
        let second = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        let raises = [&first, &second]
            .iter()
            .flat_map(|signals| signals.iter())
            .filter(|signal| **signal == AuthSignal::RaiseAuthorizationTimeout)
            .count();
        assert_eq!(raises, 2, "first {first:?}, second {second:?}");
    }

    #[test]
    fn a_withdraw_after_a_granted_and_dropped_authorization_still_reports_one() {
        // The authorization arrived, so nothing timed out, and the C++ reports a
        // plug in timeout anyway: the branch is reached on `flag_authorized`
        // alone. Suppressing it here was the divergence.
        let mut auth = waiting();
        eim(&mut auth, in_session());
        auth.deauthorize(AcState::WaitingForAuthentication, in_session());

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(
            signals.contains(&AuthSignal::AuthorizationTimeout),
            "{signals:?}"
        );
    }

    #[test]
    fn the_next_session_expects_an_authorization_again() {
        let mut auth = waiting();
        auth.withdraw(AcState::WaitingForAuthentication, in_session());

        auth.clear();
        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(
            signals.contains(&AuthSignal::AuthorizationTimeout),
            "a fresh session expects its own authorization, got {signals:?}"
        );
    }

    #[test]
    fn withdrawing_an_external_authorization_consumes_a_reservation() {
        let mut auth = waiting();
        eim(&mut auth, in_session());

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert_eq!(signals.first(), Some(&AuthSignal::ReservationConsumed));
    }

    #[test]
    fn withdrawing_a_plug_and_charge_authorization_consumes_no_reservation() {
        let mut auth = waiting();
        auth.authorize(id_tag_for_tests("EMAID", true), no_tariff(), in_session());

        let signals = auth.withdraw(AcState::WaitingForAuthentication, in_session());

        assert!(
            !signals.contains(&AuthSignal::ReservationConsumed),
            "only an external identification consumes a reservation, got {signals:?}"
        );
    }

    #[test]
    fn an_accepted_authorization_records_the_tariff_that_came_with_it() {
        let mut auth = waiting();

        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec!["GBP 0.12/kWh, no idle fee".to_owned()]),
            in_session(),
        );

        assert_eq!(auth.tariff().text(), Some("GBP 0.12/kWh, no idle fee"));
    }

    #[test]
    fn several_tariff_messages_bill_under_the_first() {
        // The interface's own rule: "The first message in this array shall be
        // used to start the transaction at the powermeter (for OCMF)."
        let mut auth = waiting();

        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec![
                "EUR 0.30/kWh".to_owned(),
                "0,30 EUR/kWh".to_owned(),
                "0.30 EUR pro kWh".to_owned(),
            ]),
            in_session(),
        );

        assert_eq!(auth.tariff().text(), Some("EUR 0.30/kWh"));
    }

    #[test]
    fn an_authorization_with_no_tariff_names_none() {
        // Nothing is invented for it. A validator that sent no tariff messages
        // leaves `tariff_text` unset on the metering request, which is what
        // `Charger::start_transaction`'s `empty()` guard does.
        let mut auth = waiting();

        eim(&mut auth, in_session());

        assert_eq!(auth.tariff().text(), None);
    }

    #[test]
    fn a_verdict_carrying_no_tariff_replaces_one_that_did() {
        // Two authorizations in one session, the second free of charge. Without
        // the unconditional assignment the transaction the second opens would
        // be billed under the first one's terms.
        let mut auth = waiting();
        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]),
            in_session(),
        );

        auth.authorize(id_tag_for_tests("OTHER", false), no_tariff(), in_session());

        assert_eq!(auth.tariff().text(), None);
    }

    #[test]
    fn the_tariff_goes_with_the_token_on_every_route_that_drops_it() {
        // Three routes lower the identity, and each has to take the terms with
        // it: a tariff outliving the token it arrived with is a price no
        // presented credential agreed to.
        let tariff = || TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]);
        let tag = || id_tag_for_tests("TOKEN", false);

        let mut dropped = waiting();
        dropped.authorize(tag(), tariff(), in_session());
        dropped.deauthorize(AcState::WaitingForAuthentication, in_session());
        assert_eq!(dropped.token(), None);
        assert_eq!(dropped.tariff().text(), None, "deauthorize");

        let mut revoked = waiting();
        revoked.authorize(tag(), tariff(), in_session());
        revoked.revoke();
        assert_eq!(revoked.token(), None);
        assert_eq!(revoked.tariff().text(), None, "revoke");

        let mut cleared = waiting();
        cleared.authorize(tag(), tariff(), in_session());
        cleared.clear();
        assert_eq!(cleared.token(), None);
        assert_eq!(cleared.tariff().text(), None, "clear");
    }

    #[test]
    fn a_discarded_authorization_records_no_tariff_for_either_reason() {
        // A verdict discarded by the guard records nothing at all, terms
        // included: `Charger.cpp:1629-1636` returns before either assignment.
        // Both reasons, because they are two conditions on one guard and a
        // tariff written above it would leak on whichever was left undriven.
        for context in [
            AuthContext {
                session_active: true,
                externally_cancelled: true,
                disable_requested: false,
            },
            AuthContext {
                session_active: true,
                externally_cancelled: false,
                disable_requested: true,
            },
        ] {
            let mut auth = waiting();

            auth.authorize(
                id_tag_for_tests("TOKEN", false),
                TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]),
                context,
            );

            assert_eq!(auth.tariff().text(), None, "{context:?}");
            assert!(!auth.authorized(), "{context:?}");
        }
    }

    #[test]
    fn a_tariff_replaces_a_different_tariff() {
        // The ordinary re-validation. The dangerous direction is covered above;
        // this is the one a deployment actually sees when a price changes
        // mid-session.
        let mut auth = waiting();
        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec!["EUR 0.30/kWh".to_owned()]),
            in_session(),
        );

        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec!["EUR 0.45/kWh".to_owned()]),
            in_session(),
        );

        assert_eq!(auth.tariff().text(), Some("EUR 0.45/kWh"));
    }

    #[test]
    fn an_empty_tariff_message_is_carried_as_an_empty_text() {
        // `first()`, not a truthiness test: the guard the C++ has is on the
        // vector being empty and not on the content being blank.
        let mut auth = waiting();

        auth.authorize(
            id_tag_for_tests("TOKEN", false),
            TariffMessages::new(vec![String::new()]),
            in_session(),
        );

        assert_eq!(auth.tariff().text(), Some(""));
    }

    #[test]
    fn losing_the_authorization_stops_charging() {
        let auth = waiting();

        for phase in [SessionPhase::Authorized, SessionPhase::Charging] {
            assert_eq!(auth.poll(phase), AuthPoll::Stop, "{phase:?}");
        }
    }

    #[test]
    fn losing_the_authorization_while_stopping_finishes_the_session() {
        let auth = waiting();

        assert_eq!(auth.poll(SessionPhase::Stopping), AuthPoll::Finish);
    }

    #[test]
    fn a_state_that_is_not_charging_yet_waits_rather_than_stopping() {
        let auth = waiting();

        for phase in [
            SessionPhase::Idle,
            SessionPhase::Disabled,
            SessionPhase::Reserved,
            SessionPhase::WaitingForAuthorization,
            SessionPhase::Finished,
        ] {
            assert_eq!(auth.poll(phase), AuthPoll::Continue, "{phase:?}");
        }
    }

    #[test]
    fn a_held_authorization_lets_every_state_continue() {
        let mut auth = waiting();
        eim(&mut auth, in_session());

        for phase in [
            SessionPhase::Idle,
            SessionPhase::Disabled,
            SessionPhase::Reserved,
            SessionPhase::WaitingForAuthorization,
            SessionPhase::Authorized,
            SessionPhase::Charging,
            SessionPhase::Stopping,
            SessionPhase::Finished,
        ] {
            assert_eq!(auth.poll(phase), AuthPoll::Continue, "{phase:?}");
        }
    }
}
