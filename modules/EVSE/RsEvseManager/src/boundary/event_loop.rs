// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The single writer and the one queue that feeds it.
//!
//! Framework callbacks, timer expiries and effect completions all arrive as
//! `Event` on one unbounded channel. One queue means one total order, and it
//! means no intake policy can admit an error raise while refusing its clear:
//! there is no capacity to run out of and no lane to choose.
//!
//! Time is an input. The loop reads the clock once per event and passes the
//! instant into the reducer, which never reads a clock itself.

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};
use std::sync::mpsc::{channel, Receiver, RecvTimeoutError, Sender, TryRecvError};
use std::sync::Arc;
use std::time::{Duration, Instant};

use crate::core::effect::{Effect, EffectId, TimerId};
use crate::core::event::{Event, ReplyToken};

/// Queue depth at which the loop warns. Mirrors the framework's own per-topic
/// limit so the two report congestion at the same scale.
pub const QUEUE_WARN_DEPTH: usize = 100;

/// Per-event processing time at which the loop warns. The reducer does
/// microseconds of work per event, so tens of milliseconds means something is
/// performing I/O on the writer thread.
pub const LATENCY_WARN: Duration = Duration::from_millis(50);

/// Queue depth and processing latency high water marks. This is the whole of the
/// module's observability: no registry, no correlation ids, no ring buffer.
#[derive(Debug, Default)]
pub struct LoopStats {
    depth: AtomicUsize,
    depth_high_water: AtomicUsize,
    latency_high_water_ns: AtomicU64,
    stale_timers: AtomicU64,
}

impl LoopStats {
    fn on_enqueue(&self) -> usize {
        let depth = self.depth.fetch_add(1, Ordering::Relaxed) + 1;
        self.depth_high_water.fetch_max(depth, Ordering::Relaxed);
        depth
    }

    fn on_dequeue(&self) {
        // Saturating by construction: every dequeue pairs with an enqueue.
        let _ = self
            .depth
            .fetch_update(Ordering::Relaxed, Ordering::Relaxed, |depth| {
                Some(depth.saturating_sub(1))
            });
    }

    fn record_latency(&self, elapsed: Duration) {
        let nanos = u64::try_from(elapsed.as_nanos()).unwrap_or(u64::MAX);
        self.latency_high_water_ns
            .fetch_max(nanos, Ordering::Relaxed);
    }

    fn on_stale_timer(&self) {
        self.stale_timers.fetch_add(1, Ordering::Relaxed);
    }

    pub fn depth(&self) -> usize {
        self.depth.load(Ordering::Relaxed)
    }

    pub fn depth_high_water(&self) -> usize {
        self.depth_high_water.load(Ordering::Relaxed)
    }

    pub fn latency_high_water(&self) -> Duration {
        Duration::from_nanos(self.latency_high_water_ns.load(Ordering::Relaxed))
    }

    /// Timer expiries discarded as superseded or cancelled. Nonzero is normal.
    pub fn stale_timers(&self) -> u64 {
        self.stale_timers.load(Ordering::Relaxed)
    }
}

/// Intake. Cloned into every framework callback, the timer thread and every
/// effect worker. `send` never blocks, never drops for capacity and never
/// discriminates between event kinds.
#[derive(Clone)]
pub struct EventSender {
    tx: Sender<Event>,
    stats: Arc<LoopStats>,
}

impl EventSender {
    pub fn send(&self, event: Event) {
        let depth = self.stats.on_enqueue();
        if self.tx.send(event).is_err() {
            self.stats.on_dequeue();
            // Only reachable after the writer thread is gone, which is after
            // shutdown has already reached safe state.
            log::debug!("event discarded: the writer thread has stopped");
            return;
        }
        if depth >= QUEUE_WARN_DEPTH {
            log::warn!("event queue depth is {depth}, the writer may be stuck or too slow");
        }
    }

    pub fn stats(&self) -> &Arc<LoopStats> {
        &self.stats
    }
}

/// The receiving end. Owned by the writer thread alone.
pub struct EventQueue {
    rx: Receiver<Event>,
    stats: Arc<LoopStats>,
}

impl EventQueue {
    pub fn recv(&self) -> Option<Event> {
        let event = self.rx.recv().ok();
        if event.is_some() {
            self.stats.on_dequeue();
        }
        event
    }

    pub fn recv_timeout(&self, timeout: Duration) -> Result<Event, RecvTimeoutError> {
        let event = self.rx.recv_timeout(timeout);
        if event.is_ok() {
            self.stats.on_dequeue();
        }
        event
    }

    pub fn try_recv(&self) -> Result<Event, TryRecvError> {
        let event = self.rx.try_recv();
        if event.is_ok() {
            self.stats.on_dequeue();
        }
        event
    }

    pub fn stats(&self) -> &Arc<LoopStats> {
        &self.stats
    }
}

/// One unbounded queue, created before anything that feeds it, so callbacks
/// registered during module start already have somewhere to append.
pub fn event_channel() -> (EventSender, EventQueue) {
    let (tx, rx) = channel();
    let stats = Arc::new(LoopStats::default());
    (
        EventSender {
            tx,
            stats: Arc::clone(&stats),
        },
        EventQueue { rx, stats },
    )
}

/// What the writer drives. `Core` is the only production implementation; tests
/// substitute a recorder, which is why this module needs neither `Core` nor
/// everestrs to be exercised.
pub trait Reducer {
    fn apply(&mut self, event: Event, now: Instant) -> Vec<Effect>;
}

/// Where the writer sends effects, timer requests and command verdicts. The
/// last two are lock-plus-notify and are handled here rather than being
/// dispatched, so a blocked peer cannot delay a deadline or an answer.
/// `Effect::context` answers `None` for all three, which is what records that
/// they have no lane.
pub trait EffectDispatch {
    fn dispatch(&self, id: Option<EffectId>, effect: Effect);
    fn arm_timer(&self, id: TimerId, generation: u64, after: Duration);
    fn cancel_timer(&self, id: TimerId);
    /// Completes a caller blocked on a command's verdict.
    fn answer(&self, reply: ReplyToken, answer: bool);
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StepOutcome {
    Continue,
    /// The expiry named a generation that is no longer armed. Not delivered.
    StaleTimer,
    /// Safe state effects have been dispatched. Teardown follows.
    Shutdown,
}

/// The single writer. Owns the reducer outright; no lock protects it because
/// nothing else can reach it.
pub struct Writer<R: Reducer, D: EffectDispatch> {
    reducer: R,
    dispatch: D,
    stats: Arc<LoopStats>,
    next_generation: u64,
    /// Generation currently armed per timer id. An expiry not matching this is
    /// cancelled or superseded and is discarded before it reaches the reducer.
    armed: HashMap<TimerId, u64>,
}

impl<R: Reducer, D: EffectDispatch> Writer<R, D> {
    pub fn new(reducer: R, dispatch: D, stats: Arc<LoopStats>) -> Self {
        Self {
            reducer,
            dispatch,
            stats,
            next_generation: 0,
            armed: HashMap::new(),
        }
    }

    pub fn reducer(&self) -> &R {
        &self.reducer
    }

    pub fn into_parts(self) -> (R, D) {
        (self.reducer, self.dispatch)
    }

    /// One event, one clock read, one reducer call, then dispatch.
    pub fn step(&mut self, event: Event, now: Instant) -> StepOutcome {
        if let Event::Timer { id, generation } = event {
            if self.armed.get(&id) != Some(&generation) {
                self.stats.on_stale_timer();
                return StepOutcome::StaleTimer;
            }
            // One shot: a fired timer is no longer armed.
            self.armed.remove(&id);
        }

        let shutdown = matches!(event, Event::Shutdown);
        let started = Instant::now();
        let effects = self.reducer.apply(event, now);
        for effect in effects {
            self.perform(effect);
        }
        let elapsed = started.elapsed();
        self.stats.record_latency(elapsed);
        if elapsed >= LATENCY_WARN {
            log::warn!("event processing took {elapsed:?}, above the {LATENCY_WARN:?} threshold");
        }

        if shutdown {
            StepOutcome::Shutdown
        } else {
            StepOutcome::Continue
        }
    }

    fn perform(&mut self, effect: Effect) {
        match effect {
            Effect::StartTimer { id, after } => {
                let generation = self.next_generation;
                self.next_generation += 1;
                self.armed.insert(id, generation);
                self.dispatch.arm_timer(id, generation, after);
            }
            Effect::CancelTimer { id } => {
                self.armed.remove(&id);
                self.dispatch.cancel_timer(id);
            }
            // Handed over here rather than dispatched, so the caller's answer
            // cannot queue behind a slow device on some lane.
            Effect::AnswerCommand { reply, answer } => {
                self.dispatch.answer(reply, answer);
            }
            other => {
                // Identity comes from the core, which is the only party that can
                // correlate a completion with the stage that asked for it. The
                // loop mints nothing.
                let id = other.awaited();
                self.dispatch.dispatch(id, other);
            }
        }
    }
}

/// Runs until `Event::Shutdown` is processed or intake closes. Safe state effects
/// are dispatched before this returns; waiting for them to finish is the caller's
/// bounded join.
pub fn drive<R: Reducer, D: EffectDispatch>(writer: &mut Writer<R, D>, queue: &EventQueue) {
    while let Some(event) = queue.recv() {
        let now = Instant::now();
        if writer.step(event, now) == StepOutcome::Shutdown {
            return;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::effect::{EffectIds, EffectOutcome, ExecContext};
    use crate::core::event::{ErrorEvent, ErrorSource, MeterReading, Severity};
    use crate::core::hlc::Power;
    use std::collections::VecDeque;
    use std::sync::Mutex;

    /// Records what reached the reducer and returns preloaded effects. This is
    /// the whole reason `Reducer` exists: the queue's delivery guarantees are
    /// asserted without a power path in the way.
    #[derive(Default)]
    struct Recorder {
        seen: Vec<Event>,
        pending: VecDeque<Vec<Effect>>,
    }

    impl Recorder {
        fn emit(&mut self, effects: Vec<Effect>) {
            self.pending.push_back(effects);
        }
    }

    impl Reducer for Recorder {
        fn apply(&mut self, event: Event, _now: Instant) -> Vec<Effect> {
            self.seen.push(event);
            self.pending.pop_front().unwrap_or_default()
        }
    }

    #[derive(Default)]
    struct Recording {
        dispatched: Mutex<Vec<(Option<EffectId>, Effect)>>,
        armed: Mutex<Vec<(TimerId, u64, Duration)>>,
        cancelled: Mutex<Vec<TimerId>>,
        answered: Mutex<Vec<(ReplyToken, bool)>>,
    }

    impl EffectDispatch for &Recording {
        fn dispatch(&self, id: Option<EffectId>, effect: Effect) {
            self.dispatched.lock().unwrap().push((id, effect));
        }

        fn arm_timer(&self, id: TimerId, generation: u64, after: Duration) {
            self.armed.lock().unwrap().push((id, generation, after));
        }

        fn cancel_timer(&self, id: TimerId) {
            self.cancelled.lock().unwrap().push(id);
        }

        fn answer(&self, reply: ReplyToken, answer: bool) {
            self.answered.lock().unwrap().push((reply, answer));
        }
    }

    fn error(raised: bool) -> Event {
        Event::Error(ErrorEvent {
            source: ErrorSource::Bsp,
            error_type: "evse_board_support/MREC8EmergencyStop".into(),
            sub_type: String::new(),
            vendor_id: String::new(),
            severity: Severity::High,
            raised,
        })
    }

    fn meter() -> Event {
        Event::Meter(MeterReading {
            energy_wh_import: 1.0,
            power_w: Some(Power {
                total_w: 2.0,
                ..Power::default()
            }),
            voltage_v: 230.0,
            current_a: 16.0,
            dc_voltage_v: None,
            phase_currents_a: None,
        })
    }

    fn raised_flags(seen: &[Event]) -> Vec<bool> {
        seen.iter()
            .filter_map(|event| match event {
                Event::Error(error) => Some(error.raised),
                _ => None,
            })
            .collect()
    }

    #[test]
    fn an_error_raise_and_its_clear_both_arrive_under_sustained_load() {
        // The regression this queue exists to make unrepresentable: a bounded
        // intake with a reserved lane admitted the raise and dropped the clear,
        // latching the EVSE inoperative until restart.
        const PRODUCERS: usize = 4;
        const PER_PRODUCER: usize = 2000;

        let (sender, queue) = event_channel();

        let mut handles = Vec::new();
        for _ in 0..PRODUCERS {
            let sender = sender.clone();
            handles.push(std::thread::spawn(move || {
                for _ in 0..PER_PRODUCER {
                    sender.send(meter());
                }
            }));
        }
        // Interleaved with the flood, from a fifth producer.
        let flanker = {
            let sender = sender.clone();
            std::thread::spawn(move || {
                for _ in 0..500 {
                    sender.send(meter());
                }
                sender.send(error(true));
                for _ in 0..500 {
                    sender.send(meter());
                }
                sender.send(error(false));
            })
        };

        for handle in handles {
            handle.join().unwrap();
        }
        flanker.join().unwrap();
        sender.send(Event::Shutdown);

        let recording = Recording::default();
        let mut writer = Writer::new(Recorder::default(), &recording, Arc::clone(queue.stats()));
        drive(&mut writer, &queue);

        let seen = &writer.reducer().seen;
        assert_eq!(
            raised_flags(seen),
            vec![true, false],
            "both the raise and the clear must reach the core, in order"
        );
        let expected = PRODUCERS * PER_PRODUCER + 1000 + 2 + 1;
        assert_eq!(
            seen.len(),
            expected,
            "nothing is coalesced and nothing is dropped"
        );
    }

    #[test]
    fn queue_depth_high_water_is_recorded() {
        let (sender, queue) = event_channel();
        for _ in 0..QUEUE_WARN_DEPTH + 7 {
            sender.send(meter());
        }
        assert_eq!(queue.stats().depth_high_water(), QUEUE_WARN_DEPTH + 7);
        assert_eq!(queue.stats().depth(), QUEUE_WARN_DEPTH + 7);

        sender.send(Event::Shutdown);
        let recording = Recording::default();
        let mut writer = Writer::new(Recorder::default(), &recording, Arc::clone(queue.stats()));
        drive(&mut writer, &queue);

        assert_eq!(queue.stats().depth(), 0, "draining returns depth to zero");
        assert_eq!(
            queue.stats().depth_high_water(),
            QUEUE_WARN_DEPTH + 8,
            "the high water mark is not reset by draining"
        );
    }

    #[test]
    fn processing_latency_high_water_is_recorded() {
        struct Slow;
        impl Reducer for Slow {
            fn apply(&mut self, _event: Event, _now: Instant) -> Vec<Effect> {
                std::thread::sleep(Duration::from_millis(5));
                Vec::new()
            }
        }

        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut writer = Writer::new(Slow, &recording, Arc::clone(&stats));
        writer.step(meter(), Instant::now());

        assert!(
            stats.latency_high_water() >= Duration::from_millis(5),
            "observed {:?}",
            stats.latency_high_water()
        );
    }

    /// The verdict is handed over by the writer, never dispatched.
    ///
    /// If it went to a lane it would queue behind whatever that lane is doing,
    /// and a caller blocked on it would wait out its bound behind a slow
    /// device. Same reason the two timer effects are intercepted.
    #[test]
    fn a_command_verdict_is_handed_over_and_never_dispatched() {
        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut reducer = Recorder::default();
        reducer.emit(vec![
            Effect::AnswerCommand {
                reply: ReplyToken(7),
                answer: false,
            },
            Effect::PwmOff,
        ]);
        let mut writer = Writer::new(reducer, &recording, Arc::clone(&stats));

        writer.step(meter(), Instant::now());

        let answered = recording.answered.lock().unwrap().clone();
        assert_eq!(answered, vec![(ReplyToken(7), false)]);
        let dispatched = recording.dispatched.lock().unwrap().clone();
        assert_eq!(
            dispatched,
            vec![(None, Effect::PwmOff)],
            "the verdict must not reach a lane"
        );
    }

    #[test]
    fn a_cancelled_timer_does_not_reach_the_reducer() {
        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut reducer = Recorder::default();
        reducer.emit(vec![Effect::StartTimer {
            id: TimerId(42),
            after: Duration::from_secs(1),
        }]);
        reducer.emit(vec![Effect::CancelTimer { id: TimerId(42) }]);
        let mut writer = Writer::new(reducer, &recording, Arc::clone(&stats));

        writer.step(meter(), Instant::now());
        let (id, generation, _) = recording.armed.lock().unwrap()[0];
        writer.step(meter(), Instant::now());
        let cancelled_now = recording.cancelled.lock().unwrap().clone();
        assert_eq!(
            cancelled_now.as_slice(),
            &[TimerId(42)]
        );

        // A cancel races an expiry already in flight. It must not be delivered.
        let outcome = writer.step(Event::Timer { id, generation }, Instant::now());
        assert_eq!(outcome, StepOutcome::StaleTimer);
        assert_eq!(stats.stale_timers(), 1);
        assert!(
            !writer
                .reducer()
                .seen
                .iter()
                .any(|event| matches!(event, Event::Timer { .. })),
            "a cancelled timer must never reach the core"
        );
    }

    #[test]
    fn a_re_armed_timer_delivers_only_the_newest_generation() {
        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut reducer = Recorder::default();
        for _ in 0..2 {
            reducer.emit(vec![Effect::StartTimer {
                id: TimerId(9),
                after: Duration::from_secs(1),
            }]);
        }
        let mut writer = Writer::new(reducer, &recording, Arc::clone(&stats));

        writer.step(meter(), Instant::now());
        writer.step(meter(), Instant::now());

        let armed = recording.armed.lock().unwrap().clone();
        assert_eq!(armed.len(), 2);
        let (_, old_generation, _) = armed[0];
        let (_, new_generation, _) = armed[1];
        assert_ne!(
            old_generation, new_generation,
            "re-arming bumps the generation"
        );

        assert_eq!(
            writer.step(
                Event::Timer {
                    id: TimerId(9),
                    generation: old_generation
                },
                Instant::now()
            ),
            StepOutcome::StaleTimer
        );
        assert_eq!(
            writer.step(
                Event::Timer {
                    id: TimerId(9),
                    generation: new_generation
                },
                Instant::now()
            ),
            StepOutcome::Continue
        );

        let delivered: Vec<u64> = writer
            .reducer()
            .seen
            .iter()
            .filter_map(|event| match event {
                Event::Timer { generation, .. } => Some(*generation),
                _ => None,
            })
            .collect();
        assert_eq!(delivered, vec![new_generation]);
    }

    #[test]
    fn a_timer_fires_once_even_if_the_expiry_is_replayed() {
        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut reducer = Recorder::default();
        reducer.emit(vec![Effect::StartTimer {
            id: TimerId(4),
            after: Duration::from_millis(1),
        }]);
        let mut writer = Writer::new(reducer, &recording, Arc::clone(&stats));
        writer.step(meter(), Instant::now());
        let (id, generation, _) = recording.armed.lock().unwrap()[0];

        assert_eq!(
            writer.step(Event::Timer { id, generation }, Instant::now()),
            StepOutcome::Continue
        );
        assert_eq!(
            writer.step(Event::Timer { id, generation }, Instant::now()),
            StepOutcome::StaleTimer
        );
    }

    #[test]
    fn the_identity_the_core_chose_reaches_the_dispatcher_unchanged() {
        // The loop mints no identity of its own. An effect the core awaits is
        // dispatched under the identity the core put on it, so the completion
        // can be attributed; an effect nothing awaits is dispatched without one.
        //
        // The fixture cannot write an identity either, which is the same rule
        // read from the other side: it allocates one and stands in for the core.
        let chosen = EffectIds::one_space_for_tests().allocate().effect_id();
        let recording = Recording::default();
        let stats = Arc::new(LoopStats::default());
        let mut reducer = Recorder::default();
        reducer.emit(vec![
            Effect::SupplyOff,
            Effect::ImdSelfTest {
                id: chosen,
                voltage_v: 500.0,
            },
            Effect::StartTimer {
                id: TimerId(1),
                after: Duration::from_secs(1),
            },
            Effect::PwmOff,
        ]);
        let mut writer = Writer::new(reducer, &recording, Arc::clone(&stats));
        writer.step(meter(), Instant::now());

        let dispatched = recording.dispatched.lock().unwrap().clone();
        assert_eq!(
            dispatched,
            vec![
                (None, Effect::SupplyOff),
                (
                    Some(chosen),
                    Effect::ImdSelfTest {
                        id: chosen,
                        voltage_v: 500.0
                    }
                ),
                (None, Effect::PwmOff)
            ]
        );
        let armed_now = recording.armed.lock().unwrap().clone();
        assert_eq!(
            armed_now.len(),
            1,
            "timers are armed, not dispatched"
        );
    }

    #[test]
    fn effects_are_routed_by_their_own_classification() {
        // The writer does not decide context; it asks the effect. Asserted here so
        // a future routing shortcut in the writer shows up as a failure.
        assert_eq!(
            Effect::SupplyOff.context(),
            Some(ExecContext::Device(crate::core::effect::Device::Supply))
        );
        assert_eq!(
            Effect::PwmOff.context(),
            Some(ExecContext::Device(crate::core::effect::Device::Bsp))
        );
        assert_eq!(
            Effect::PublishReady(true).context(),
            Some(ExecContext::Publish),
            "a write on the module's own interface takes the ordered lane"
        );
    }

    #[test]
    fn shutdown_stops_the_loop_after_its_effects_are_dispatched() {
        let (sender, queue) = event_channel();
        let recording = Recording::default();
        let mut reducer = Recorder::default();
        reducer.emit(vec![Effect::AllowPowerOn(false), Effect::SupplyOff]);
        let mut writer = Writer::new(reducer, &recording, Arc::clone(queue.stats()));

        sender.send(Event::Shutdown);
        sender.send(meter());
        drive(&mut writer, &queue);

        let dispatched = recording.dispatched.lock().unwrap().clone();
        assert_eq!(
            dispatched,
            vec![
                (None, Effect::AllowPowerOn(false)),
                (None, Effect::SupplyOff)
            ],
            "safe state effects are dispatched before the loop returns"
        );
        assert_eq!(
            writer.reducer().seen.len(),
            1,
            "the loop stops at shutdown and does not process what follows"
        );
    }

    #[test]
    fn the_loop_returns_when_intake_closes() {
        let (sender, queue) = event_channel();
        let recording = Recording::default();
        let mut writer = Writer::new(Recorder::default(), &recording, Arc::clone(queue.stats()));
        sender.send(meter());
        drop(sender);
        drive(&mut writer, &queue);
        assert_eq!(writer.reducer().seen.len(), 1);
    }

    #[test]
    fn effect_completions_re_enter_as_ordinary_events() {
        let (sender, queue) = event_channel();
        let recording = Recording::default();
        let mut writer = Writer::new(Recorder::default(), &recording, Arc::clone(queue.stats()));
        let carried = EffectIds::one_space_for_tests().allocate().effect_id();
        sender.send(Event::EffectDone {
            id: Some(carried),
            outcome: EffectOutcome::Failed("peer refused".into()),
        });
        sender.send(Event::Shutdown);
        drive(&mut writer, &queue);

        assert!(matches!(
            writer.reducer().seen[0],
            Event::EffectDone { id, .. } if id == Some(carried)
        ));
    }
}
