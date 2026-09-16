// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Availability arbitration: which source last decided that this EVSE is
//! enabled or disabled, and what that decision asks the rest of the module to
//! do.
//!
//! A port of `enable_disable`, `parse_enable_disable_source_table` and
//! `enable_disable_source_table_update` at
//! `modules/EVSE/EvseManager/Charger.cpp:1685-1869`.

use super::session::{EnableScope, SessionEvent, SessionPhase};

/// The lowest priority the interface admits, and the priority of the seeded
/// placeholder entry. Ported from `Charger.cpp:1687` and `:1811`.
pub const LOWEST_PRIORITY: i64 = 10_000;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EnableSource {
    Unspecified,
    LocalApi,
    LocalKeyLock,
    ServiceTechnician,
    RemoteKeyLock,
    MobileApp,
    FirmwareUpdate,
    Csms,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum EnableState {
    Unassigned,
    Disable,
    Enable,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EnableEntry {
    pub source: EnableSource,
    pub state: EnableState,
    pub priority: i64,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct EnableDecision {
    pub enabled: bool,
    pub active_source: EnableEntry,
    pub event: Option<SessionEvent>,
    pub connector_enabled: Option<bool>,
    pub next_phase: Option<SessionPhase>,
}

/// The source table plus the decision it currently yields.
///
/// The C++ takes `state_machine_mutex` on every path in here
/// (`Charger.cpp:1686`, `:1705`) and `enable_disable_source_table_update` is
/// marked `// already locked`. None of that is ported. One writer owns this
/// table, so there is nothing to serialize and no lock to time out.
#[derive(Debug)]
pub struct EnableTable {
    entries: Vec<EnableEntry>,
    active: EnableEntry,
    connector_enabled: bool,
}

impl Default for EnableTable {
    fn default() -> Self {
        Self::new()
    }
}

impl EnableTable {
    pub fn new() -> Self {
        Self {
            entries: Vec::new(),
            active: EnableEntry {
                source: EnableSource::Unspecified,
                state: EnableState::Unassigned,
                priority: LOWEST_PRIORITY,
            },
            connector_enabled: true,
        }
    }

    /// Who last decided, which is what a published `Enabled` or `Disabled`
    /// session event reports as its source.
    ///
    /// This read is race free by construction rather than by a lock: the borrow
    /// checker will not hand out this `&self` while `update` holds `&mut self`,
    /// so no reader can observe a half written decision. The C++ equivalent
    /// `get_last_enable_disable_source` at `Charger.cpp:2299-2301` returns
    /// `active_enable_disable_source` by value taking no lock at all, while
    /// `parse_enable_disable_source_table` writes that same member under
    /// `state_machine_mutex` from the state machine thread. Its one production
    /// caller, `evse/evse_managerImpl.cpp:341`, reads it from the session event
    /// signal handler, so the read and the write are unsynchronized and the
    /// published source can be stale or torn.
    pub fn active_source(&self) -> EnableEntry {
        self.active
    }

    pub fn connector_enabled(&self) -> bool {
        self.connector_enabled
    }

    pub fn entries(&self) -> &[EnableEntry] {
        &self.entries
    }

    /// Seed the table from the state the module boots in and announce it.
    ///
    /// Bypasses arbitration on purpose: the announced state and the active
    /// source are the boot state, assigned directly, not the outcome of a
    /// resolve over the table. Ported from
    /// `enable_disable_initial_state_publish` at `Charger.cpp:1685-1702`.
    pub fn publish_initial_state(&mut self, disabled_at_startup: bool) -> SessionEvent {
        let (state, event) = if disabled_at_startup {
            (EnableState::Disable, SessionEvent::Disabled)
        } else {
            (EnableState::Enable, SessionEvent::Enabled)
        };
        let seed = EnableEntry {
            source: EnableSource::Unspecified,
            state,
            priority: LOWEST_PRIORITY,
        };
        self.record(seed);
        self.active = seed;
        event
    }

    /// Record one source's report, arbitrate, and say what follows.
    ///
    /// Ported from `enable_disable` at `Charger.cpp:1704-1773` minus the lock at
    /// `:1705` and minus driving the state machine at `:1770`. Running the state
    /// machine is the caller's, so that this stays a decision and not an
    /// actuation.
    pub fn update(
        &mut self,
        entry: EnableEntry,
        scope: EnableScope,
        phase: SessionPhase,
    ) -> EnableDecision {
        let previous = self.active;

        self.record(entry);
        let (enabled, winner) = self.resolve();
        self.active = winner;

        // The asymmetry. Arbitration above and the announced event below happen
        // for either scope; only connector state is scope gated. A disable
        // therefore takes effect EVSE wide while an enable restores connector
        // state only for a connector scoped request. Ported from
        // `Charger.cpp:1720-1724`.
        let connector_enabled = if scope.updates_connector_state() {
            self.connector_enabled = enabled;
            Some(enabled)
        } else {
            None
        };

        // Change detection, ported from the truth table at
        // `Charger.cpp:1740-1755`. Unassigned and Enable are equivalent here,
        // which reduces the nine cases to: one of the two states is Disable and
        // they differ.
        let changed = (self.active.state == EnableState::Disable
            || previous.state == EnableState::Disable)
            && self.active.state != previous.state;

        let mut event = None;
        let mut next_phase = None;
        if changed {
            if enabled {
                event = Some(SessionEvent::Enabled);
                // The C++ clears its disable flag, re-enables the BSP and moves
                // Disabled to Idle at `:1726-1731`. Only a connector scoped
                // request can get there, because the transition is guarded on
                // connector state.
                if self.connector_enabled {
                    next_phase = Some(SessionPhase::Idle);
                }
            } else {
                event = Some(SessionEvent::Disabled);
                // A live session is stopped, not torn down. The C++ arms
                // `flag_disable_requested` and every active charging state
                // routes itself to StoppingCharging rather than dropping power,
                // `Charger.cpp:1756-1769`. Stopping is that route, and the
                // caller records `StopReason::EvseDisabled` for it, which is the
                // reason the C++ stores at `:1765`.
                //
                // An idle EVSE has no session to stop and enters Disabled
                // directly, which is what the Idle state does with the same flag
                // at `Charger.cpp:236-238`.
                next_phase = match phase {
                    SessionPhase::WaitingForAuthorization
                    | SessionPhase::Authorized
                    | SessionPhase::Charging => Some(SessionPhase::Stopping),
                    SessionPhase::Idle => Some(SessionPhase::Disabled),
                    SessionPhase::Disabled
                    | SessionPhase::Reserved
                    | SessionPhase::Stopping
                    | SessionPhase::Finished => None,
                };
            }
        }

        EnableDecision {
            enabled,
            active_source: self.active,
            event,
            connector_enabled,
            next_phase,
        }
    }

    /// Decide enablement from the current table without mutating it.
    ///
    /// Ported from `parse_enable_disable_source_table` at
    /// `Charger.cpp:1776-1853`. Unassigned entries are skipped, the lowest
    /// priority value wins, and at an exact tie a Disable displaces the standing
    /// winner while an Enable leaves it alone. That last rule is why this is not
    /// a plain `min_by_key`: a tied Enable must not take the winner slot even
    /// though its priority compares equal.
    pub fn resolve(&self) -> (bool, EnableEntry) {
        let mut enabled = true; // By default, it is enabled.
        let mut winner = EnableEntry {
            source: EnableSource::Unspecified,
            state: EnableState::Unassigned,
            priority: LOWEST_PRIORITY,
        };

        for entry in &self.entries {
            if entry.state == EnableState::Unassigned {
                continue;
            }

            if winner.state == EnableState::Unassigned {
                // First assigned entry seen, whatever its priority.
                winner = *entry;
                enabled = entry.state != EnableState::Disable;
            } else if entry.priority == winner.priority {
                if entry.state == EnableState::Disable {
                    winner = *entry;
                    enabled = false;
                }
            } else if entry.priority < winner.priority {
                winner = *entry;
                enabled = entry.state != EnableState::Disable;
            }
        }

        (enabled, winner)
    }

    /// One row per source: a repeated report overwrites in place, so the table
    /// cannot grow duplicates and is bounded by the number of sources. Ported
    /// from `enable_disable_source_table_update` at `Charger.cpp:1855-1868`,
    /// whose `// already locked` comment has no counterpart here.
    fn record(&mut self, entry: EnableEntry) {
        match self
            .entries
            .iter()
            .position(|existing| existing.source == entry.source)
        {
            Some(at) => self.entries[at] = entry,
            None => self.entries.push(entry),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(source: EnableSource, state: EnableState, priority: i64) -> EnableEntry {
        EnableEntry {
            source,
            state,
            priority,
        }
    }

    fn report(
        table: &mut EnableTable,
        source: EnableSource,
        state: EnableState,
        priority: i64,
    ) -> EnableDecision {
        table.update(
            entry(source, state, priority),
            EnableScope::Connector,
            SessionPhase::Idle,
        )
    }

    #[test]
    fn an_empty_table_is_enabled() {
        let table = EnableTable::new();
        let (enabled, winner) = table.resolve();
        assert!(enabled);
        assert_eq!(winner.state, EnableState::Unassigned);
        assert_eq!(winner.priority, LOWEST_PRIORITY);
    }

    #[test]
    fn a_table_of_nothing_but_unassigned_entries_is_enabled() {
        let mut table = EnableTable::new();
        report(
            &mut table,
            EnableSource::LocalKeyLock,
            EnableState::Unassigned,
            0,
        );
        report(&mut table, EnableSource::Csms, EnableState::Unassigned, 1);
        assert!(table.resolve().0);
        assert_eq!(table.active_source().state, EnableState::Unassigned);
    }

    #[test]
    fn the_lowest_priority_value_wins() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 42);
        report(&mut table, EnableSource::Csms, EnableState::Enable, 7);
        let (enabled, winner) = table.resolve();
        assert!(enabled);
        assert_eq!(winner.source, EnableSource::Csms);
    }

    #[test]
    fn a_higher_priority_value_does_not_displace_the_winner() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::Csms, EnableState::Enable, 7);
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 42);
        let (enabled, winner) = table.resolve();
        assert!(enabled);
        assert_eq!(winner.source, EnableSource::Csms);
    }

    #[test]
    fn an_unassigned_entry_at_the_best_priority_lets_the_next_source_decide() {
        // The worked example in the C++ comment at Charger.cpp:1798-1812.
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 42);
        report(
            &mut table,
            EnableSource::LocalKeyLock,
            EnableState::Unassigned,
            0,
        );
        let (enabled, winner) = table.resolve();
        assert!(!enabled);
        assert_eq!(winner.source, EnableSource::LocalApi);
    }

    #[test]
    fn an_assigned_entry_at_the_lowest_priority_still_becomes_the_active_source() {
        // The placeholder winner sits at the lowest priority too, so an entry
        // that ties with it only takes the slot because the first assigned entry
        // seen wins outright. Ported from Charger.cpp:1821-1828.
        let mut table = EnableTable::new();
        let decision = report(
            &mut table,
            EnableSource::Csms,
            EnableState::Enable,
            LOWEST_PRIORITY,
        );
        assert!(decision.enabled);
        assert_eq!(table.active_source().source, EnableSource::Csms);
        assert_eq!(table.active_source().state, EnableState::Enable);
    }

    #[test]
    fn a_disable_beats_an_enable_at_the_same_priority() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::Csms, EnableState::Enable, 5);
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 5);
        let (enabled, winner) = table.resolve();
        assert!(!enabled);
        assert_eq!(winner.source, EnableSource::LocalApi);
    }

    #[test]
    fn an_enable_at_the_same_priority_leaves_a_winning_disable_alone() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 5);
        report(&mut table, EnableSource::Csms, EnableState::Enable, 5);
        let (enabled, winner) = table.resolve();
        assert!(!enabled);
        assert_eq!(winner.source, EnableSource::LocalApi);
    }

    #[test]
    fn an_enable_at_the_same_priority_leaves_a_winning_enable_alone() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::Csms, EnableState::Enable, 5);
        report(&mut table, EnableSource::MobileApp, EnableState::Enable, 5);
        let (enabled, winner) = table.resolve();
        assert!(enabled);
        assert_eq!(winner.source, EnableSource::Csms);
    }

    #[test]
    fn a_repeated_report_from_one_source_overwrites_its_entry_in_place() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::LocalApi, EnableState::Disable, 5);
        report(&mut table, EnableSource::LocalApi, EnableState::Enable, 5);
        assert_eq!(table.entries().len(), 1);
        assert!(table.resolve().0);
    }

    #[test]
    fn repeating_one_source_many_times_never_grows_the_table() {
        let mut table = EnableTable::new();
        for priority in 0..20 {
            report(
                &mut table,
                EnableSource::Csms,
                EnableState::Disable,
                priority,
            );
        }
        assert_eq!(table.entries().len(), 1);
        assert_eq!(table.entries()[0].priority, 19);
    }

    #[test]
    fn every_source_keeps_its_own_row() {
        let mut table = EnableTable::new();
        for (index, source) in [
            EnableSource::Unspecified,
            EnableSource::LocalApi,
            EnableSource::LocalKeyLock,
            EnableSource::ServiceTechnician,
            EnableSource::RemoteKeyLock,
            EnableSource::MobileApp,
            EnableSource::FirmwareUpdate,
            EnableSource::Csms,
        ]
        .into_iter()
        .enumerate()
        {
            report(&mut table, source, EnableState::Enable, index as i64 + 1);
        }
        assert_eq!(table.entries().len(), 8);
    }

    #[test]
    fn a_first_disable_reports_a_state_change() {
        let mut table = EnableTable::new();
        let decision = report(&mut table, EnableSource::Csms, EnableState::Disable, 5);
        assert!(!decision.enabled);
        assert_eq!(decision.event, Some(SessionEvent::Disabled));
    }

    #[test]
    fn a_first_enable_reports_no_state_change() {
        // Unassigned and Enable are equivalent for change detection, so the
        // startup default of enabled is not re-announced.
        let mut table = EnableTable::new();
        let decision = report(&mut table, EnableSource::Csms, EnableState::Enable, 5);
        assert!(decision.enabled);
        assert_eq!(decision.event, None);
    }

    #[test]
    fn a_repeated_disable_reports_no_further_state_change() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::Csms, EnableState::Disable, 5);
        let decision = report(&mut table, EnableSource::Csms, EnableState::Disable, 5);
        assert!(!decision.enabled);
        assert_eq!(decision.event, None);
    }

    #[test]
    fn leaving_a_disable_reports_a_state_change() {
        let mut table = EnableTable::new();
        report(&mut table, EnableSource::Csms, EnableState::Disable, 5);
        let decision = report(&mut table, EnableSource::Csms, EnableState::Unassigned, 5);
        assert!(decision.enabled);
        assert_eq!(decision.event, Some(SessionEvent::Enabled));
    }

    #[test]
    fn a_connector_scoped_request_assigns_connector_state() {
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        assert_eq!(decision.connector_enabled, Some(false));
        assert!(!table.connector_enabled());
    }

    #[test]
    fn an_evse_scoped_request_leaves_connector_state_untouched() {
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Evse,
            SessionPhase::Idle,
        );
        assert_eq!(decision.connector_enabled, None);
        assert!(table.connector_enabled());
    }

    #[test]
    fn an_evse_scoped_disable_still_decides_and_still_announces() {
        // The asymmetry: connector state is skipped but arbitration and the
        // announced event are not. Ported from Charger.cpp:1718-1724.
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Evse,
            SessionPhase::Idle,
        );
        assert!(!decision.enabled);
        assert_eq!(decision.event, Some(SessionEvent::Disabled));
        assert_eq!(table.active_source().source, EnableSource::Csms);
    }

    #[test]
    fn an_evse_scoped_enable_does_not_restore_a_disabled_connector() {
        let mut table = EnableTable::new();
        table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        assert!(!table.connector_enabled());
        table.update(
            entry(EnableSource::Csms, EnableState::Enable, 5),
            EnableScope::Evse,
            SessionPhase::Idle,
        );
        assert!(
            !table.connector_enabled(),
            "an EVSE scoped enable leaves the connector disabled"
        );
    }

    #[test]
    fn re_enabling_a_connector_returns_the_session_to_idle() {
        let mut table = EnableTable::new();
        table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Enable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        assert!(decision.enabled);
        assert_eq!(decision.event, Some(SessionEvent::Enabled));
        assert_eq!(decision.next_phase, Some(SessionPhase::Idle));
    }

    #[test]
    fn an_evse_scoped_enable_of_a_disabled_connector_does_not_return_to_idle() {
        // The re-enable transition is guarded on connector state, which an EVSE
        // scoped request never assigns, so it stays disabled. Ported from
        // Charger.cpp:1726.
        let mut table = EnableTable::new();
        table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Enable, 5),
            EnableScope::Evse,
            SessionPhase::Idle,
        );
        assert_eq!(decision.event, Some(SessionEvent::Enabled));
        assert_eq!(decision.next_phase, None);
    }

    #[test]
    fn a_winning_disable_routes_an_active_session_through_stopping() {
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Charging,
        );
        assert_eq!(decision.next_phase, Some(SessionPhase::Stopping));
    }

    #[test]
    fn a_winning_disable_routes_an_authorized_session_through_stopping() {
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Authorized,
        );
        assert_eq!(decision.next_phase, Some(SessionPhase::Stopping));
    }

    #[test]
    fn a_winning_disable_takes_an_idle_evse_straight_to_disabled() {
        // There is no session to stop, so the C++ Idle state acts on the flag
        // itself and enters Disabled (`Charger.cpp:236-238`).
        let mut table = EnableTable::new();
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Idle,
        );
        assert_eq!(decision.next_phase, Some(SessionPhase::Disabled));
    }

    #[test]
    fn a_winning_disable_in_a_settled_state_routes_nowhere() {
        for phase in [
            SessionPhase::Disabled,
            SessionPhase::Reserved,
            SessionPhase::Stopping,
            SessionPhase::Finished,
        ] {
            let mut table = EnableTable::new();
            let decision = table.update(
                entry(EnableSource::Csms, EnableState::Disable, 5),
                EnableScope::Connector,
                phase,
            );
            assert_eq!(decision.next_phase, None, "{phase:?}");
        }
    }

    #[test]
    fn a_losing_disable_leaves_an_active_session_charging() {
        let mut table = EnableTable::new();
        report(
            &mut table,
            EnableSource::LocalKeyLock,
            EnableState::Enable,
            0,
        );
        let decision = table.update(
            entry(EnableSource::Csms, EnableState::Disable, 5),
            EnableScope::Connector,
            SessionPhase::Charging,
        );
        assert!(decision.enabled);
        assert_eq!(decision.next_phase, None);
        assert_eq!(decision.event, None);
    }

    #[test]
    fn a_disabled_startup_seeds_the_table_with_a_disable() {
        let mut table = EnableTable::new();
        let event = table.publish_initial_state(true);
        assert_eq!(event, SessionEvent::Disabled);
        assert_eq!(table.entries().len(), 1);
        assert_eq!(table.active_source().state, EnableState::Disable);
        assert_eq!(table.active_source().source, EnableSource::Unspecified);
        assert_eq!(table.active_source().priority, LOWEST_PRIORITY);
    }

    #[test]
    fn an_enabled_startup_seeds_the_table_with_an_enable() {
        let mut table = EnableTable::new();
        let event = table.publish_initial_state(false);
        assert_eq!(event, SessionEvent::Enabled);
        assert_eq!(table.active_source().state, EnableState::Enable);
    }

    #[test]
    fn the_startup_publish_bypasses_arbitration() {
        // A disabled startup announces Disabled and records it as active even
        // though the seeded entry sits at the lowest priority and a resolve of
        // the same table would agree only by coincidence. What proves the bypass
        // is that the active source is assigned directly rather than through
        // resolve, so a better priority entry already present does not override
        // it. Ported from Charger.cpp:1690-1701.
        let mut table = EnableTable::new();
        report(
            &mut table,
            EnableSource::LocalKeyLock,
            EnableState::Enable,
            0,
        );
        let event = table.publish_initial_state(true);
        assert_eq!(event, SessionEvent::Disabled);
        assert_eq!(table.active_source().source, EnableSource::Unspecified);
        assert_eq!(table.active_source().state, EnableState::Disable);
        assert!(
            table.resolve().0,
            "arbitration on its own would have said enabled"
        );
    }

    #[test]
    fn the_startup_publish_overwrites_its_own_seed_in_place() {
        let mut table = EnableTable::new();
        table.publish_initial_state(true);
        table.publish_initial_state(false);
        assert_eq!(table.entries().len(), 1);
    }
}
