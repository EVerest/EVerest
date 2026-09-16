// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The data link: what the three `D-LINK_*` requests cost.
//!
//! A port of `Charger::dlink_pause`, `Charger::dlink_terminate` and
//! `Charger::dlink_error` (`Charger.cpp:2055-2116`), and of the SLAC relay the
//! same three callbacks send (`EvseManager.cpp:372-392`).
//!
//! The decision is a pure function of the request and two facts about the port,
//! so it lives here rather than inside a power path: the same three requests
//! reach an AC port and a DC port, and only the pilot half of the answer is
//! mode specific.

use crate::core::effect::SlacUpdate;

/// Which of the three data link requests arrived.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DataLinkRequest {
    /// `D-LINK_ERROR.req`. The link failed and matching must restart.
    Error,
    /// `D-LINK_PAUSE.req`. The link goes to power saving and stays matched, so
    /// the session may resume.
    Pause,
    /// `D-LINK_TERMINATE.req`. The link goes away and the session is over.
    Terminate,
}

impl DataLinkRequest {
    /// The relay the C++ sends to SLAC from the same callback that informs the
    /// charger: `EvseManager.cpp:377`, `:384` and `:391`. Unconditional there,
    /// and unconditional here.
    pub fn slac_relay(self) -> SlacUpdate {
        match self {
            DataLinkRequest::Error => SlacUpdate::DlinkError,
            DataLinkRequest::Pause => SlacUpdate::DlinkPause,
            DataLinkRequest::Terminate => SlacUpdate::DlinkTerminate,
        }
    }

    /// The verdict this request records, or `None` for one that records none.
    ///
    /// `Charger::dlink_pause` and `Charger::dlink_terminate` each write one;
    /// `Charger::dlink_error` writes nothing at all, so an error leaves the
    /// verdict of whatever came before it standing.
    pub fn verdict(self) -> Option<TerminatePause> {
        match self {
            DataLinkRequest::Error => None,
            DataLinkRequest::Pause => Some(TerminatePause::Pause),
            DataLinkRequest::Terminate => Some(TerminatePause::Terminate),
        }
    }
}

/// The verdict the vehicle's last data link request left standing:
/// `shared_context.hlc_charging_terminate_pause` (`Charger.hpp:91-95`).
///
/// It is held because the matching lifecycle is ported, and only because of
/// that. The C++ has two readers. The first, `Charger::run_state_machine`'s
/// `ChargingPausedEV` arm, drops the pilot to X1 once a verdict has arrived,
/// which both writers have already done one line after writing it, so it
/// changes nothing observable and is not ported. The second, the
/// `ChargingPausedEVSE` arm at `Charger.cpp:1020-1022`, skips
/// `signal_slac_start` when the verdict was a pause, and that signal now has an
/// effect here: `HlcPort::on_session_resume`.
#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
pub enum TerminatePause {
    /// No verdict since the session, or the charge, last began. The C++ writes
    /// it at the `Idle` entry (`Charger.cpp:236`) and again at the `Charging`
    /// entry (`:804`), so a pause is judged by the verdict of the charge it
    /// interrupted and not by an earlier one.
    #[default]
    Unknown,
    /// `Charger::dlink_terminate`. The link is gone and the vehicle is
    /// unmatched.
    Terminate,
    /// `Charger::dlink_pause`. The link is in power saving and still matched,
    /// so a resume needs no new matching.
    Pause,
}

impl TerminatePause {
    /// Whether a resume out of the EVSE's own pause owes SLAC a wake up.
    ///
    /// `Charger.cpp:1021` tests for a pause and nothing else, so the unknown
    /// verdict wakes SLAC as a terminate does.
    pub fn wakes_slac(self) -> bool {
        self != TerminatePause::Pause
    }
}

/// The pilot route a data link request owes.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum DlinkPilot {
    /// Nothing on the pilot.
    ///
    /// Two distinct reasons in the C++, both arriving at the same answer.
    /// `[V2G3-M07-04]` covers a request received with the pilot already in X1:
    /// the transition it demands is X1 to E/F to X1 or X2, and the state
    /// machine is already running it. `[V2G3-M07-12]` is the option the C++
    /// picks for nominal duty cycle mode, which is not to interrupt basic AC
    /// charging because high level communication failed
    /// (`Charger.cpp:2109-2113`).
    None,
    /// Signal X1 and nothing more. `Charger.cpp:2058` and `:2066`.
    X1,
    /// `[V2G3-M07-05]` through `[V2G3-M07-09]`: X1, then E/F held for
    /// `T_STEP_EF`, then back to waiting for authentication with the offer
    /// withdrawn. `Charger.cpp:2095-2108`.
    ///
    /// The C++ notes at `:2088-2091` that it does not wait for SLAC to report
    /// unmatched in X1 and runs an ordinary `T_STEP_X1` instead, which it calls
    /// sufficient for the SLAC module to reset. That shortcut is ported as is;
    /// the alternative is a wait on a fact this module does not receive.
    RestartThroughX1ThenEf,
}

/// The decision, from the request and the two facts the C++ branches on.
///
/// All three requests withdraw the high level communication half of the
/// contactor permission first (`Charger.cpp:2057`, `:2065`, `:2073`), which is
/// unconditional and therefore not part of what this decides. What it decides
/// is the pilot route, the only part that reads the port's state.
///
/// `pwm_running` is `shared_context.pwm_running` and `five_percent` is
/// `hlc_use_5percent_current_session`. Neither is read by the pause or the
/// terminate arm, which is why those two are unconditional.
pub fn pilot_for(request: DataLinkRequest, pwm_running: bool, five_percent: bool) -> DlinkPilot {
    match request {
        DataLinkRequest::Pause | DataLinkRequest::Terminate => DlinkPilot::X1,
        DataLinkRequest::Error if pwm_running && five_percent => {
            DlinkPilot::RestartThroughX1ThenEf
        }
        DataLinkRequest::Error => DlinkPilot::None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pilots(request: DataLinkRequest) -> Vec<DlinkPilot> {
        [(false, false), (false, true), (true, false), (true, true)]
            .into_iter()
            .map(|(pwm, five)| pilot_for(request, pwm, five))
            .collect()
    }

    #[test]
    fn a_pause_and_a_terminate_signal_x1_whatever_the_pilot_is_doing() {
        for request in [DataLinkRequest::Pause, DataLinkRequest::Terminate] {
            for pilot in pilots(request) {
                assert_eq!(pilot, DlinkPilot::X1, "{request:?}");
            }
        }
    }

    #[test]
    fn a_data_link_error_restarts_the_session_only_from_a_running_five_percent_offer() {
        // The one arm of `Charger::dlink_error` that does anything on the
        // pilot, `Charger.cpp:2084-2108`.
        assert_eq!(
            pilot_for(DataLinkRequest::Error, true, true),
            DlinkPilot::RestartThroughX1ThenEf
        );
        // `[V2G3-M07-12]`: a nominal duty cycle session keeps charging.
        assert_eq!(
            pilot_for(DataLinkRequest::Error, true, false),
            DlinkPilot::None
        );
        // `[V2G3-M07-04]`: the pilot is already in X1.
        assert_eq!(
            pilot_for(DataLinkRequest::Error, false, true),
            DlinkPilot::None
        );
        assert_eq!(
            pilot_for(DataLinkRequest::Error, false, false),
            DlinkPilot::None
        );
    }

    #[test]
    fn each_request_relays_its_own_name_to_slac() {
        assert_eq!(DataLinkRequest::Error.slac_relay(), SlacUpdate::DlinkError);
        assert_eq!(DataLinkRequest::Pause.slac_relay(), SlacUpdate::DlinkPause);
        assert_eq!(
            DataLinkRequest::Terminate.slac_relay(),
            SlacUpdate::DlinkTerminate
        );
    }

    /// `Charger::dlink_pause` and `Charger::dlink_terminate` each write a
    /// verdict as their first statement and `Charger::dlink_error` writes none,
    /// so an error leaves whatever came before it standing.
    #[test]
    fn only_the_pause_and_the_terminate_record_a_verdict() {
        assert_eq!(DataLinkRequest::Error.verdict(), None);
        assert_eq!(
            DataLinkRequest::Pause.verdict(),
            Some(TerminatePause::Pause)
        );
        assert_eq!(
            DataLinkRequest::Terminate.verdict(),
            Some(TerminatePause::Terminate)
        );
    }

    /// `Charger.cpp:1021` tests for a pause and for nothing else, so the
    /// unknown verdict wakes SLAC exactly as a terminate does. Written over
    /// every verdict rather than over the two that differ, so a new one has to
    /// choose a side here.
    #[test]
    fn every_verdict_but_a_pause_wakes_slac() {
        for verdict in [
            TerminatePause::Unknown,
            TerminatePause::Terminate,
            TerminatePause::Pause,
        ] {
            assert_eq!(
                verdict.wakes_slac(),
                verdict != TerminatePause::Pause,
                "{verdict:?}"
            );
        }
    }

    /// A port that has heard nothing owes a wake up, because a link that never
    /// paused was never matched either.
    #[test]
    fn the_verdict_a_session_starts_with_is_unknown() {
        assert_eq!(TerminatePause::default(), TerminatePause::Unknown);
    }
}
