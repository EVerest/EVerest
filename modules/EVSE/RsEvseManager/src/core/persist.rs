// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Session persistence, so a transaction interrupted by a power loss is closed
//! on the next boot instead of staying open forever in the billing meter.
//!
//! Ported from `PersistentStore` (`PersistentStore.cpp/.hpp`). That class keeps
//! one string, the session uuid, under one key, and everything else about
//! recovery is the two lifecycle points that write it and the startup read that
//! consumes it.

use crate::core::effect::Effect;

/// The suffix `PersistentStore`'s constructor appends to the module id
/// (`PersistentStore.cpp:15`).
const KEY_SUFFIX: &str = "_session";

/// The uuid of the transaction currently open, and the one recovered from the
/// previous run.
///
/// One type owns both halves because they share the key: the boundary has to
/// read the same name the core later writes, and a key formula duplicated on
/// the two sides of that read is a recovery that silently stops recovering.
/// `key` is therefore derived once, by [`SessionStore::key_for`], which both
/// sides call.
#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SessionStore {
    key: String,
    recovered: Option<String>,
}

impl SessionStore {
    /// The key for a module id. `PersistentStore` builds it in its constructor
    /// and never again, so it is a function of the id alone.
    ///
    /// The id is passed through unaltered, as the C++ passes `info.id`
    /// (`EvseManager.cpp:128`). `interfaces/kvs.yaml` constrains keys to
    /// `^[A-Za-z0-9_.]+$`, so a module id outside that set is rejected by the
    /// store in the C++ too; sanitizing here would write to a different key
    /// than the C++ did and lose the record across a migration.
    pub fn key_for(module_id: &str) -> String {
        format!("{module_id}{KEY_SUFFIX}")
    }

    /// `recovered` is what the store held at startup.
    ///
    /// The empty string is folded into `None` here, once, because that is what
    /// `PersistentStore::get_session` means by it: an absent key, a value that
    /// is not a string, and a load that threw all return `{}`, and both readers
    /// test `not session_uuid.empty()` (`EvseManager.cpp:1479`,
    /// `Charger.cpp:1481`). Folding it at construction is what keeps every
    /// later reader from having to remember that an empty uuid is not a uuid.
    pub fn new(module_id: &str, recovered: Option<String>) -> Self {
        Self {
            key: Self::key_for(module_id),
            recovered: recovered.filter(|uuid| !uuid.is_empty()),
        }
    }

    /// `PersistentStore::store_session` (`PersistentStore.cpp:18-22`).
    pub fn store(&self, session_uuid: String) -> Effect {
        Effect::Persist {
            key: self.key.clone(),
            value: session_uuid,
        }
    }

    /// `PersistentStore::clear_session` (`PersistentStore.cpp:24-28`).
    ///
    /// The C++ writes the empty string where this deletes the key. The only
    /// reader is `get_session`, which cannot tell the two apart: it answers
    /// `{}` for a missing key and for an empty value alike. Deleting says the
    /// same thing without leaving a record behind that means "no record".
    pub fn clear(&self) -> Effect {
        Effect::PersistDelete {
            key: self.key.clone(),
        }
    }

    /// The uuid left by the previous run, taken so recovery runs once.
    ///
    /// Taken rather than read because the record is consumed by the recovery
    /// that clears it: the C++ reads the store twice
    /// (`EvseManager.cpp:1478`, `Charger.cpp:1480`) but clears it in between,
    /// so a second startup pass would find nothing.
    pub fn take_recovered(&mut self) -> Option<String> {
        self.recovered.take()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_key_is_the_module_id_and_the_cpp_suffix() {
        assert_eq!(
            SessionStore::key_for("evse_manager"),
            "evse_manager_session"
        );
        assert_eq!(
            SessionStore::new("connector_1", None).key,
            "connector_1_session"
        );
    }

    #[test]
    fn both_sides_of_the_startup_read_derive_the_same_key() {
        let store = SessionStore::new("evse_manager", None);
        let stored = store.store("uuid".into());
        let cleared = store.clear();
        let (Effect::Persist { key: written, .. }, Effect::PersistDelete { key: removed }) =
            (&stored, &cleared)
        else {
            panic!("got {stored:?} and {cleared:?}");
        };
        assert_eq!(written, &SessionStore::key_for("evse_manager"));
        assert_eq!(removed, &SessionStore::key_for("evse_manager"));
    }

    /// `get_session` answers `{}` for an absent key, a non-string value and a
    /// load that threw, and both call sites test emptiness. So the empty string
    /// is not a session, and nothing downstream should have to know that.
    #[test]
    fn an_empty_recovered_uuid_is_no_record() {
        assert_eq!(
            SessionStore::new("evse_manager", Some(String::new())).take_recovered(),
            None
        );
        assert_eq!(
            SessionStore::new("evse_manager", None).take_recovered(),
            None
        );
    }

    #[test]
    fn a_recovered_uuid_is_offered_once() {
        let mut store = SessionStore::new("evse_manager", Some("left-over".into()));
        assert_eq!(store.take_recovered().as_deref(), Some("left-over"));
        assert_eq!(store.take_recovered(), None);
    }

    #[test]
    fn the_stored_value_is_the_session_uuid() {
        assert_eq!(
            SessionStore::new("evse_manager", None).store("abc-123".into()),
            Effect::Persist {
                key: "evse_manager_session".into(),
                value: "abc-123".into(),
            }
        );
    }
}
