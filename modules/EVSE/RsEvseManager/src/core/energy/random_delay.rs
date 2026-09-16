// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The UK smart charging random delay.
//!
//! UK regulation requires a charge point to spread a load step over a random
//! interval, so a fleet answering one tariff signal does not step the grid
//! together. The C++ implements it inside the enforced limits handler
//! (`energy_grid/energyImpl.cpp:448-499`): while a delay runs, the limit the
//! energy manager asked for is withheld and the limit from before the change
//! is applied in its place, and a countdown is published so an operator can
//! see what is being withheld and for how long.
//!
//! Four commands on `uk_random_delay` move the feature at runtime
//! (`random_delay/uk_random_delayImpl.cpp:15-31`), and three configuration
//! keys seed it (`manifest.yaml:258-274`).
//!
//! ## The predicate that was not one
//!
//! `energyImpl::random_delay_needed` (`energyImpl.cpp:359-383`) reads as a
//! query and is not one: its startup branch assigns the member
//! `last_enforced_limit = 0.` before returning true, while the value it
//! compares is the by-value parameter. The caller reads that member four lines
//! later to decide which limit the delay holds (`:465`), and overwrites it
//! unconditionally at `:501`, so the write is dead apart from that one read.
//! Its entire meaning is therefore: *a delay that fires because the module
//! just came up with a vehicle attached holds zero, not the previous limit.*
//!
//! Here that is the return value. `Trigger` names which of the two limits a
//! due delay holds, and `hold_for` turns the name into the figure, so the
//! decision is a function of its inputs and the choice of held limit is stated
//! at the point it is made. Behavior is unchanged: the mutation had exactly
//! one reader and it is the one this replaces.

use std::time::{Duration, Instant};

use super::enforce::almost_eq;
use crate::core::path::iec::AcState;

/// `detect_startup_with_ev_attached_duration` (`energyImpl.hpp:68`). How long
/// after the module announces readiness a vehicle already at the connector
/// counts as a startup rather than as a steady state.
const STARTUP_WITH_EV_ATTACHED: Duration = Duration::from_secs(5);

/// `almost_eq(last_limit, 0.) and limit > 0.1` (`energyImpl.cpp:366`). The
/// threshold the zero crossing branches compare against, which is the same
/// figure as the `almost_eq` window and not derived from it.
const NONZERO_A: f64 = 0.1;

/// `RAND_MAX + 1`, which is what actually bounds the C++ draw whatever the
/// configured maximum says: `std::rand() % n` for an `n` above `RAND_MAX`
/// returns `std::rand()` unchanged, so no C++ delay is ever longer than
/// `RAND_MAX` seconds.
///
/// Reproduced rather than ignored. `set_duration_s` is on the bus and takes a
/// plain integer, so `set_duration_s(i64::MAX)` is reachable, and without this
/// bound it would name a deadline no `Instant` can represent: adding the
/// drawn duration panics and takes the module down, where the C++ quietly
/// caps. Sixty eight years is the same "never" the C++ reaches.
const DRAW_CEILING_S: i64 = 1 << 31;

/// What configuration says about the delay (`manifest.yaml:258-274`).
///
/// Two of the three are boot values for state the bus can move afterwards, and
/// the third is fixed for the life of the process. Named apart so a reader can
/// tell which is which without opening `RandomDelay`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct RandomDelaySettings {
    /// `uk_smartcharging_random_delay_enable`, the boot value of a flag
    /// `enable` and `disable` move (`EvseManager.cpp:130`).
    pub enabled_at_boot: bool,
    /// `uk_smartcharging_random_delay_max_duration`, the boot value of a
    /// figure `set_duration_s` moves (`EvseManager.cpp:131`).
    pub max_duration_s_at_boot: i64,
    /// `uk_smartcharging_random_delay_at_any_change`. Configuration only:
    /// nothing on the bus moves it. Defaults to true, so out of the box any
    /// change of limit delays and not only a start or a stop.
    pub at_any_change: bool,
}

/// `types::uk_random_delay::CountDown`, as this module fills it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CountDown {
    /// Seconds remaining, zero whenever no delay is running.
    pub countdown_s: i64,
    /// The limit that will apply once the delay is over, which is the limit
    /// the energy manager asked for.
    pub current_limit_after_delay_a: f64,
    /// The limit applied while the delay runs.
    pub current_limit_during_delay_a: f64,
    /// How long ago the running delay began, for the wire's `start_time`.
    ///
    /// The C++ stamps `date::utc_clock::now()` once at the start (`:461`) and
    /// formats that same stamp on every publish (`:489`). `core` reads no wall
    /// clock, so the elapsed monotonic interval travels instead and the
    /// boundary subtracts it from the wall clock it can read. The published
    /// instant is therefore recomputed per publish rather than frozen, which
    /// differs only if the wall clock is stepped mid delay, and then the
    /// recomputed answer is the better of the two.
    ///
    /// Absent exactly where the C++ leaves the optional unset: the two
    /// branches that publish a zero countdown (`:479-482` and `:492-497`) fill
    /// no `start_time`.
    pub started_ago: Option<Duration>,
}

/// The limit to apply, and what to tell the bus about it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Applied {
    /// What the charger, the cable rating cap and the republished limits all
    /// take. The requested figure, or the held one while a delay runs.
    pub limit_a: f64,
    /// The countdown to publish. `None` while the feature is disabled, which
    /// is what the C++ does: the whole block including both publishes sits
    /// inside `if (mod->random_delay_enabled)` (`:448`), so a disabled feature
    /// publishes nothing at all rather than a zero countdown.
    pub countdown: Option<CountDown>,
}

/// Which limit a due delay holds. The two arms are the C++ predicate's two
/// ways of returning true; see the module header.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Trigger {
    /// The limit changed in a way the configured policy delays. The delay
    /// holds what the port last enforced.
    LimitChanged,
    /// The module came up within the last `STARTUP_WITH_EV_ATTACHED` with a
    /// vehicle already at the connector. The delay holds zero, so a port that
    /// restarted mid session does not resume at a remembered current before
    /// its delay has run.
    StartupWithEvAttached,
}

/// A delay in progress.
///
/// A struct behind an `Option` rather than a bool beside two timestamps, which
/// is how the C++ carries it (`EvseManager.hpp:253-255`): there, the deadline
/// outlives the flag and can be read when nothing is running. Here it cannot
/// be reached without a delay to read it from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Run {
    /// `random_delay_start_time`, for the countdown's `start_time`.
    started_at: Instant,
    /// `random_delay_end_time`.
    ends_at: Instant,
}

/// The whole of the feature's state.
pub struct RandomDelay {
    /// `config.uk_smartcharging_random_delay_at_any_change`.
    at_any_change: bool,
    /// `mod->random_delay_enabled`.
    enabled: bool,
    /// `mod->random_delay_max_duration`, in seconds and signed, because
    /// `set_duration_s` takes a plain integer off the bus and the interface
    /// bounds nothing. See `draw`.
    max_duration_s: i64,
    /// `mod->random_delay_running` plus the two timestamps it guards.
    running: Option<Run>,
    /// `limit_when_random_delay_started` (`energyImpl.hpp:66`).
    ///
    /// Outlives the delay that set it, deliberately: the not running countdown
    /// publishes it too (`:496`), so what a consumer reads there is the held
    /// limit of the previous delay, and zero before the first one.
    hold_a: f64,
    /// `last_enforced_limit` (`energyImpl.hpp:65`). The limit the energy
    /// manager last asked for, which is what the next change is measured
    /// against. Written on every call, including calls made while a delay
    /// runs, which is why a running delay does not retrigger itself.
    last_enforced_a: f64,
    draws: SplitMix64,
}

impl RandomDelay {
    pub fn new(settings: RandomDelaySettings, seed: u64) -> Self {
        Self {
            at_any_change: settings.at_any_change,
            enabled: settings.enabled_at_boot,
            max_duration_s: settings.max_duration_s_at_boot,
            running: None,
            hold_a: 0.0,
            last_enforced_a: 0.0,
            draws: SplitMix64 { state: seed },
        }
    }

    /// `handle_enable` (`uk_random_delayImpl.cpp:15-18`). Enabling also cancels
    /// whatever was running, so it is a reset and not only a switch.
    pub fn enable(&mut self) {
        self.running = None;
        self.enabled = true;
    }

    /// `handle_disable` (`:20-23`).
    ///
    /// Dropping the running delay here changes nothing observable, in the C++
    /// or here: the only route back to enabled is `enable`, which drops it
    /// too. Kept because the C++ writes it and a reader comparing the two
    /// should not have to work out why one line is missing. The mutation
    /// sweep names it as an equivalent mutant rather than a coverage gap.
    pub fn disable(&mut self) {
        self.running = None;
        self.enabled = false;
    }

    /// `handle_cancel` (`:25-27`). "The effect is the same as if the time
    /// expired just now" (`interfaces/uk_random_delay.yaml`), and dropping the
    /// delay is how that is achieved: the next enforced limit finds nothing
    /// running, and `last_enforced_a` already equals the request that started
    /// the delay, so no fresh one is drawn for the same request.
    pub fn cancel(&mut self) {
        self.running = None;
    }

    /// `handle_set_duration_s` (`:29-31`). Takes the value as given, including
    /// zero and negatives, because the C++ does and because refusing here
    /// would leave the caller believing a maximum it does not have. What the
    /// out of range values mean is decided at the draw.
    pub fn set_duration_s(&mut self, seconds: i64) {
        self.max_duration_s = seconds;
    }

    /// `energyImpl.cpp:448-501`, which is the whole of the delay's effect on
    /// an enforced limit.
    ///
    /// Runs on every enforced limit, enabled or not, because the last
    /// enforced limit is recorded outside the enabled gate (`:501`): a feature
    /// enabled mid session must measure its first change against the limit
    /// that was actually in force, not against zero.
    pub fn apply(
        &mut self,
        requested_a: f64,
        state: AcState,
        ready_since: Option<Instant>,
        now: Instant,
    ) -> Applied {
        if !self.enabled {
            self.last_enforced_a = requested_a;
            return Applied {
                limit_a: requested_a,
                countdown: None,
            };
        }

        // `:451-455`. A state in which no delay makes sense drops the running
        // one. This precedes the start decision and the limit change branches
        // below do not look at state, so an unsuitable state can drop a delay
        // and start a fresh one in the same call. That is what the C++ does.
        if !delay_makes_sense(state) {
            self.running = None;
        }

        // `:459-466`.
        if self.running.is_none() {
            if let Some(trigger) = self.trigger(requested_a, state, ready_since, now) {
                let seconds = self.draw();
                self.running = Some(Run {
                    started_at: now,
                    ends_at: now + Duration::from_secs(seconds),
                });
                self.hold_a = self.hold_for(trigger);
                log::info!("UK smart charging regulations: starting random delay of {seconds}s");
            }
        }

        // `:469-498`. The held limit is taken before the deadline is checked
        // (`:471` precedes `:479`), so the call that ends a delay still
        // withholds the request and the call after it is the first to carry
        // it. Both are decided in the one match, so the limit applied and the
        // countdown published cannot disagree about whether a delay ran.
        let (limit_a, countdown) = match self.running {
            Some(run) => {
                // The C++ tests `<= 0` against a signed difference of two
                // steady clock reads. A saturating monotonic difference cannot
                // be negative, so zero is the whole of that condition.
                let left = run.ends_at.saturating_duration_since(now).as_secs();
                let countdown_s = if left == 0 {
                    log::info!("UK smart charging regulations: random delay elapsed");
                    self.running = None;
                    0
                } else {
                    log::debug!(
                        "random delay running, {left}s left; applying the limit from before \
                         the delay ({}A) instead of the requested limit ({requested_a}A)",
                        self.hold_a
                    );
                    i64::try_from(left).unwrap_or(i64::MAX)
                };
                (
                    self.hold_a,
                    CountDown {
                        countdown_s,
                        current_limit_after_delay_a: requested_a,
                        current_limit_during_delay_a: self.hold_a,
                        // Filled only while the delay still has time to run,
                        // which is where `:489` fills it.
                        started_ago: (countdown_s > 0)
                            .then(|| now.saturating_duration_since(run.started_at)),
                    },
                )
            }
            None => (
                requested_a,
                CountDown {
                    countdown_s: 0,
                    current_limit_after_delay_a: requested_a,
                    current_limit_during_delay_a: self.hold_a,
                    started_ago: None,
                },
            ),
        };

        // `:501`. The requested figure and never the held one.
        self.last_enforced_a = requested_a;
        Applied {
            limit_a,
            countdown: Some(countdown),
        }
    }

    /// `random_delay_needed` (`:359-383`), with its hidden write returned
    /// rather than performed. `None` is the C++ `false`.
    fn trigger(
        &self,
        requested_a: f64,
        state: AcState,
        ready_since: Option<Instant>,
        now: Instant,
    ) -> Option<Trigger> {
        let last_a = self.last_enforced_a;
        if self.at_any_change {
            if !almost_eq(last_a, requested_a) {
                return Some(Trigger::LimitChanged);
            }
        } else if (almost_eq(last_a, 0.0) && requested_a > NONZERO_A)
            || (last_a > NONZERO_A && almost_eq(requested_a, 0.0))
        {
            return Some(Trigger::LimitChanged);
        }

        // `:373-380`. Reached only when the branches above did not answer:
        // the C++ short circuits on their returns, so a limit change that
        // already triggered holds the previous limit even during startup.
        //
        // Before the module announces readiness the C++ compares against a
        // default constructed steady clock time point, so the interval is the
        // whole uptime of the machine and the branch is false. `None` is that.
        let within_startup = ready_since
            .is_some_and(|since| now.saturating_duration_since(since) < STARTUP_WITH_EV_ATTACHED);
        if delay_makes_sense(state) && within_startup {
            return Some(Trigger::StartupWithEvAttached);
        }
        None
    }

    /// Which limit the delay holds, which is the whole of what the C++
    /// mutation decided.
    fn hold_for(&self, trigger: Trigger) -> f64 {
        match trigger {
            Trigger::LimitChanged => self.last_enforced_a,
            // The `last_enforced_limit = 0.` at `:378`, read at `:465`.
            Trigger::StartupWithEvAttached => 0.0,
        }
    }

    /// `std::rand() % mod->random_delay_max_duration.load().count()` (`:462`).
    ///
    /// The modulus is guarded, which the C++ is not. `set_duration_s` is on
    /// the bus, takes a plain integer and bounds nothing, so a zero maximum is
    /// reachable and divides by zero there; a negative one is reachable too
    /// and silently becomes a short delay, because a non negative left operand
    /// modulo a negative divisor is non negative. Both become a zero length
    /// delay here, which is the answer a maximum of one already gives in the
    /// C++: the delay starts, withholds the request for exactly the call that
    /// started it, and reports itself elapsed.
    fn draw(&mut self) -> u64 {
        let Ok(max) = u64::try_from(self.max_duration_s.min(DRAW_CEILING_S)) else {
            return 0;
        };
        if max == 0 {
            return 0;
        }
        self.draws.next() % max
    }
}

/// The delay a tree gets in a test that is not about the delay: the manifest
/// defaults (`manifest.yaml:258-274`), which have the feature off. So every
/// such test enforces the limit the energy manager sent, unheld, and the
/// delay's own tests build their own.
///
/// Not available to production, which reads the three keys from configuration
/// and must not fall back to a literal if one is missing.
#[cfg(test)]
pub(crate) fn boot_defaults() -> RandomDelay {
    RandomDelay::new(
        RandomDelaySettings {
            enabled_at_boot: false,
            max_duration_s_at_boot: 600,
            at_any_change: true,
        },
        0,
    )
}

/// The three states in which a delay makes sense (`:374-375`, `:451-453`).
///
/// One function for both sites because the C++ writes the same disjunction
/// twice, once plain and once negated, and a port that let the two drift would
/// clear delays in a state that can still start them.
fn delay_makes_sense(state: AcState) -> bool {
    matches!(
        state,
        AcState::PrepareCharging | AcState::Charging | AcState::WaitingForAuthentication
    )
}

/// splitmix64: a counter through a strong finalizer, so a seed fixes the whole
/// sequence and nothing repeats within its period.
///
/// The C++ draws from `std::rand()`, seeded once from `time(0)`
/// (`energyImpl.cpp:32`). That seed has one second of resolution and nothing
/// per station in it, so two ports whose processes come up in the same wall
/// clock second draw the same delays, which is the load synchronization the
/// regulation exists to prevent. The seed here comes from the boundary
/// (`boundary::random::seed`) and carries the node id, and the generator lives
/// in `core` so a fixed seed makes the drawn lengths testable.
struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    fn next(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9e37_79b9_7f4a_7c15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xbf58_476d_1ce4_e5b9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94d0_49bb_1331_11eb);
        z ^ (z >> 31)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// `max_duration_s_at_boot` large enough that a drawn delay is very
    /// unlikely to be zero, so a test about holding does not silently become a
    /// test about elapsing. The draws are seeded, so "unlikely" is checked:
    /// `a_drawn_delay_is_bounded_by_the_maximum` pins the actual values.
    const MAX_S: i64 = 600;

    fn enabled(at_any_change: bool, seed: u64) -> RandomDelay {
        RandomDelay::new(
            RandomDelaySettings {
                enabled_at_boot: true,
                max_duration_s_at_boot: MAX_S,
                at_any_change,
            },
            seed,
        )
    }

    /// Applies a limit in a state that permits a delay, well past any startup
    /// window, which is the steady state the limit change branches describe.
    fn steady(delay: &mut RandomDelay, requested_a: f64, now: Instant) -> Applied {
        delay.apply(requested_a, AcState::Charging, None, now)
    }

    #[test]
    fn a_disabled_feature_publishes_no_countdown_and_holds_nothing() {
        let mut delay = RandomDelay::new(
            RandomDelaySettings {
                enabled_at_boot: false,
                max_duration_s_at_boot: MAX_S,
                at_any_change: true,
            },
            1,
        );
        let applied = steady(&mut delay, 16.0, Instant::now());
        assert_eq!(applied.limit_a, 16.0);
        // Not a zero countdown. A consumer can tell "no delay running" from
        // "the feature is not in use", which is what the C++ gate at `:448`
        // gives it.
        assert_eq!(applied.countdown, None);
    }

    /// The last enforced limit is recorded outside the enabled gate (`:501`),
    /// so a feature switched on mid session measures its first change against
    /// the limit that was actually in force. A port that recorded it only
    /// while enabled would see 16 A as a change from zero and delay a step
    /// that never happened.
    #[test]
    fn a_feature_enabled_mid_session_measures_against_the_limit_in_force() {
        let mut delay = RandomDelay::new(
            RandomDelaySettings {
                enabled_at_boot: false,
                max_duration_s_at_boot: MAX_S,
                at_any_change: true,
            },
            1,
        );
        let now = Instant::now();
        steady(&mut delay, 16.0, now);
        delay.enable();
        let applied = steady(&mut delay, 16.0, now);
        assert_eq!(applied.limit_a, 16.0, "an unchanged limit was delayed");
        assert_eq!(applied.countdown.unwrap().countdown_s, 0);
    }

    #[test]
    fn any_change_delays_every_move_of_more_than_the_almost_equal_window() {
        let mut delay = enabled(true, 2);
        let now = Instant::now();
        // Zero to sixteen: the delay holds the zero that was in force, so the
        // step up waits.
        let start = steady(&mut delay, 16.0, now);
        assert_eq!(start.limit_a, 0.0);
        assert!(start.countdown.unwrap().countdown_s > 0);

        // A move inside the window is not a change. Driven on a fresh delay
        // because the one above is still running.
        let mut small = enabled(true, 2);
        steady(&mut small, 16.0, now);
        let elapsed = now + Duration::from_secs(MAX_S as u64);
        steady(&mut small, 16.0, elapsed);
        let applied = steady(&mut small, 16.05, elapsed);
        assert_eq!(applied.limit_a, 16.05, "a move of 0.05 A was delayed");
        assert_eq!(applied.countdown.unwrap().countdown_s, 0);
    }

    /// `energyImpl.cpp:365-371`. With `at_any_change` off, only a start and a
    /// stop are delayed, so a tariff that trims a running session from 32 A to
    /// 16 A steps immediately.
    #[test]
    fn without_any_change_only_the_zero_crossings_delay() {
        let now = Instant::now();
        let elapsed = now + Duration::from_secs(MAX_S as u64);

        let mut up = enabled(false, 3);
        assert!(steady(&mut up, 16.0, now).countdown.unwrap().countdown_s > 0);

        let mut trim = enabled(false, 3);
        steady(&mut trim, 32.0, now);
        steady(&mut trim, 32.0, elapsed);
        let applied = steady(&mut trim, 16.0, elapsed);
        assert_eq!(
            applied.limit_a, 16.0,
            "a trim between two nonzero limits waited"
        );
        assert_eq!(applied.countdown.unwrap().countdown_s, 0);

        let mut down = enabled(false, 3);
        steady(&mut down, 32.0, now);
        steady(&mut down, 32.0, elapsed);
        let stop = steady(&mut down, 0.0, elapsed);
        assert_eq!(
            stop.limit_a, 32.0,
            "a stop did not hold the running current"
        );
        assert!(stop.countdown.unwrap().countdown_s > 0);
    }

    /// `energyImpl.cpp:366-370` compares against 0.1 A twice and the two
    /// comparisons are not complements: `almost_eq(x, 0.)` is `|x| < 0.1` and
    /// the other side is `x > 0.1`, so a limit of exactly 0.1 A is neither
    /// zero nor nonzero and neither branch fires. A step from nothing to
    /// 0.1 A is therefore not a start, and stepping back down from it is not
    /// a stop. Undriven by every other test here, all of which use whole
    /// amps, so a moved threshold would go unnoticed.
    #[test]
    fn the_zero_crossing_branches_have_a_dead_band_at_a_tenth_of_an_amp() {
        let now = Instant::now();
        for (from, to, expected) in [
            (0.0, 0.05, false),
            (0.0, 0.1, false),
            (0.0, 0.11, true),
            (0.05, 0.0, false),
            (0.1, 0.0, false),
            (0.11, 0.0, true),
        ] {
            let mut delay = enabled(false, 21);
            // Reach `from` with the feature off, so it is the remembered
            // limit and no delay is pending.
            delay.disable();
            steady(&mut delay, from, now);
            delay.enable();
            let applied = steady(&mut delay, to, now);
            assert_eq!(
                applied.countdown.expect("the feature is on").countdown_s > 0,
                expected,
                "{from} A to {to} A"
            );
        }
    }

    /// The whole point of the feature: the requested limit is withheld and the
    /// previous one applied, until the drawn interval is over.
    #[test]
    fn the_limit_from_before_the_change_is_applied_until_the_delay_ends() {
        let mut delay = enabled(true, 4);
        let now = Instant::now();
        // Settle at 32 A with no delay pending.
        steady(&mut delay, 32.0, now);
        let drawn = u64::try_from(steady(&mut delay, 32.0, now).countdown.unwrap().countdown_s)
            .expect("a running delay");
        let settled = now + Duration::from_secs(drawn);
        steady(&mut delay, 32.0, settled);
        assert_eq!(steady(&mut delay, 32.0, settled).limit_a, 32.0);

        // Now the energy manager asks for 6 A.
        let start = steady(&mut delay, 6.0, settled);
        let held_for = u64::try_from(start.countdown.unwrap().countdown_s).expect("a delay");
        assert_eq!(start.limit_a, 32.0);
        assert_eq!(start.countdown.unwrap().current_limit_after_delay_a, 6.0);
        assert_eq!(start.countdown.unwrap().current_limit_during_delay_a, 32.0);

        // Mid delay the request is still withheld and the countdown falls.
        let mid = steady(&mut delay, 6.0, settled + Duration::from_secs(held_for - 1));
        assert_eq!(mid.limit_a, 32.0);
        assert_eq!(mid.countdown.unwrap().countdown_s, 1);

        // The call that finds the deadline passed still withholds: the C++
        // takes the held limit at `:471`, before it looks at the clock at
        // `:479`. The call after it is the first to carry the request.
        let ends = settled + Duration::from_secs(held_for);
        let last = steady(&mut delay, 6.0, ends);
        assert_eq!(
            last.limit_a, 32.0,
            "the closing call released the request early"
        );
        assert_eq!(last.countdown.unwrap().countdown_s, 0);
        assert_eq!(steady(&mut delay, 6.0, ends).limit_a, 6.0);
    }

    /// A running delay absorbs further changes rather than restarting for
    /// each one. The C++ short circuits on `not mod->random_delay_running`
    /// (`energyImpl.cpp:459`), so the predicate is not even consulted while a
    /// delay is up: the original held limit and the original deadline both
    /// stand.
    ///
    /// Without that guard an energy manager that revises its answer every few
    /// seconds, which is the normal case under a moving tariff, would redraw
    /// the delay each time and hold the port indefinitely, and the held limit
    /// would drift to whatever was requested one revision ago rather than
    /// staying at the limit that was actually in force. Found by the mutation
    /// sweep: removing the guard changed nothing any other test could see.
    #[test]
    fn a_change_arriving_mid_delay_neither_redraws_it_nor_moves_what_it_holds() {
        let mut delay = enabled(true, 22);
        let now = Instant::now();
        // Settle at 32 A with nothing pending.
        delay.disable();
        steady(&mut delay, 32.0, now);
        delay.enable();

        let start = steady(&mut delay, 6.0, now).countdown.expect("enabled");
        let drawn = start.countdown_s;
        assert!(drawn > 2, "the drawn delay is too short to revise inside");
        assert_eq!(start.current_limit_during_delay_a, 32.0);

        // The energy manager changes its mind twice while the delay runs.
        let mut applied = steady(&mut delay, 20.0, now + Duration::from_secs(1));
        assert_eq!(applied.limit_a, 32.0, "the hold moved under the revision");
        assert_eq!(
            applied.countdown.unwrap().countdown_s,
            drawn - 1,
            "the deadline was redrawn"
        );

        applied = steady(&mut delay, 12.0, now + Duration::from_secs(2));
        assert_eq!(applied.limit_a, 32.0);
        assert_eq!(applied.countdown.unwrap().countdown_s, drawn - 2);
        assert_eq!(
            applied.countdown.unwrap().current_limit_after_delay_a,
            12.0,
            "the countdown named a stale request"
        );

        // The last revision is what lands, once.
        let ends = now + Duration::from_secs(u64::try_from(drawn).expect("nonnegative"));
        assert_eq!(steady(&mut delay, 12.0, ends).limit_a, 32.0);
        assert_eq!(steady(&mut delay, 12.0, ends).limit_a, 12.0);
    }

    /// `:501` records the requested limit and not the held one, on every call
    /// including the calls made while a delay runs. That is what stops a
    /// running delay from starting another one the moment it ends.
    #[test]
    fn a_delay_does_not_retrigger_itself_when_it_ends() {
        let mut delay = enabled(true, 5);
        let now = Instant::now();
        let drawn = u64::try_from(steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s)
            .expect("a running delay");
        let ends = now + Duration::from_secs(drawn);
        steady(&mut delay, 16.0, ends);
        let after = steady(&mut delay, 16.0, ends);
        assert_eq!(after.limit_a, 16.0);
        assert_eq!(
            after.countdown.unwrap().countdown_s,
            0,
            "the delay drew itself a second interval"
        );
    }

    /// `start_time` is filled at `:489` only, which is the branch with time
    /// left. Both zero countdown branches leave the optional unset.
    #[test]
    fn only_a_running_countdown_carries_a_start_time() {
        let mut delay = enabled(true, 6);
        let now = Instant::now();
        let start = steady(&mut delay, 16.0, now).countdown.unwrap();
        assert_eq!(start.started_ago, Some(Duration::ZERO));

        let drawn = u64::try_from(start.countdown_s).expect("a running delay");
        let mid = steady(&mut delay, 16.0, now + Duration::from_secs(1))
            .countdown
            .unwrap();
        assert_eq!(mid.started_ago, Some(Duration::from_secs(1)));

        let ends = now + Duration::from_secs(drawn);
        assert_eq!(
            steady(&mut delay, 16.0, ends)
                .countdown
                .unwrap()
                .started_ago,
            None
        );
        assert_eq!(
            steady(&mut delay, 16.0, ends)
                .countdown
                .unwrap()
                .started_ago,
            None
        );
    }

    /// `:496`. The idle countdown reports the held limit of the last delay,
    /// not the limit in force, and zero before any delay has run. Preserved
    /// because it is what a consumer of this variable actually receives.
    #[test]
    fn the_idle_countdown_reports_the_previous_delays_held_limit() {
        let mut delay = enabled(true, 7);
        let now = Instant::now();
        // Before any delay has run the field is its initial zero.
        delay.disable();
        steady(&mut delay, 32.0, now);
        delay.enable();
        assert_eq!(
            steady(&mut delay, 32.0, now)
                .countdown
                .unwrap()
                .current_limit_during_delay_a,
            0.0
        );

        // A delay that holds 32 A, waited out. Nonzero deliberately: a hold of
        // zero would not tell a reset field from a retained one.
        let drawn = u64::try_from(steady(&mut delay, 6.0, now).countdown.unwrap().countdown_s)
            .expect("a running delay");
        let ends = now + Duration::from_secs(drawn);
        steady(&mut delay, 6.0, ends);
        let idle = steady(&mut delay, 6.0, ends).countdown.unwrap();
        assert_eq!(idle.countdown_s, 0);
        assert_eq!(
            idle.current_limit_during_delay_a, 32.0,
            "the stale hold was lost"
        );
        assert_eq!(idle.current_limit_after_delay_a, 6.0);
    }

    /// `:451-455`. A state in which no delay makes sense drops the running
    /// one, so a vehicle that unplugged mid delay does not keep a stale limit
    /// held against the next session.
    #[test]
    fn a_state_that_permits_no_delay_drops_the_running_one() {
        for state in [
            AcState::Startup,
            AcState::Idle,
            AcState::ChargingPausedEv,
            AcState::ChargingPausedEvse,
            AcState::StoppingCharging,
            AcState::Finished,
            AcState::Disabled,
        ] {
            let mut delay = enabled(true, 8);
            let now = Instant::now();
            assert!(steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s > 0);
            let applied = delay.apply(16.0, state, None, now);
            assert_eq!(applied.limit_a, 16.0, "{state:?} kept the delay");
            assert_eq!(applied.countdown.unwrap().countdown_s, 0, "{state:?}");
        }
        for state in [
            AcState::PrepareCharging,
            AcState::Charging,
            AcState::WaitingForAuthentication,
        ] {
            let mut delay = enabled(true, 8);
            let now = Instant::now();
            assert!(steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s > 0);
            let applied = delay.apply(16.0, state, None, now);
            assert_eq!(applied.limit_a, 0.0, "{state:?} dropped the delay");
            assert!(applied.countdown.unwrap().countdown_s > 0, "{state:?}");
        }
    }

    /// The reset at `:451-455` runs before the start decision, and the limit
    /// change branches do not look at state, so an unsuitable state can drop a
    /// delay and draw a fresh one in the same call. Odd, and pinned so a
    /// tidier port cannot quietly change it.
    #[test]
    fn an_unsuitable_state_can_drop_a_delay_and_start_another_at_once() {
        let mut delay = enabled(true, 9);
        let now = Instant::now();
        assert!(steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s > 0);
        // Idle permits no delay, but the limit also moved, so the reset and
        // the start both fire.
        let applied = delay.apply(24.0, AcState::Idle, None, now);
        assert!(applied.countdown.unwrap().countdown_s > 0);
        assert_eq!(
            applied.limit_a, 16.0,
            "the fresh delay held the wrong limit"
        );
    }

    /// The trap. `random_delay_needed` writes `last_enforced_limit = 0.` in
    /// its startup branch (`:378`) and the caller reads that member to decide
    /// what the delay holds (`:465`), so a restart with a vehicle attached
    /// holds zero rather than the remembered current. A port that treated the
    /// predicate as pure would hold the remembered current and energize the
    /// cable straight after a restart, which is the behavior the delay exists
    /// to prevent.
    #[test]
    fn a_restart_with_a_vehicle_attached_holds_zero_and_not_the_remembered_limit() {
        let mut delay = RandomDelay::new(
            RandomDelaySettings {
                enabled_at_boot: false,
                max_duration_s_at_boot: MAX_S,
                at_any_change: true,
            },
            10,
        );
        let ready = Instant::now();
        // 16 A was in force before the feature came on, so no limit change is
        // pending and the startup branch is the only one that can fire.
        steady(&mut delay, 16.0, ready);
        delay.enable();

        let applied = delay.apply(
            16.0,
            AcState::Charging,
            Some(ready),
            ready + Duration::from_secs(1),
        );
        assert!(
            applied.countdown.unwrap().countdown_s > 0,
            "no startup delay"
        );
        assert_eq!(applied.limit_a, 0.0, "the remembered current was resumed");
        assert_eq!(
            applied.countdown.unwrap().current_limit_during_delay_a,
            0.0,
            "the countdown advertised a limit the port was not applying"
        );
    }

    /// The startup branch is after two `return true`s, so a limit change that
    /// already triggered never reaches it and the delay holds the previous
    /// limit even inside the startup window.
    #[test]
    fn a_limit_change_inside_the_startup_window_still_holds_the_previous_limit() {
        let mut delay = RandomDelay::new(
            RandomDelaySettings {
                enabled_at_boot: false,
                max_duration_s_at_boot: MAX_S,
                at_any_change: true,
            },
            11,
        );
        let ready = Instant::now();
        steady(&mut delay, 16.0, ready);
        delay.enable();
        let applied = delay.apply(
            24.0,
            AcState::Charging,
            Some(ready),
            ready + Duration::from_secs(1),
        );
        assert!(applied.countdown.unwrap().countdown_s > 0);
        assert_eq!(
            applied.limit_a, 16.0,
            "the startup branch overtook the change"
        );
    }

    /// Both halves of the startup condition, and the absent readiness the C++
    /// expresses as a default constructed time point whose distance from now
    /// is the machine's uptime.
    #[test]
    fn the_startup_delay_needs_a_permitting_state_inside_the_window() {
        let ready = Instant::now();
        let cases = [
            (AcState::Charging, Some(ready), Duration::from_secs(4), true),
            (
                AcState::Charging,
                Some(ready),
                Duration::from_secs(5),
                false,
            ),
            (
                AcState::Charging,
                Some(ready),
                Duration::from_secs(600),
                false,
            ),
            (AcState::Charging, None, Duration::from_secs(1), false),
            (AcState::Idle, Some(ready), Duration::from_secs(1), false),
            (
                AcState::ChargingPausedEv,
                Some(ready),
                Duration::from_secs(1),
                false,
            ),
            (
                AcState::PrepareCharging,
                Some(ready),
                Duration::from_secs(1),
                true,
            ),
            (
                AcState::WaitingForAuthentication,
                Some(ready),
                Duration::from_secs(1),
                true,
            ),
        ];
        for (state, ready_since, age, expected) in cases {
            let mut delay = RandomDelay::new(
                RandomDelaySettings {
                    enabled_at_boot: false,
                    max_duration_s_at_boot: MAX_S,
                    at_any_change: true,
                },
                12,
            );
            steady(&mut delay, 16.0, ready);
            delay.enable();
            let applied = delay.apply(16.0, state, ready_since, ready + age);
            assert_eq!(
                applied.countdown.unwrap().countdown_s > 0,
                expected,
                "{state:?} at {age:?} after {ready_since:?}"
            );
        }
    }

    #[test]
    fn enable_disable_and_cancel_each_drop_a_running_delay() {
        let now = Instant::now();
        for (name, act) in [
            ("enable", RandomDelay::enable as fn(&mut RandomDelay)),
            ("disable", RandomDelay::disable),
            ("cancel", RandomDelay::cancel),
        ] {
            let mut delay = enabled(true, 13);
            assert!(steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s > 0);
            act(&mut delay);
            let applied = steady(&mut delay, 16.0, now);
            assert_eq!(applied.limit_a, 16.0, "{name} did not release the request");
            match applied.countdown {
                // `disable` also stops the publish, which the other two do not.
                None => assert_eq!(name, "disable"),
                Some(countdown) => assert_eq!(countdown.countdown_s, 0, "{name}"),
            }
        }
    }

    /// A cancel does not merely end the delay, it must not be followed by a
    /// fresh one for the same request. `:501` is why: the requested limit was
    /// already recorded on the call that started the delay.
    #[test]
    fn a_cancelled_delay_is_not_immediately_redrawn() {
        let mut delay = enabled(true, 14);
        let now = Instant::now();
        steady(&mut delay, 16.0, now);
        delay.cancel();
        assert_eq!(
            steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s,
            0,
            "the cancel was undone by a fresh draw"
        );
    }

    /// A re-enable is a cancel plus a switch, so the port does not keep
    /// holding a limit across an operator turning the feature off and on.
    #[test]
    fn a_disable_then_enable_leaves_nothing_held() {
        let mut delay = enabled(true, 15);
        let now = Instant::now();
        steady(&mut delay, 16.0, now);
        delay.disable();
        delay.enable();
        assert_eq!(steady(&mut delay, 16.0, now).limit_a, 16.0);
    }

    /// `std::rand() % max` divides by zero for a zero maximum, and
    /// `set_duration_s(0)` is reachable from the bus. Here it is a zero length
    /// delay, which is what a maximum of one already produces.
    #[test]
    fn an_out_of_range_maximum_gives_a_zero_length_delay_and_no_panic() {
        let now = Instant::now();
        for maximum in [0, 1, -5, i64::MIN] {
            let mut delay = enabled(true, 16);
            delay.set_duration_s(maximum);
            let applied = steady(&mut delay, 16.0, now);
            let countdown = applied.countdown.expect("the feature is on");
            assert_eq!(countdown.countdown_s, 0, "maximum {maximum}");
            assert_eq!(countdown.started_ago, None, "maximum {maximum}");
            // Zero length still withholds for the one call that drew it, as a
            // maximum of one does in the C++.
            assert_eq!(applied.limit_a, 0.0, "maximum {maximum}");
            assert_eq!(
                steady(&mut delay, 16.0, now).limit_a,
                16.0,
                "maximum {maximum}"
            );
        }
    }

    /// The one maximum the bus can set that a plain port cannot survive: the
    /// deadline it names is not a representable `Instant` and constructing it
    /// panics. Capped at what `RAND_MAX` already caps the C++ at.
    #[test]
    fn the_largest_maximum_the_bus_can_set_draws_a_representable_deadline() {
        let now = Instant::now();
        for maximum in [i64::MAX, DRAW_CEILING_S, DRAW_CEILING_S + 1, 1 << 40] {
            let mut delay = enabled(true, 20);
            delay.set_duration_s(maximum);
            let drawn = steady(&mut delay, 16.0, now)
                .countdown
                .expect("the feature is on")
                .countdown_s;
            assert!(
                (0..DRAW_CEILING_S).contains(&drawn),
                "maximum {maximum} drew {drawn}s"
            );
        }
    }

    #[test]
    fn a_new_maximum_applies_to_the_next_delay_and_not_the_running_one() {
        let mut delay = enabled(true, 17);
        let now = Instant::now();
        let drawn = steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s;
        delay.set_duration_s(1);
        assert_eq!(
            steady(&mut delay, 16.0, now).countdown.unwrap().countdown_s,
            drawn,
            "the running delay was shortened under it"
        );
    }

    /// Every drawn interval is inside `0..max`, over enough seeds that a draw
    /// escaping the modulus would show. The upper bound is what the maximum is
    /// for; the lower bound is what says the generator is not returning a
    /// constant.
    #[test]
    fn a_drawn_delay_is_bounded_by_the_maximum() {
        let now = Instant::now();
        let mut seen = Vec::new();
        for seed in 0..256 {
            let mut delay = RandomDelay::new(
                RandomDelaySettings {
                    enabled_at_boot: true,
                    max_duration_s_at_boot: 10,
                    at_any_change: true,
                },
                seed,
            );
            let drawn = steady(&mut delay, 16.0, now)
                .countdown
                .expect("the feature is on")
                .countdown_s;
            assert!(
                (0..10).contains(&drawn),
                "drew {drawn}s from a maximum of 10"
            );
            seen.push(drawn);
        }
        seen.sort_unstable();
        seen.dedup();
        assert!(
            seen.len() > 5,
            "the draws collapsed onto {} values",
            seen.len()
        );
    }

    /// A fixed seed fixes the sequence, which is what makes the delay lengths
    /// above testable at all, and two seeds do not share one.
    #[test]
    fn the_seed_fixes_the_sequence_and_two_seeds_differ() {
        fn draws(seed: u64) -> Vec<i64> {
            let mut delay = RandomDelay::new(
                RandomDelaySettings {
                    enabled_at_boot: true,
                    max_duration_s_at_boot: MAX_S,
                    at_any_change: true,
                },
                seed,
            );
            let mut now = Instant::now();
            let mut out = Vec::new();
            for step in 0..8 {
                // A fresh change each time, each one waited out before the
                // next, so every iteration draws.
                let requested = f64::from(step) * 3.0 + 6.0;
                let drawn = steady(&mut delay, requested, now)
                    .countdown
                    .expect("the feature is on")
                    .countdown_s;
                out.push(drawn);
                now += Duration::from_secs(u64::try_from(drawn).expect("nonnegative"));
                steady(&mut delay, requested, now);
            }
            out
        }
        assert_eq!(draws(18), draws(18), "the same seed drew two sequences");
        assert_ne!(draws(18), draws(19), "two seeds shared a sequence");
    }
}
