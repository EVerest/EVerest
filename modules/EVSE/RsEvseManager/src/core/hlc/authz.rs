// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The authorization bridge between the ISO 15118 stack and this module's own
//! authorization state.
//!
//! The stack asks for an authorization and then waits. Nothing else in the
//! module knows that it is waiting, so the answer cannot be derived from the
//! authorization state alone: a permission that arrives reaches the vehicle
//! only if the vehicle asked for one, and a permission that arrives twice must
//! not be announced twice. `Authz` holds exactly that missing fact, the two
//! waiting flags `hlc_waiting_for_auth_eim` and `hlc_waiting_for_auth_pnc`
//! (`EvseManager.hpp:336-337`), and every decision the C++ makes off them: the
//! two request handlers (`EvseManager.cpp:998-1014` and `:1030-1045`), the
//! response gate `EvseManager::charger_was_authorized` (`:1891-1908`), and the
//! two verdict routing rules in `evse_managerImpl::handle_authorize_response`
//! (`evse/evse_managerImpl.cpp:424-455`).

use crate::core::auth::{AuthorizationKind, AuthorizationStatus, CertificateStatus};
use crate::core::effect::{Effect, HlcUpdate};
use crate::core::hlc::setup::HlcConfig;

/// The answer the vehicle gets, the two arguments of
/// `call_authorization_response`.
///
/// A named struct rather than a pair, because the generated
/// `authorization_response` takes two adjacent enum arguments and the two are
/// interchangeable to the compiler. Naming them here is what lets the boundary
/// fill them by name.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct AuthorizationResponse {
    pub status: AuthorizationStatus,
    pub certificate: CertificateStatus,
}

impl AuthorizationResponse {
    /// `EvseManager.cpp:1894-1895`. A plug and charge grant reports the
    /// contract certificate as accepted, because one took part.
    pub const GRANTED_PLUG_AND_CHARGE: Self = Self {
        status: AuthorizationStatus::Accepted,
        certificate: CertificateStatus::Accepted,
    };

    /// `EvseManager.cpp:1004-1005` and `:1902-1903`. An external identification
    /// grant reports no certificate, because none took part.
    pub const GRANTED_EIM: Self = Self {
        status: AuthorizationStatus::Accepted,
        certificate: CertificateStatus::NoCertificateAvailable,
    };

    /// `EvseManager.cpp:415-416`, reached from `Charger::deauthorize_internal`
    /// (`Charger.cpp:1673-1674`). Neither a grant nor a refusal: an unknown
    /// status is the one answer that breaks the vehicle out of its authorize
    /// loop when no authorization arrived at all, which is the only reason this
    /// response exists.
    pub const TIMED_OUT: Self = Self {
        status: AuthorizationStatus::Unknown,
        certificate: CertificateStatus::NoCertificateAvailable,
    };
}

/// A `types::authorization::ProvidedIdToken` this module relays without
/// reading it.
///
/// The plug and charge handler republishes the token the stack handed over,
/// changing one field (`EvseManager.cpp:1031-1035`). It reads none of the rest,
/// and the rest is the contract certificate and its OCSP hash data, so
/// modelling it here would put four more wire types in `core` with no decision
/// in any of them. The payload travels as the serialized wire value instead,
/// which is lossless in both directions and cannot silently drop a field the
/// interface gains later.
#[derive(Clone, Debug, PartialEq)]
pub struct OpaqueToken(serde_json::Value);

impl OpaqueToken {
    pub fn new(payload: serde_json::Value) -> Self {
        Self(payload)
    }

    pub fn payload(&self) -> &serde_json::Value {
        &self.0
    }
}

/// A token this module publishes on its own `token_provider` interface.
#[derive(Clone, Debug, PartialEq)]
pub enum ProvidedToken {
    /// The autocharge token (`create_autocharge_token`,
    /// `EvseManager.cpp:42-49`). `id_token` is the whole identity string; the
    /// boundary adds the two constants the C++ sets beside it, the
    /// `MacAddress` token type and the `Autocharge` authorization type.
    Autocharge {
        id_token: String,
        connectors: Vec<i64>,
    },
    /// The plug and charge token, relayed with the connector list replaced
    /// (`EvseManager.cpp:1031-1035`).
    PlugAndCharge {
        token: OpaqueToken,
        connectors: Vec<i64>,
    },
}

/// The identity half of `create_autocharge_token` (`EvseManager.cpp:45-46`).
///
/// The colons come out of the MAC address before the prefix goes on, so
/// `AA:BB:CC:DD:EE:FF` and `AABBCCDDEEFF` name the same vehicle.
pub fn autocharge_id_token(evcc_id: &str) -> String {
    format!("VID:{}", evcc_id.replace(':', ""))
}

/// What the two handlers and the response gate read off the charger.
///
/// Passed in rather than held, for the reason `AuthContext` is: none of it is
/// the bridge's to own. The C++ reads it through `Charger::get_authorized_pnc`,
/// `get_authorized_eim` and `get_authorized_eim_ready_for_hlc`.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct AuthorizationHeld {
    /// `Charger::get_authorized_eim` (`Charger.cpp:1594-1597`). Mutually
    /// exclusive with `pnc`: both read the one permission and disagree only on
    /// how it was obtained.
    pub eim: bool,
    /// `Charger::get_authorized_pnc` (`Charger.cpp:1589-1592`).
    pub pnc: bool,
    /// The `ready` half of `Charger::get_authorized_eim_ready_for_hlc`
    /// (`Charger.cpp:1741-1750`), which holds in `Charging` and in the two
    /// paused states.
    ///
    /// It is the **charger's state**, so `Core` derives it from the power
    /// path's. It was read off `SessionPhase::Charging` here, which is a phase
    /// production never assigns: the phase goes `WaitingForAuthorization`,
    /// `Authorized`, `Stopping`, `Finished`, `Idle` and never that, so the
    /// answer was always false and the debug arm below could not be satisfied
    /// by a charging session.
    pub charging: bool,
}

/// How a verdict from `Auth` is routed
/// (`evse/evse_managerImpl.cpp:424-455`).
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Route {
    /// Hand the permission to the authorization state machine, then re-examine
    /// the response gate. A grant is the only verdict routed here.
    Grant,
    /// Drop it. An accepted authorization that is not plug and charge, arriving
    /// while the vehicle is waiting for plug and charge, has no effect
    /// (`:430-434`).
    Ignore,
    /// A refusal, which reaches the charger in neither form: `Charger::authorize`
    /// is called from the accepted branch alone (`:436`), so a refusal leaves a
    /// live session charging and a later grant can still arrive and be honored.
    ///
    /// The payload is the answer for the vehicle, which a plug and charge
    /// refusal carries and an external identification refusal does not
    /// (`:448-454`). Only the contract is forwarded, because a later external
    /// identification grant is still possible and a vehicle told it was refused
    /// would have stopped waiting for one.
    Refused(Option<AuthorizationResponse>),
}

/// A verdict as it arrived from `Auth`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct Verdict {
    pub kind: AuthorizationKind,
    pub status: AuthorizationStatus,
    /// Optional on the wire. The C++ substitutes `Accepted` for an absent one
    /// (`evse/evse_managerImpl.cpp:452-453`).
    pub certificate: Option<CertificateStatus>,
}

/// The two waiting flags and the decisions made off them.
#[derive(Debug, Default)]
pub struct Authz {
    /// `hlc_waiting_for_auth_eim`.
    waiting_eim: bool,
    /// `hlc_waiting_for_auth_pnc`.
    ///
    /// The C++ exposes it through a getter, `get_hlc_waiting_for_auth_pnc`
    /// (`EvseManager.cpp:1766-1768`), because the one rule that reads it lives
    /// in another class. `route` below is a method on this struct, so the port
    /// needs no equivalent and has none: an accessor with no production caller
    /// is surface, and the flag is observable through the decision it makes.
    waiting_pnc: bool,
}

impl Authz {
    /// Both flags down.
    ///
    /// The C++ clears them in three places and clears both in all three:
    /// unconditionally when a vehicle arrives (`EvseManager.cpp:1127-1130`), on
    /// unplug inside the SLAC arm (`:1099-1100`), and in whichever condition of
    /// the response gate held. The arrival is the primary reset; the unplug one
    /// is reachable only where SLAC is wired, which is implied anywhere these
    /// flags can be set at all, so both are ported and neither needs a gate of
    /// its own.
    pub fn clear(&mut self) {
        self.waiting_eim = false;
        self.waiting_pnc = false;
    }

    /// `subscribe_require_auth_eim` (`EvseManager.cpp:998-1014`).
    ///
    /// `autocharge_id_token` is the identity the stack reported earlier, which
    /// the port derives once and holds. Absent until the vehicle has named
    /// itself.
    pub fn on_require_eim(
        &mut self,
        config: &HlcConfig,
        held: AuthorizationHeld,
        autocharge_id_token: Option<&str>,
    ) -> Vec<Effect> {
        let already_authorized = if config.dbg_auth_after_tstep {
            held.eim && held.charging
        } else {
            held.eim
        };
        if already_authorized {
            // A late request, with charging already running. The vehicle is
            // answered at once and nothing is left waiting.
            self.clear();
            return vec![Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                AuthorizationResponse::GRANTED_EIM,
            ))];
        }

        let mut effects = Vec::new();
        if config.enable_autocharge {
            match autocharge_id_token {
                Some(id_token) => {
                    effects.push(Effect::PublishProvidedToken(ProvidedToken::Autocharge {
                        id_token: id_token.to_owned(),
                        connectors: config.connectors.clone(),
                    }))
                }
                // The C++ publishes its `autocharge_token` member unguarded,
                // and that member is only ever filled from the stack's own
                // `evcc_id` report (`EvseManager.cpp:1016-1018`). With the
                // identity taken from SLAC instead the member stays value
                // initialized, so the C++ offers a token with an empty identity
                // and an authorization type nobody chose. That is a defect
                // rather than a decision, so nothing is offered here and the
                // gap names itself.
                None => log::warn!(
                    "autocharge is enabled but no vehicle identity has arrived, no token offered"
                ),
            }
        }
        self.waiting_eim = true;
        self.waiting_pnc = false;
        effects
    }

    /// `subscribe_require_auth_pnc` (`EvseManager.cpp:1030-1045`).
    pub fn on_require_plug_and_charge(
        &mut self,
        config: &HlcConfig,
        held: AuthorizationHeld,
        token: OpaqueToken,
    ) -> Vec<Effect> {
        if held.pnc {
            // Nothing is offered and nothing is answered. The C++ states the
            // reason for the first half at `EvseManager.cpp:1040`: only publish
            // the token if we are not authorized yet, which is what enables
            // pause and resume with plug and charge. A republished token would
            // be validated a second time and the resumed session would be a new
            // one.
            self.clear();
            return Vec::new();
        }
        self.waiting_eim = false;
        self.waiting_pnc = true;
        vec![Effect::PublishProvidedToken(ProvidedToken::PlugAndCharge {
            token,
            connectors: config.connectors.clone(),
        })]
    }

    /// `EvseManager::charger_was_authorized` (`EvseManager.cpp:1891-1908`).
    ///
    /// Two independent conditions rather than a branch, which is what the C++
    /// writes. Each answers the vehicle and clears both flags, so a plug and
    /// charge grant that holds would suppress the external identification one
    /// in the same pass. Neither holding means nothing is sent and nothing is
    /// cleared: the vehicle asked for one kind of authorization, and a
    /// different one arriving is not its answer.
    ///
    /// The suppression is unreachable and the shape is kept for fidelity
    /// alone. Both handlers set one flag and clear the other, so at most one is
    /// ever up, and `eim` and `pnc` read the one permission and disagree only
    /// on how it was obtained, so at most one of those is ever true either.
    /// Nothing observable therefore distinguishes this from an if/else with
    /// the conditions in either order, each clearing only its own flag; the
    /// tests below pin the exclusion that makes that so, including against an
    /// `AuthorizationHeld` carrying both.
    pub fn on_authorization_settled(&mut self, held: AuthorizationHeld) -> Vec<Effect> {
        let mut effects = Vec::new();
        if self.waiting_pnc && held.pnc {
            effects.push(Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                AuthorizationResponse::GRANTED_PLUG_AND_CHARGE,
            )));
            self.clear();
        }
        if self.waiting_eim && held.eim {
            effects.push(Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(
                AuthorizationResponse::GRANTED_EIM,
            )));
            self.clear();
        }
        effects
    }

    /// The routing half of `handle_authorize_response`
    /// (`evse/evse_managerImpl.cpp:424-455`).
    pub fn route(&self, verdict: Verdict) -> Route {
        route(self.waiting_pnc, verdict)
    }
}

/// The routing half of `handle_authorize_response`
/// (`evse/evse_managerImpl.cpp:424-455`), as a function of the one fact it
/// reads off this state machine.
///
/// A free function because the command it serves reaches every deployment and
/// only one of the three answers is a stack fact. A basic AC port holds no
/// `Authz`, and nothing there can be waiting for a plug and charge answer, so
/// `Core` calls this with `false` and a refusal stays a refusal. Reading
/// `Route::Grant` for the absent case instead would have authorized on a
/// rejection.
pub fn route(waiting_pnc: bool, verdict: Verdict) -> Route {
    let plug_and_charge = verdict.kind == AuthorizationKind::PlugAndCharge;
    if verdict.status == AuthorizationStatus::Accepted {
        if waiting_pnc && !plug_and_charge {
            return Route::Ignore;
        }
        return Route::Grant;
    }
    Route::Refused(plug_and_charge.then(|| AuthorizationResponse {
        status: verdict.status,
        certificate: verdict.certificate.unwrap_or(CertificateStatus::Accepted),
    }))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::{Mapping, RawConfig, Settings, Wiring};
    use serde_json::{json, Value};

    /// The wiring an `HlcConfig` exists on: the stack and SLAC connected.
    /// `HlcConfig::for_deployment` reads nothing else.
    fn fully_wired() -> Wiring {
        Wiring {
            hlc: true,
            slac: true,
            ..Wiring::default()
        }
    }

    fn config(pairs: &[(&str, Value)]) -> HlcConfig {
        let mut all = vec![
            ("connector_id", json!(1)),
            // A configuration exists only where the stack does, and on AC that
            // needs the key as well as the wiring.
            ("ac_hlc_enabled", json!(true)),
        ];
        all.extend_from_slice(pairs);
        let raw: RawConfig = all
            .iter()
            .cloned()
            .map(|(key, value)| (key.to_string(), value))
            .collect();
        let settings = Settings::from_raw(
            &raw,
            &Mapping {
                evse: 1,
                connectors: vec![1],
            },
        )
        .unwrap();
        // Wired and enabled, which is the only shape an `HlcConfig` has:
        // `for_deployment` is the one constructor and it answers `None` for a
        // deployment `hlc_enabled` would have been false on.
        HlcConfig::for_deployment(&settings, &fully_wired())
            .expect("a wired deployment has a configuration")
    }

    fn autocharging() -> HlcConfig {
        config(&[("enable_autocharge", json!(true))])
    }

    fn held(eim: bool, pnc: bool) -> AuthorizationHeld {
        AuthorizationHeld {
            eim,
            pnc,
            charging: false,
        }
    }

    fn nothing_held() -> AuthorizationHeld {
        AuthorizationHeld::default()
    }

    fn a_token() -> OpaqueToken {
        OpaqueToken::new(json!({
            "id_token": {"value": "CONTRACT", "type": "eMAID"},
            "authorization_type": "PlugAndCharge",
            "connectors": [7],
        }))
    }

    /// Whether the bridge is waiting for a contract, asserted through the one
    /// production reader of that flag rather than through the field: an
    /// ordinary permission is dropped exactly while a contract is awaited
    /// (`evse/evse_managerImpl.cpp:430-434`).
    fn awaits_a_contract(authz: &Authz) -> bool {
        authz.route(verdict(
            AuthorizationKind::Eim,
            AuthorizationStatus::Accepted,
        )) == Route::Ignore
    }

    fn granted(effects: &[Effect]) -> Vec<AuthorizationResponse> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::HlcUpdate(HlcUpdate::AuthorizationResponse(response)) => Some(*response),
                _ => None,
            })
            .collect()
    }

    fn published(effects: &[Effect]) -> Vec<ProvidedToken> {
        effects
            .iter()
            .filter_map(|effect| match effect {
                Effect::PublishProvidedToken(token) => Some(token.clone()),
                _ => None,
            })
            .collect()
    }

    // The identity the autocharge token carries.

    #[test]
    fn an_autocharge_identity_is_the_mac_address_without_its_colons() {
        assert_eq!(autocharge_id_token("AA:BB:CC:DD:EE:FF"), "VID:AABBCCDDEEFF");
    }

    #[test]
    fn an_autocharge_identity_that_arrived_without_colons_is_unchanged() {
        assert_eq!(autocharge_id_token("AABBCCDDEEFF"), "VID:AABBCCDDEEFF");
    }

    /// The three answers this module originates, pinned against literals.
    ///
    /// Every other test in this file names the constant, so a constant with the
    /// wrong pair in it would satisfy all of them at once. These are the only
    /// assertions that do not.
    #[test]
    fn the_three_answers_this_module_originates_carry_the_pairs_the_cpp_sends() {
        assert_eq!(
            AuthorizationResponse::GRANTED_PLUG_AND_CHARGE,
            AuthorizationResponse {
                status: AuthorizationStatus::Accepted,
                certificate: CertificateStatus::Accepted,
            },
            "`EvseManager.cpp:1894-1895`"
        );
        assert_eq!(
            AuthorizationResponse::GRANTED_EIM,
            AuthorizationResponse {
                status: AuthorizationStatus::Accepted,
                certificate: CertificateStatus::NoCertificateAvailable,
            },
            "`EvseManager.cpp:1004-1005` and `:1902-1903`"
        );
        assert_eq!(
            AuthorizationResponse::TIMED_OUT,
            AuthorizationResponse {
                status: AuthorizationStatus::Unknown,
                certificate: CertificateStatus::NoCertificateAvailable,
            },
            "`EvseManager.cpp:415-416`"
        );
    }

    // The external identification handler.

    #[test]
    fn a_request_arriving_after_the_port_is_already_authorized_is_answered_at_once() {
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&config(&[]), held(true, false), None);

        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
        assert!(!awaits_a_contract(&authz), "nothing is left waiting");
    }

    #[test]
    fn an_answer_given_at_once_reports_that_no_certificate_took_part() {
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&config(&[]), held(true, false), None);

        assert_eq!(
            granted(&effects),
            vec![AuthorizationResponse {
                status: AuthorizationStatus::Accepted,
                certificate: CertificateStatus::NoCertificateAvailable,
            }]
        );
    }

    #[test]
    fn an_answer_given_at_once_also_drops_a_contract_request_left_standing() {
        // Reachable: a card authorizes the port, the vehicle then asks on its
        // contract, and only afterwards asks by external identification. The
        // C++ clears both flags before it answers (`EvseManager.cpp:1002-1003`).
        // Left standing, the contract flag would drop every later ordinary
        // verdict through the precedence rule and answer the vehicle a second
        // time if a contract grant ever arrived.
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        authz.on_require_eim(&config(&[]), held(true, false), None);

        assert!(!awaits_a_contract(&authz), "the contract request is spent");
        assert!(
            authz.on_authorization_settled(held(false, true)).is_empty(),
            "a contract grant is nobody's answer now"
        );
    }

    #[test]
    fn a_request_with_no_authorization_yet_leaves_the_vehicle_waiting_and_says_nothing() {
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&config(&[]), nothing_held(), None);

        assert!(effects.is_empty(), "{effects:?}");
    }

    #[test]
    fn a_pending_external_identification_request_is_answered_when_the_permission_arrives() {
        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);

        let effects = authz.on_authorization_settled(held(true, false));

        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
    }

    #[test]
    fn autocharge_offers_the_vehicles_own_identity_as_a_token() {
        let mut authz = Authz::default();

        let effects =
            authz.on_require_eim(&autocharging(), nothing_held(), Some("VID:AABBCCDDEEFF"));

        assert_eq!(
            published(&effects),
            vec![ProvidedToken::Autocharge {
                id_token: "VID:AABBCCDDEEFF".into(),
                connectors: vec![1],
            }]
        );
    }

    #[test]
    fn autocharge_left_off_offers_nothing() {
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&config(&[]), nothing_held(), Some("VID:AABBCCDDEEFF"));

        assert!(published(&effects).is_empty(), "{effects:?}");
    }

    #[test]
    fn autocharge_with_no_identity_reported_yet_offers_nothing() {
        // The C++ publishes its value initialized member here, which carries an
        // empty identity. Declining is the divergence recorded in the handler.
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&autocharging(), nothing_held(), None);

        assert!(published(&effects).is_empty(), "{effects:?}");
    }

    #[test]
    fn autocharge_is_not_offered_when_the_port_is_already_authorized() {
        let mut authz = Authz::default();

        let effects =
            authz.on_require_eim(&autocharging(), held(true, false), Some("VID:AABBCCDDEEFF"));

        assert!(published(&effects).is_empty(), "{effects:?}");
        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
    }

    #[test]
    fn autocharge_left_off_on_an_already_authorized_port_offers_nothing_either() {
        // The fourth corner of autocharge crossed with an authorization already
        // held. The other three are above.
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&config(&[]), held(true, false), None);

        assert!(published(&effects).is_empty(), "{effects:?}");
        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
    }

    #[test]
    fn the_debug_arm_offers_autocharge_where_the_ordinary_arm_would_have_answered() {
        // The two settings cross: the debug arm decides which branch is taken,
        // and the branch decides whether autocharge offers anything. With an
        // authorization held but not yet in use, the debug arm falls through to
        // the offer that the ordinary arm would have skipped.
        let mut debugging = autocharging();
        debugging.dbg_auth_after_tstep = true;
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&debugging, held(true, false), Some("VID:AABBCCDDEEFF"));

        assert_eq!(
            published(&effects),
            vec![ProvidedToken::Autocharge {
                id_token: "VID:AABBCCDDEEFF".into(),
                connectors: vec![1],
            }],
            "{effects:?}"
        );
        assert!(granted(&effects).is_empty(), "{effects:?}");
    }

    #[test]
    fn the_debug_arm_waits_for_charging_before_it_calls_the_port_authorized() {
        let debugging = config(&[("dbg_hlc_auth_after_tstep", json!(true))]);
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(&debugging, held(true, false), None);

        assert!(
            granted(&effects).is_empty(),
            "an authorization not yet in use does not count for the debug arm, got {effects:?}"
        );
    }

    #[test]
    fn the_debug_arm_answers_once_the_port_is_charging() {
        let debugging = config(&[("dbg_hlc_auth_after_tstep", json!(true))]);
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(
            &debugging,
            AuthorizationHeld {
                eim: true,
                pnc: false,
                charging: true,
            },
            None,
        );

        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
    }

    #[test]
    fn the_ordinary_arm_ignores_whether_the_port_is_charging() {
        let mut authz = Authz::default();

        let effects = authz.on_require_eim(
            &config(&[]),
            AuthorizationHeld {
                eim: true,
                pnc: false,
                charging: false,
            },
            None,
        );

        assert_eq!(granted(&effects), vec![AuthorizationResponse::GRANTED_EIM]);
    }

    // The plug and charge handler.

    #[test]
    fn a_plug_and_charge_request_offers_the_token_the_stack_handed_over() {
        let mut authz = Authz::default();

        let effects = authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        assert_eq!(
            published(&effects),
            vec![ProvidedToken::PlugAndCharge {
                token: a_token(),
                connectors: vec![1],
            }]
        );
    }

    #[test]
    fn a_plug_and_charge_request_leaves_the_vehicle_waiting_and_says_nothing() {
        let mut authz = Authz::default();

        let effects = authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        assert!(granted(&effects).is_empty(), "{effects:?}");
        assert!(awaits_a_contract(&authz));
    }

    #[test]
    fn a_plug_and_charge_request_on_an_already_authorized_port_offers_nothing() {
        // Republishing the token would have it validated a second time, and the
        // resumed session would be a new one. This is what makes pause and
        // resume work with plug and charge.
        let mut authz = Authz::default();

        let effects = authz.on_require_plug_and_charge(&config(&[]), held(false, true), a_token());

        assert!(effects.is_empty(), "{effects:?}");
        assert!(!awaits_a_contract(&authz), "nothing is left waiting");
    }

    #[test]
    fn a_pending_plug_and_charge_request_is_answered_when_the_permission_arrives() {
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        let effects = authz.on_authorization_settled(held(false, true));

        assert_eq!(
            granted(&effects),
            vec![AuthorizationResponse::GRANTED_PLUG_AND_CHARGE],
            "a plug and charge grant reports the contract certificate as accepted"
        );
    }

    // The response gate.

    #[test]
    fn a_permission_nobody_asked_for_is_not_announced_to_the_vehicle() {
        let mut authz = Authz::default();

        let effects = authz.on_authorization_settled(held(true, false));

        assert!(effects.is_empty(), "{effects:?}");
    }

    #[test]
    fn a_permission_of_the_other_kind_neither_answers_nor_stops_the_waiting() {
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        let effects = authz.on_authorization_settled(held(true, false));

        assert!(effects.is_empty(), "{effects:?}");
        assert!(
            awaits_a_contract(&authz),
            "the vehicle is still waiting for the kind it asked for"
        );
    }

    #[test]
    fn a_matching_permission_is_announced_once_and_not_again() {
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        let first = authz.on_authorization_settled(held(false, true));
        let second = authz.on_authorization_settled(held(false, true));

        assert_eq!(
            granted(&first),
            vec![AuthorizationResponse::GRANTED_PLUG_AND_CHARGE]
        );
        assert!(second.is_empty(), "{second:?}");
    }

    #[test]
    fn at_most_one_kind_of_authorization_is_ever_waited_for() {
        // Both handlers set one flag and clear the other, so the two conditions
        // of the response gate are mutually exclusive whichever order the
        // requests arrive in. The gate keeps the C++ shape, two independent
        // conditions with plug and charge first, but only one of them can fire.
        let mut authz = Authz::default();
        let config = config(&[]);

        for _ in 0..2 {
            authz.on_require_eim(&config, nothing_held(), None);
            assert!(!awaits_a_contract(&authz));
            // Waiting for external identification, so a plug and charge
            // permission is not this vehicle's answer.
            assert!(authz.on_authorization_settled(held(false, true)).is_empty());

            authz.on_require_plug_and_charge(&config, nothing_held(), a_token());
            assert!(awaits_a_contract(&authz));
            // And the other way round.
            assert!(authz.on_authorization_settled(held(true, false)).is_empty());
        }
    }

    #[test]
    fn holding_both_kinds_at_once_still_answers_only_the_kind_that_was_asked_for() {
        // `AuthorizationHeld` can carry both, though `Auth` never reports both:
        // its two getters read the one permission and disagree only on how it
        // was obtained. Driven anyway, because this is the only input that
        // could make the gate's two conditions hold together, and it does not:
        // the waiting flags are exclusive, so the second condition is dead
        // whatever the permission says. That is why the C++ ordering, plug and
        // charge first and each condition clearing both flags, is unobservable
        // here rather than merely untested.
        let both = AuthorizationHeld {
            eim: true,
            pnc: true,
            charging: false,
        };

        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());
        assert_eq!(
            granted(&authz.on_authorization_settled(both)),
            vec![AuthorizationResponse::GRANTED_PLUG_AND_CHARGE],
            "one answer, and it is the one the vehicle asked for"
        );

        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);
        assert_eq!(
            granted(&authz.on_authorization_settled(both)),
            vec![AuthorizationResponse::GRANTED_EIM],
            "and the other way round"
        );
    }

    #[test]
    fn a_pending_request_with_no_permission_held_at_all_is_not_answered() {
        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);

        let effects = authz.on_authorization_settled(nothing_held());

        assert!(effects.is_empty(), "{effects:?}");
    }

    #[test]
    fn clearing_takes_both_flags_down() {
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());

        authz.clear();

        assert!(!awaits_a_contract(&authz));
        assert!(
            authz.on_authorization_settled(held(false, true)).is_empty(),
            "a cleared bridge answers nothing"
        );
        assert!(
            authz.on_authorization_settled(held(true, false)).is_empty(),
            "including the other kind"
        );
    }

    // Verdict routing.

    fn verdict(kind: AuthorizationKind, status: AuthorizationStatus) -> Verdict {
        Verdict {
            kind,
            status,
            certificate: None,
        }
    }

    fn waiting_for_plug_and_charge() -> Authz {
        let mut authz = Authz::default();
        authz.on_require_plug_and_charge(&config(&[]), nothing_held(), a_token());
        authz
    }

    #[test]
    fn an_ordinary_permission_arriving_while_the_vehicle_waits_for_a_contract_is_dropped() {
        let authz = waiting_for_plug_and_charge();

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::Eim,
                AuthorizationStatus::Accepted
            )),
            Route::Ignore
        );
    }

    #[test]
    fn a_contract_permission_arriving_while_the_vehicle_waits_for_one_is_applied() {
        let authz = waiting_for_plug_and_charge();

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::PlugAndCharge,
                AuthorizationStatus::Accepted
            )),
            Route::Grant
        );
    }

    #[test]
    fn an_ordinary_permission_is_applied_when_no_contract_is_being_waited_for() {
        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::Eim,
                AuthorizationStatus::Accepted
            )),
            Route::Grant
        );
    }

    #[test]
    fn a_refused_contract_is_reported_to_the_vehicle() {
        let authz = waiting_for_plug_and_charge();

        assert_eq!(
            authz.route(Verdict {
                kind: AuthorizationKind::PlugAndCharge,
                status: AuthorizationStatus::Blocked,
                certificate: Some(CertificateStatus::CertificateRevoked),
            }),
            Route::Refused(Some(AuthorizationResponse {
                status: AuthorizationStatus::Blocked,
                certificate: CertificateStatus::CertificateRevoked,
            }))
        );
    }

    #[test]
    fn a_refused_contract_that_named_no_certificate_status_reports_it_as_accepted() {
        let authz = waiting_for_plug_and_charge();

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::PlugAndCharge,
                AuthorizationStatus::Expired
            )),
            Route::Refused(Some(AuthorizationResponse {
                status: AuthorizationStatus::Expired,
                certificate: CertificateStatus::Accepted,
            }))
        );
    }

    #[test]
    fn an_ordinary_refusal_is_reported_to_no_one_and_applied_to_nothing() {
        // A later external identification attempt can still succeed, and a
        // vehicle told it was refused would have stopped waiting for one. The
        // charger is not told either, so the session it may be running lives on.
        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::Eim,
                AuthorizationStatus::Blocked
            )),
            Route::Refused(None)
        );
    }

    #[test]
    fn a_refused_contract_is_reported_even_though_nobody_asked_for_one() {
        // `evse/evse_managerImpl.cpp:447-454` reads the token kind and nothing
        // else: the forward is not gated on what the vehicle is waiting for,
        // unlike every grant this bridge announces.
        let authz = Authz::default();

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::PlugAndCharge,
                AuthorizationStatus::Blocked
            )),
            Route::Refused(Some(AuthorizationResponse {
                status: AuthorizationStatus::Blocked,
                certificate: CertificateStatus::Accepted,
            }))
        );
    }

    #[test]
    fn a_refusal_arriving_while_the_vehicle_waits_for_the_other_kind_is_still_routed_by_its_own_kind(
    ) {
        // The drop rule reads the accepted branch only, so a refusal is never
        // dropped by it whichever kind is being waited for.
        let authz = waiting_for_plug_and_charge();

        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::Eim,
                AuthorizationStatus::Invalid
            )),
            Route::Refused(None)
        );

        let mut authz = Authz::default();
        authz.on_require_eim(&config(&[]), nothing_held(), None);
        assert_eq!(
            authz.route(verdict(
                AuthorizationKind::PlugAndCharge,
                AuthorizationStatus::Invalid
            )),
            Route::Refused(Some(AuthorizationResponse {
                status: AuthorizationStatus::Invalid,
                certificate: CertificateStatus::Accepted,
            }))
        );
    }
}
