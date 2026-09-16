// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! What the module tells the ISO 15118 stack about payment and contracts.
//!
//! The C++ derives this three times, at three trigger points, and the three
//! derivations are not the same. `EvseManager.cpp:346-363` runs at boot,
//! `:1285-1308` on an `Authorized` or a `SessionFinished` session event, and
//! `:1322-1344` on a session start. Each ends in `call_session_setup`, and
//! those are the only three call sites of it in the module.
//!
//! What they share is the gating of the two contract flags on plug and charge
//! being enabled. What they do not share is which payment options they offer,
//! and the differences are load bearing rather than accidental:
//!
//! - The contract option is offered at boot whenever plug and charge is
//!   enabled, but on a session event only for `SessionFinished` and never for
//!   `Authorized`, because the stack must not offer the contract option or the
//!   certificate installation service to a session that is already authorized
//!   (the C++ comment at `:1297-1299`).
//! - A session start that was already authorized offers external payment alone
//!   and clears both contract flags.
//! - Boot and the session event arm fall back to external payment when both
//!   options are disabled; the session start arm does not, so it can produce an
//!   empty option list.
//!
//! So the three arms stay three arms here. Collapsing them would lose the
//! distinction, and the shared half is small enough that sharing it is all that
//! is worth sharing.

use crate::core::config::Settings;
use crate::core::session::StartSessionReason;

/// `types::iso15118::PaymentOption`.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum PaymentOption {
    Contract,
    ExternalPayment,
}

/// The plug and charge state, which is three `std::atomic_bool` in the C++
/// (`EvseManager.hpp:339-341`), seeded from configuration at
/// `EvseManager.cpp:198-200` and afterwards owned by the
/// `set_plug_and_charge_configuration` command.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct PlugAndCharge {
    /// `pnc_enabled`. Gates the other two, so neither can reach the stack set
    /// while plug and charge is off.
    pub enabled: bool,
    pub central_validation_allowed: bool,
    pub certificate_installation_enabled: bool,
}

impl PlugAndCharge {
    pub fn from_settings(settings: &Settings) -> Self {
        Self {
            enabled: settings.payment.enable_contract,
            central_validation_allowed: settings.payment.central_contract_validation_allowed,
            certificate_installation_enabled: settings
                .payment
                .contract_certificate_installation_enabled,
        }
    }

    /// The two flags as the stack may be told them: `(certificate service,
    /// central validation)`, both false unless plug and charge is enabled.
    ///
    /// This is the one line all three trigger points share
    /// (`EvseManager.cpp:348-350`, `:1287-1289`, `:1324-1327`). Returning the
    /// pair rather than exposing the raw fields is what makes the gate
    /// impossible for a fourth trigger point to forget.
    fn told(self) -> (bool, bool) {
        if self.enabled {
            (
                self.certificate_installation_enabled,
                self.central_validation_allowed,
            )
        } else {
            (false, false)
        }
    }

    /// The `set_plug_and_charge_configuration` command
    /// (`evse/evse_managerImpl.cpp:511-524`). Each field is optional and an
    /// absent one leaves the current value standing, which is the three
    /// `has_value()` guards there.
    pub fn apply(&mut self, request: &PlugAndChargeConfiguration) {
        if let Some(enabled) = request.enabled {
            self.enabled = enabled;
        }
        if let Some(allowed) = request.central_validation_allowed {
            self.central_validation_allowed = allowed;
        }
        if let Some(enabled) = request.certificate_installation_enabled {
            self.certificate_installation_enabled = enabled;
        }
    }
}

/// The `set_plug_and_charge_configuration` payload,
/// `types::evse_manager::PlugAndChargeConfiguration`. Every field is optional
/// on the wire and means "leave this one alone" when absent.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub struct PlugAndChargeConfiguration {
    pub enabled: Option<bool>,
    pub central_validation_allowed: Option<bool>,
    pub certificate_installation_enabled: Option<bool>,
}

/// Which of the three C++ trigger points is emitting.
///
/// The session event arm names only the two events that reach it: the C++
/// lambda early returns for every other value (`EvseManager.cpp:1280-1282`), so
/// a third event is not representable here rather than being filtered again.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Trigger {
    /// `EvseManager.cpp:362`, unconditional inside the boot block.
    Boot,
    /// `EvseManager.cpp:1307` with the event `Authorized`.
    Authorized,
    /// `EvseManager.cpp:1307` with the event `SessionFinished`.
    SessionFinished,
    /// `EvseManager.cpp:1343`, for any start reason.
    SessionStarted(StartSessionReason),
}

/// The three `call_session_setup` arguments.
///
/// Field named for the interface argument (`interfaces/ISO15118_charger.yaml`
/// `session_setup`), not for the C++ local `_contract_certificate_installation_enabled`,
/// because the two flags are adjacent booleans on a three argument call whose
/// generated Rust order is alphabetical and therefore neither the C++ order nor
/// the interface order.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SessionSetup {
    pub payment_options: Vec<PaymentOption>,
    pub supported_certificate_service: bool,
    pub central_contract_validation_allowed: bool,
    /// `EvseManager::fake_dc_enabled`, the fourth wire argument.
    ///
    /// Not derived here: it is the power path's answer, carried through so the
    /// three trigger points cannot disagree about it. `PowerPath::presents_fake_dc`
    /// says why the path owns it.
    pub fake_dc: bool,
}

/// The derivation, one arm per trigger point.
pub fn derive(
    trigger: Trigger,
    pnc: PlugAndCharge,
    payment_enable_eim: bool,
    fake_dc: bool,
) -> SessionSetup {
    let (mut supported_certificate_service, mut central_contract_validation_allowed) = pnc.told();
    let mut payment_options = Vec::new();

    match trigger {
        // `EvseManager.cpp:352-361`.
        Trigger::Boot => {
            if payment_enable_eim {
                payment_options.push(PaymentOption::ExternalPayment);
            }
            if pnc.enabled {
                payment_options.push(PaymentOption::Contract);
            }
            if !payment_enable_eim && !pnc.enabled {
                warn_about_both_options_disabled();
                payment_options.push(PaymentOption::ExternalPayment);
            }
        }

        // `EvseManager.cpp:1291-1306`. The contract option is offered for a
        // finished session and not for an authorized one, and the else branch
        // withdraws the certificate service while leaving central validation
        // alone.
        Trigger::Authorized | Trigger::SessionFinished => {
            if payment_enable_eim {
                payment_options.push(PaymentOption::ExternalPayment);
            }
            if pnc.enabled && trigger == Trigger::SessionFinished {
                payment_options.push(PaymentOption::Contract);
            } else {
                supported_certificate_service = false;
            }
            if !payment_enable_eim && !pnc.enabled {
                warn_about_both_options_disabled();
                payment_options.push(PaymentOption::ExternalPayment);
            }
        }

        // `EvseManager.cpp:1329-1342`. No fallback push, so this arm can leave
        // the list empty.
        Trigger::SessionStarted(StartSessionReason::Authorized) => {
            payment_options.push(PaymentOption::ExternalPayment);
            supported_certificate_service = false;
            central_contract_validation_allowed = false;
        }
        Trigger::SessionStarted(StartSessionReason::EvConnected) => {
            if payment_enable_eim {
                payment_options.push(PaymentOption::ExternalPayment);
            }
            if pnc.enabled {
                payment_options.push(PaymentOption::Contract);
            }
        }
    }

    SessionSetup {
        payment_options,
        supported_certificate_service,
        central_contract_validation_allowed,
        fake_dc,
    }
}

/// `EvseManager.cpp:359` and `:1304`. A vehicle offered no payment option at
/// all cannot authorize, so external payment is offered whatever the
/// deployment configured, and the deployment is told its configuration was
/// overridden.
fn warn_about_both_options_disabled() {
    log::warn!(
        "Both payment options are disabled! ExternalPayment is nevertheless enabled in this case."
    );
}

#[cfg(test)]
mod tests {
    use super::*;

    const BOTH_CONTRACT_FLAGS: PlugAndCharge = PlugAndCharge {
        enabled: true,
        central_validation_allowed: true,
        certificate_installation_enabled: true,
    };

    fn pnc(enabled: bool) -> PlugAndCharge {
        PlugAndCharge {
            enabled,
            ..BOTH_CONTRACT_FLAGS
        }
    }

    fn started(reason: StartSessionReason) -> Trigger {
        Trigger::SessionStarted(reason)
    }

    /// Every trigger point, so a property asserted below is asserted over all
    /// four of them and a fifth cannot be added without appearing here.
    const EVERY_TRIGGER: &[Trigger] = &[
        Trigger::Boot,
        Trigger::Authorized,
        Trigger::SessionFinished,
        Trigger::SessionStarted(StartSessionReason::EvConnected),
        Trigger::SessionStarted(StartSessionReason::Authorized),
    ];

    #[test]
    fn disabled_plug_and_charge_clears_both_contract_flags_at_every_trigger_point() {
        for trigger in EVERY_TRIGGER {
            let setup = derive(*trigger, pnc(false), true, false);
            assert!(
                !setup.supported_certificate_service,
                "{trigger:?} left the certificate service set"
            );
            assert!(
                !setup.central_contract_validation_allowed,
                "{trigger:?} left central validation set"
            );
        }
    }

    #[test]
    fn disabled_plug_and_charge_offers_no_contract_at_any_trigger_point() {
        for trigger in EVERY_TRIGGER {
            let setup = derive(*trigger, pnc(false), true, false);
            assert!(
                !setup.payment_options.contains(&PaymentOption::Contract),
                "{trigger:?} offered the contract option"
            );
        }
    }

    #[test]
    fn boot_offers_both_options_when_both_are_enabled() {
        let setup = derive(Trigger::Boot, pnc(true), true, false);
        assert_eq!(
            setup.payment_options,
            vec![PaymentOption::ExternalPayment, PaymentOption::Contract]
        );
        assert!(setup.supported_certificate_service);
        assert!(setup.central_contract_validation_allowed);
    }

    #[test]
    fn boot_offers_the_contract_alone_without_external_payment() {
        let setup = derive(Trigger::Boot, pnc(true), false, false);
        assert_eq!(setup.payment_options, vec![PaymentOption::Contract]);
    }

    #[test]
    fn a_session_finished_offers_the_contract_and_keeps_the_certificate_service() {
        let setup = derive(Trigger::SessionFinished, pnc(true), true, false);
        assert_eq!(
            setup.payment_options,
            vec![PaymentOption::ExternalPayment, PaymentOption::Contract]
        );
        assert!(setup.supported_certificate_service);
        assert!(setup.central_contract_validation_allowed);
    }

    /// The distinction the two events exist to draw
    /// (`EvseManager.cpp:1294-1301`). An already authorized session must not be
    /// offered the contract option or the certificate installation service.
    #[test]
    fn an_authorized_event_offers_no_contract_and_withdraws_the_certificate_service() {
        let setup = derive(Trigger::Authorized, pnc(true), true, false);
        assert_eq!(setup.payment_options, vec![PaymentOption::ExternalPayment]);
        assert!(!setup.supported_certificate_service);
    }

    /// The asymmetry inside that same else branch: it clears the certificate
    /// service and leaves central validation standing.
    #[test]
    fn an_authorized_event_leaves_central_validation_standing() {
        let setup = derive(Trigger::Authorized, pnc(true), true, false);
        assert!(setup.central_contract_validation_allowed);
    }

    #[test]
    fn a_session_start_that_was_already_authorized_offers_external_payment_alone() {
        let setup = derive(
            started(StartSessionReason::Authorized),
            pnc(true),
            true,
            false,
        );
        assert_eq!(setup.payment_options, vec![PaymentOption::ExternalPayment]);
        assert!(!setup.supported_certificate_service);
        assert!(!setup.central_contract_validation_allowed);
    }

    /// The one arm that clears central validation, which the `Authorized`
    /// session event arm does not (`EvseManager.cpp:1333`).
    #[test]
    fn an_authorized_start_clears_central_validation_where_an_authorized_event_does_not() {
        let start = derive(
            started(StartSessionReason::Authorized),
            pnc(true),
            true,
            false,
        );
        let event = derive(Trigger::Authorized, pnc(true), true, false);
        assert!(!start.central_contract_validation_allowed);
        assert!(event.central_contract_validation_allowed);
    }

    /// An already authorized start offers external payment even where the
    /// deployment disabled it, because the arm pushes it unconditionally
    /// (`EvseManager.cpp:1331`).
    #[test]
    fn an_authorized_start_offers_external_payment_even_where_it_is_disabled() {
        let setup = derive(
            started(StartSessionReason::Authorized),
            pnc(true),
            false,
            false,
        );
        assert_eq!(setup.payment_options, vec![PaymentOption::ExternalPayment]);
    }

    #[test]
    fn a_start_from_a_plug_in_offers_the_configured_options() {
        let setup = derive(
            started(StartSessionReason::EvConnected),
            pnc(true),
            true,
            false,
        );
        assert_eq!(
            setup.payment_options,
            vec![PaymentOption::ExternalPayment, PaymentOption::Contract]
        );
        assert!(setup.supported_certificate_service);
        assert!(setup.central_contract_validation_allowed);
    }

    /// The fallback at `EvseManager.cpp:358-361` and `:1303-1306`: a deployment
    /// with both options off is still offered external payment, because a
    /// vehicle offered nothing cannot authorize at all.
    #[test]
    fn boot_and_the_session_events_fall_back_to_external_payment() {
        for trigger in [Trigger::Boot, Trigger::Authorized, Trigger::SessionFinished] {
            let setup = derive(trigger, pnc(false), false, false);
            assert_eq!(
                setup.payment_options,
                vec![PaymentOption::ExternalPayment],
                "{trigger:?} offered nothing"
            );
        }
    }

    /// The session start arm has no such fallback, so it can offer nothing at
    /// all. `interfaces/ISO15118_charger.yaml` declares `minItems: 1` on the
    /// list, so this is a C++ defect rather than a decision; it is ported
    /// unchanged because inventing the missing arm would change what a
    /// deployment with both options disabled is offered mid session, and that
    /// belongs in the C++ first.
    #[test]
    fn a_start_from_a_plug_in_can_offer_nothing_where_both_options_are_disabled() {
        let setup = derive(
            started(StartSessionReason::EvConnected),
            pnc(false),
            false,
            false,
        );
        assert!(setup.payment_options.is_empty());
    }

    /// The combination no test above drove: external payment disabled and plug
    /// and charge enabled. It is the row that exposes a second instance of the
    /// empty option list, because the fallback at `EvseManager.cpp:1303` is
    /// guarded on *both* being off and so does not fire here, while the
    /// `Authorized` branch offers no contract either.
    #[test]
    fn an_authorized_event_with_external_payment_disabled_offers_nothing() {
        let setup = derive(Trigger::Authorized, pnc(true), false, false);
        assert!(setup.payment_options.is_empty());
        assert!(!setup.supported_certificate_service);
        assert!(setup.central_contract_validation_allowed);
    }

    /// The same row on the other two arms, where it does offer something: the
    /// contract alone.
    #[test]
    fn the_contract_alone_is_offered_where_external_payment_is_disabled() {
        for trigger in [
            Trigger::SessionFinished,
            started(StartSessionReason::EvConnected),
        ] {
            assert_eq!(
                derive(trigger, pnc(true), false, false).payment_options,
                vec![PaymentOption::Contract],
                "{trigger:?}"
            );
        }
    }

    /// The full input space of the derivation, so no row is left undriven: five
    /// triggers by the two configuration booleans. The table is the assertion,
    /// which makes every row visible rather than implied by a property.
    #[test]
    fn every_combination_of_the_two_settings_is_pinned_at_every_trigger_point() {
        use PaymentOption::{Contract, ExternalPayment};

        let expected: &[(Trigger, bool, bool, &[PaymentOption])] = &[
            (Trigger::Boot, true, true, &[ExternalPayment, Contract]),
            (Trigger::Boot, true, false, &[ExternalPayment]),
            (Trigger::Boot, false, true, &[Contract]),
            (Trigger::Boot, false, false, &[ExternalPayment]),
            (Trigger::Authorized, true, true, &[ExternalPayment]),
            (Trigger::Authorized, true, false, &[ExternalPayment]),
            // The second empty list, and the reason this table exists.
            (Trigger::Authorized, false, true, &[]),
            (Trigger::Authorized, false, false, &[ExternalPayment]),
            (
                Trigger::SessionFinished,
                true,
                true,
                &[ExternalPayment, Contract],
            ),
            (Trigger::SessionFinished, true, false, &[ExternalPayment]),
            (Trigger::SessionFinished, false, true, &[Contract]),
            (Trigger::SessionFinished, false, false, &[ExternalPayment]),
            (
                Trigger::SessionStarted(StartSessionReason::EvConnected),
                true,
                true,
                &[ExternalPayment, Contract],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::EvConnected),
                true,
                false,
                &[ExternalPayment],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::EvConnected),
                false,
                true,
                &[Contract],
            ),
            // The third empty list.
            (
                Trigger::SessionStarted(StartSessionReason::EvConnected),
                false,
                false,
                &[],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::Authorized),
                true,
                true,
                &[ExternalPayment],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::Authorized),
                true,
                false,
                &[ExternalPayment],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::Authorized),
                false,
                true,
                &[ExternalPayment],
            ),
            (
                Trigger::SessionStarted(StartSessionReason::Authorized),
                false,
                false,
                &[ExternalPayment],
            ),
        ];

        assert_eq!(
            expected.len(),
            EVERY_TRIGGER.len() * 4,
            "the table must cover every trigger point against both settings"
        );
        for (trigger, eim, contract, options) in expected {
            assert_eq!(
                derive(*trigger, pnc(*contract), *eim, false).payment_options,
                options.to_vec(),
                "{trigger:?} with eim {eim} and pnc {contract}"
            );
        }
    }

    #[test]
    fn external_payment_is_offered_first_wherever_both_are_offered() {
        for trigger in EVERY_TRIGGER {
            let options = derive(*trigger, pnc(true), true, false).payment_options;
            if options.contains(&PaymentOption::Contract) {
                assert_eq!(
                    options.first(),
                    Some(&PaymentOption::ExternalPayment),
                    "{trigger:?} ordered the options the other way round"
                );
            }
        }
    }

    #[test]
    fn no_option_is_offered_twice_at_any_trigger_point() {
        for trigger in EVERY_TRIGGER {
            for eim in [false, true] {
                for enabled in [false, true] {
                    let mut options = derive(*trigger, pnc(enabled), eim, false).payment_options;
                    let before = options.len();
                    options.dedup();
                    assert_eq!(
                        before,
                        options.len(),
                        "{trigger:?} with eim {eim} and pnc {enabled} repeated an option"
                    );
                }
            }
        }
    }

    /// Each contract flag reaches the stack independently, so neither can be
    /// wired to the other's source.
    #[test]
    fn the_two_contract_flags_travel_independently() {
        let only_certificate = PlugAndCharge {
            enabled: true,
            central_validation_allowed: false,
            certificate_installation_enabled: true,
        };
        let only_central = PlugAndCharge {
            enabled: true,
            central_validation_allowed: true,
            certificate_installation_enabled: false,
        };
        let setup = derive(Trigger::Boot, only_certificate, true, false);
        assert!(setup.supported_certificate_service);
        assert!(!setup.central_contract_validation_allowed);

        let setup = derive(Trigger::Boot, only_central, true, false);
        assert!(!setup.supported_certificate_service);
        assert!(setup.central_contract_validation_allowed);
    }

    // The command.

    #[test]
    fn an_absent_field_leaves_its_current_value_standing() {
        let mut state = BOTH_CONTRACT_FLAGS;
        state.apply(&PlugAndChargeConfiguration::default());
        assert_eq!(state, BOTH_CONTRACT_FLAGS);
    }

    #[test]
    fn each_field_sets_only_its_own() {
        let mut state = BOTH_CONTRACT_FLAGS;
        state.apply(&PlugAndChargeConfiguration {
            enabled: Some(false),
            ..PlugAndChargeConfiguration::default()
        });
        assert_eq!(
            state,
            PlugAndCharge {
                enabled: false,
                ..BOTH_CONTRACT_FLAGS
            }
        );

        let mut state = BOTH_CONTRACT_FLAGS;
        state.apply(&PlugAndChargeConfiguration {
            central_validation_allowed: Some(false),
            ..PlugAndChargeConfiguration::default()
        });
        assert_eq!(
            state,
            PlugAndCharge {
                central_validation_allowed: false,
                ..BOTH_CONTRACT_FLAGS
            }
        );

        let mut state = BOTH_CONTRACT_FLAGS;
        state.apply(&PlugAndChargeConfiguration {
            certificate_installation_enabled: Some(false),
            ..PlugAndChargeConfiguration::default()
        });
        assert_eq!(
            state,
            PlugAndCharge {
                certificate_installation_enabled: false,
                ..BOTH_CONTRACT_FLAGS
            }
        );
    }

    /// Turning plug and charge off through the command reaches the next
    /// derivation, which is the whole point of the command.
    #[test]
    fn turning_plug_and_charge_off_withdraws_the_contract_from_the_next_setup() {
        let mut state = BOTH_CONTRACT_FLAGS;
        assert!(derive(Trigger::Boot, state, true, false)
            .payment_options
            .contains(&PaymentOption::Contract));
        state.apply(&PlugAndChargeConfiguration {
            enabled: Some(false),
            ..PlugAndChargeConfiguration::default()
        });
        assert!(!derive(Trigger::Boot, state, true, false)
            .payment_options
            .contains(&PaymentOption::Contract));
    }

    #[test]
    fn the_configured_values_seed_the_state() {
        use crate::core::config::{Mapping, RawConfig, Settings};
        use serde_json::json;

        let raw: RawConfig = [
            ("payment_enable_contract", json!(false)),
            ("central_contract_validation_allowed", json!(true)),
            ("contract_certificate_installation_enabled", json!(false)),
        ]
        .into_iter()
        .map(|(k, v)| (k.to_string(), v))
        .collect();
        let settings = Settings::from_raw(
            &raw,
            &Mapping {
                evse: 1,
                connectors: vec![1],
            },
        )
        .unwrap();

        assert_eq!(
            PlugAndCharge::from_settings(&settings),
            PlugAndCharge {
                enabled: false,
                central_validation_allowed: true,
                certificate_installation_enabled: false,
            }
        );
    }
}
