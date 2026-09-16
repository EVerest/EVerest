// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The identity a token carried, `shared_context.id_token` and
//! `shared_context.stop_transaction_id_token` in the C++.
//!
//! One `types::authorization::ProvidedIdToken`, which is the type all three of
//! `SessionStarted.id_tag`, `TransactionStarted.id_tag` and
//! `TransactionFinished.id_tag` are.

use serde_json::Value;

/// `types::authorization::IdTokenType`, the kind of credential an identity
/// declares.
///
/// Every one of the eight the interface names, because
/// `Charger::start_transaction` maps all eight onto the OCMF identification
/// type through `utils::convert_to_ocmf_identification_type` and the mapping is
/// total: there is no value this module may answer `UNDEFINED` for by default.
/// A card is `ISO14443` and is billed as one.
///
/// Mirrored rather than read out of [`IdTag::payload`], for the reason the
/// record's own note gives: `core` cannot name the wire type and indexing the
/// payload by field name as text is not a reader this module allows.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum IdTokenType {
    Central,
    EMaid,
    MacAddress,
    Iso14443,
    Iso15693,
    KeyCode,
    Local,
    NoAuthorization,
}

/// One `ProvidedIdToken`: the whole wire record, plus the three facts this
/// module decides on.
///
/// The record travels as its own serialization for the reason
/// `hlc::authz::OpaqueToken` does, and the reason is sharper here. It is eight
/// fields deep in three nested wire types, and every one of the six this module
/// does not read is read by a consumer of the events it appears on:
/// `OCPP201::process_transaction_started` and
/// `ChargePointV2::on_event_transaction_started` render `id_token.type` and
/// `id_token.additional_info` into the OCPP `idToken`, `request_id` into
/// `remoteStartId` and `parent_id_token` into `groupIdToken`, and read
/// `authorization_type` to decide whether the trigger reason is `RemoteStart`.
/// A mirror of the record in `core` would have to be widened by hand every time
/// the interface gains a field, and a field it failed to carry would reach the
/// wire as a plausible default that no consumer could tell from the real thing.
/// Carrying the serialization cannot drop one.
///
/// `value`, `token_type` and `plug_and_charge` sit beside it rather than being
/// read out of it because `core` cannot name the wire type and would otherwise
/// have to index the payload by field name as text. They are derived once, from
/// the same record, by the single constructor below; nothing else may build one.
#[derive(Clone, Debug, PartialEq)]
pub struct IdTag {
    payload: Value,
    value: String,
    token_type: IdTokenType,
    plug_and_charge: bool,
}

impl IdTag {
    /// The one constructor. `value` is `id_token.value`, `token_type` is
    /// `id_token.type` and `plug_and_charge` is
    /// `authorization_type == PlugAndCharge`, all three taken from the same
    /// record `payload` serializes, which is what keeps them from disagreeing
    /// with it.
    pub fn new(
        payload: Value,
        value: String,
        token_type: IdTokenType,
        plug_and_charge: bool,
    ) -> Self {
        Self {
            payload,
            value,
            token_type,
            plug_and_charge,
        }
    }

    /// The whole record, for the boundary to deserialize back into the wire
    /// type it came from.
    pub fn payload(&self) -> &Value {
        &self.payload
    }

    /// `id_token.value`, the only part of the identity any decision in this
    /// module reads. It is what opens the metering transaction
    /// (`Charger::start_transaction` fills `req.identification_data` from it).
    pub fn value(&self) -> &str {
        &self.value
    }

    /// `id_token.type`, the credential kind the identity declares.
    ///
    /// It is what the metering transaction is billed under:
    /// `Charger::start_transaction` fills `req.identification_type` from
    /// `utils::convert_to_ocmf_identification_type(id_token.id_token.type)`.
    /// Not the same question as [`Self::is_plug_and_charge`], which reads
    /// `authorization_type`: the C++ bills on this field alone.
    pub fn token_type(&self) -> IdTokenType {
        self.token_type
    }

    /// Whether the authorization was negotiated over ISO 15118, the C++
    /// `authorized_pnc`. `Charger.cpp:1645-1646` reads the token's declared
    /// authorization type on every accepted authorization for this.
    pub fn is_plug_and_charge(&self) -> bool {
        self.plug_and_charge
    }
}

/// Every value the interface names, for the tests that have to drive all of
/// them.
///
/// A ninth value cannot be added without noticing: `token_type_on_the_wire`
/// below and `ocmf_identification_type` and `id_token_type_of` at the boundary
/// are all total matches, so the interface gaining one is a compile error in
/// three places. What this array adds is that a test can enumerate the domain
/// rather than sampling it.
#[cfg(test)]
pub(crate) const EVERY_TOKEN_TYPE: [IdTokenType; 8] = [
    IdTokenType::Central,
    IdTokenType::EMaid,
    IdTokenType::MacAddress,
    IdTokenType::Iso14443,
    IdTokenType::Iso15693,
    IdTokenType::KeyCode,
    IdTokenType::Local,
    IdTokenType::NoAuthorization,
];

/// The wire spelling of a token type, so a payload built here cannot claim a
/// different type from the one derived beside it.
///
/// The spellings are `types/authorization.yaml`'s own, which is what
/// `id_tag_from` at the boundary reads them back out of. Production reads it
/// for the free charging identity below; the test fixtures read it for the
/// same reason.
pub(crate) fn token_type_on_the_wire(token_type: IdTokenType) -> &'static str {
    match token_type {
        IdTokenType::Central => "Central",
        IdTokenType::EMaid => "eMAID",
        IdTokenType::MacAddress => "MacAddress",
        IdTokenType::Iso14443 => "ISO14443",
        IdTokenType::Iso15693 => "ISO15693",
        IdTokenType::KeyCode => "KeyCode",
        IdTokenType::Local => "Local",
        IdTokenType::NoAuthorization => "NoAuthorization",
    }
}

/// The identity a free charging deployment authorizes itself with.
///
/// `evse/evse_managerImpl.cpp:151-158`: an RFID authorization carrying a local
/// token that reads `FREESERVICE`, declared already validated so nothing is
/// asked of the authorization module. The value reaches the metering
/// transaction as its identification data, which is why it is that string and
/// not a nicer one.
///
/// Built here rather than at the config boundary so the payload and the three
/// facts derived beside it cannot disagree, which is the whole reason `IdTag`
/// has one constructor.
pub fn free_service_token() -> IdTag {
    const FREE_SERVICE: &str = "FREESERVICE";
    IdTag::new(
        serde_json::json!({
            "id_token": {
                "value": FREE_SERVICE,
                "type": token_type_on_the_wire(IdTokenType::Local),
            },
            "authorization_type": "RFID",
            "prevalidated": true,
        }),
        FREE_SERVICE.to_owned(),
        IdTokenType::Local,
        false,
    )
}

/// A token as the boundary would build one, with the payload agreeing with the
/// three facts derived beside it.
///
/// The one fixture, shared across the crate's test modules, because a fixture
/// that let those three disagree with the payload would test a token the
/// boundary cannot produce.
#[cfg(test)]
pub(crate) fn typed_id_tag_for_tests(
    value: &str,
    token_type: IdTokenType,
    plug_and_charge: bool,
) -> IdTag {
    let authorization_type = if plug_and_charge {
        "PlugAndCharge"
    } else {
        "RFID"
    };
    IdTag::new(
        serde_json::json!({
            "id_token": { "value": value, "type": token_type_on_the_wire(token_type) },
            "authorization_type": authorization_type,
        }),
        value.to_owned(),
        token_type,
        plug_and_charge,
    )
}

/// The same fixture for the tests that do not care which credential was
/// presented, which is most of them.
///
/// A card, because that is what an external identification arrives as in the
/// suite that bills these transactions, and because a fixture that defaulted to
/// `UNDEFINED` would agree with the defect this file exists to keep fixed.
#[cfg(test)]
pub(crate) fn id_tag_for_tests(value: &str, plug_and_charge: bool) -> IdTag {
    typed_id_tag_for_tests(value, IdTokenType::Iso14443, plug_and_charge)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn id_tag(value: &str, plug_and_charge: bool) -> IdTag {
        id_tag_for_tests(value, plug_and_charge)
    }

    #[test]
    fn the_payload_is_carried_whole() {
        let tag = id_tag("DEADBEEF", false);
        assert_eq!(tag.payload()["id_token"]["type"], "ISO14443");
        assert_eq!(tag.payload()["authorization_type"], "RFID");
    }

    #[test]
    fn the_three_decided_facts_are_readable_without_the_payload() {
        assert_eq!(id_tag("DEADBEEF", false).value(), "DEADBEEF");
        assert_eq!(id_tag("DEADBEEF", false).token_type(), IdTokenType::Iso14443);
        assert!(!id_tag("DEADBEEF", false).is_plug_and_charge());
        assert!(id_tag("CONTRACT", true).is_plug_and_charge());
    }

    #[test]
    fn every_token_type_is_carried_and_agrees_with_the_payload() {
        // The whole point of the field: eight credential kinds reach the core
        // and each one keeps its own identity, rather than collapsing to the
        // two a contract-or-not question can tell apart.
        for token_type in EVERY_TOKEN_TYPE {
            let tag = typed_id_tag_for_tests("ID", token_type, false);

            assert_eq!(tag.token_type(), token_type);
            assert_eq!(
                tag.payload()["id_token"]["type"],
                token_type_on_the_wire(token_type),
                "{token_type:?}"
            );
        }
    }

    #[test]
    fn the_enumeration_names_each_value_once() {
        // An array cannot be made exhaustive by the compiler, so this is what
        // keeps a duplicated entry from standing in for a missing one and
        // letting a loop over it claim to have driven eight cases.
        let mut spellings: Vec<&str> = EVERY_TOKEN_TYPE
            .iter()
            .copied()
            .map(token_type_on_the_wire)
            .collect();
        spellings.sort_unstable();
        let named = spellings.len();
        spellings.dedup();

        assert_eq!(spellings.len(), named, "duplicate entry: {spellings:?}");
    }

    #[test]
    fn a_contract_token_is_not_the_only_one_that_is_plug_and_charge_capable() {
        // `token_type` and `is_plug_and_charge` answer different questions:
        // `id_token.type` and `authorization_type`. The C++ bills on the first
        // and gates the vehicle's authorization loop on the second, so a
        // fixture that tied them together would hide the field this task adds.
        let contract_over_eim = typed_id_tag_for_tests("EMAID", IdTokenType::EMaid, false);
        assert_eq!(contract_over_eim.token_type(), IdTokenType::EMaid);
        assert!(!contract_over_eim.is_plug_and_charge());

        let card_over_plug_and_charge =
            typed_id_tag_for_tests("CARD", IdTokenType::Iso14443, true);
        assert_eq!(card_over_plug_and_charge.token_type(), IdTokenType::Iso14443);
        assert!(card_over_plug_and_charge.is_plug_and_charge());
    }
}
