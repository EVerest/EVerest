// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Effect execution off the writer thread.
//!
//! Each device and the store have a FIFO worker, independent of the publish
//! lane. No cross-device or device/publish/store order is promised. A slow
//! store cannot stall a device or announcements.
//!
//! Every lane here is serial: one thread each. There is no shared pool. The
//! ordinary pool that used to carry effects outside these domains was retired
//! once its last member left for the ordered lane; `Effect::context` answers
//! `None` for the only effects that remained, and the writer arms those itself.
//!
//! `safety_context` is the one policy point for the deferred shutdown contract.
//! It retains the previous safety lane bypass, including its known possibility
//! of stale in-flight device actuation after safety-off. Queue serialization
//! does not fix that race, and no cancellation or generation fence is claimed.
//!
//! Every executed effect posts `Event::EffectDone` onto the writer queue.

use std::collections::BTreeMap;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::sync::mpsc::{channel, Sender};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

use crate::boundary::event_loop::EventSender;
use crate::core::effect::{Device, Effect, EffectId, EffectOutcome, ExecContext};
use crate::core::event::Event;

const SERIAL_CONTEXTS: [(ExecContext, &str); 8] = [
    (ExecContext::Safety, "rsevse-safety"),
    (ExecContext::Publish, "rsevse-publish"),
    (ExecContext::Device(Device::Supply), "rsevse-supply"),
    (ExecContext::Device(Device::Bsp), "rsevse-bsp"),
    (ExecContext::Device(Device::Imd), "rsevse-imd"),
    (
        ExecContext::Device(Device::OverVoltageMonitor),
        "rsevse-ovm",
    ),
    (ExecContext::Device(Device::ConnectorLock), "rsevse-lock"),
    (ExecContext::Store, "rsevse-store"),
];

/// Each listed domain owns exactly one worker.
pub const SERIAL_LANES: usize = SERIAL_CONTEXTS.len();

/// Retain the pre-existing off/release bypass while its in-flight RPC
/// contract is deferred. These commands are exceptions to per-device FIFO.
/// In particular SupplyOff may overtake a supply call that later re-enables it.
/// Change this policy only with the shutdown decision, not by moving a device
/// worker or taking its lock here: either would silently delay safety actuation.
fn safety_context(effect: &Effect) -> Option<ExecContext> {
    match effect {
        Effect::AllowPowerOn(false)
        | Effect::SupplyOff
        | Effect::SetCpState(_)
        | Effect::UnlockConnector
        | Effect::BspEnable(false) => Some(ExecContext::Safety),
        _ => None,
    }
}

/// Performs one effect against the outside world. Implemented over the generated
/// everestrs publishers in `main`, and over a recording double in tests, which is
/// what keeps this module compilable and testable without the bindings.
pub trait EffectRunner: Send + Sync + 'static {
    fn run(&self, effect: &Effect) -> EffectOutcome;
}

struct Job {
    /// `None` for effects the core does not await. The identity is carried
    /// through untouched; the executor never invents one.
    id: Option<EffectId>,
    effect: Effect,
}

/// Counts live workers so shutdown can wait with a bound. `JoinHandle::join` has
/// no timeout, so a stuck worker must be waited on through a condvar and then
/// abandoned rather than joined.
struct Liveness {
    remaining: Mutex<usize>,
    done: Condvar,
}

impl Liveness {
    fn retire(&self) {
        let mut remaining = self
            .remaining
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        *remaining = remaining.saturating_sub(1);
        if *remaining == 0 {
            self.done.notify_all();
        }
    }

    /// True when every worker retired within `timeout`.
    fn wait(&self, timeout: Duration) -> bool {
        let deadline = std::time::Instant::now() + timeout;
        let mut remaining = self
            .remaining
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        while *remaining > 0 {
            let now = std::time::Instant::now();
            if now >= deadline {
                return false;
            }
            let (guard, _timed_out) = self
                .done
                .wait_timeout(remaining, deadline - now)
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            remaining = guard;
        }
        true
    }
}

pub struct Executor {
    queues: BTreeMap<ExecContext, Sender<Job>>,
    liveness: Arc<Liveness>,
    threads: Vec<JoinHandle<()>>,
}

impl Executor {
    pub fn start(runner: Arc<dyn EffectRunner>, events: EventSender) -> Self {
        // One thread per serial lane. Shutdown waits on this count, so a lane
        // added without it would never be waited for.
        let workers = SERIAL_LANES;
        let liveness = Arc::new(Liveness {
            remaining: Mutex::new(workers),
            done: Condvar::new(),
        });

        let mut threads = Vec::with_capacity(workers);

        let mut queues = BTreeMap::new();
        for (context, name) in SERIAL_CONTEXTS {
            let (tx, rx) = channel::<Job>();
            queues.insert(context, tx);
            threads.push(spawn_worker(
                name,
                Arc::clone(&runner),
                events.clone(),
                Arc::clone(&liveness),
                move || rx.recv().ok(),
            ));
        }
        Self {
            queues,
            liveness,
            threads,
        }
    }

    /// Routes by execution context. Classification lives on `Effect`, so a new
    /// variant does not compile until its context is chosen.
    pub fn dispatch(&self, id: Option<EffectId>, effect: Effect) {
        let Some(context) = safety_context(&effect).or_else(|| effect.context()) else {
            // `Effect::context` answers `None` only for the timer effects, which
            // the writer arms itself and never dispatches. Reaching here means a
            // boundary bug, not a shutdown.
            log::error!("effect {effect:?} dropped: it has no execution lane");
            return;
        };
        let queue = self.queues.get(&context);
        let Some(queue) = queue else {
            log::error!("effect {effect:?} dropped: executor is shutting down");
            return;
        };
        if let Err(rejected) = queue.send(Job { id, effect }) {
            let effect = rejected.0.effect;
            log::error!("effect {effect:?} dropped: {context:?} workers are gone");
        }
    }

    /// Closes intake, lets queued effects run to completion, and waits with a
    /// bound. Returns false when a worker was still busy at the deadline, in
    /// which case it is abandoned rather than joined. The bound covers every
    /// lane, so a wedged publish cannot hold the process open.
    pub fn shutdown(mut self, timeout: Duration) -> bool {
        self.queues.clear();

        let drained = self.liveness.wait(timeout);
        if !drained {
            log::error!("effect workers did not finish within {timeout:?}");
            return false;
        }
        for thread in self.threads.drain(..) {
            let _ = thread.join();
        }
        true
    }
}

fn spawn_worker<F>(
    name: &str,
    runner: Arc<dyn EffectRunner>,
    events: EventSender,
    liveness: Arc<Liveness>,
    mut next: F,
) -> JoinHandle<()>
where
    F: FnMut() -> Option<Job> + Send + 'static,
{
    std::thread::Builder::new()
        .name(name.to_string())
        .spawn(move || {
            while let Some(job) = next() {
                let outcome = execute(runner.as_ref(), &job.effect);
                events.send(Event::EffectDone {
                    id: job.id,
                    outcome,
                });
            }
            liveness.retire();
        })
        .expect("failed to spawn effect worker")
}

/// A panic in an effect is a failed effect, never a lost worker. The core sees
/// `Failed` and drives its own recovery.
fn execute(runner: &dyn EffectRunner, effect: &Effect) -> EffectOutcome {
    match catch_unwind(AssertUnwindSafe(|| runner.run(effect))) {
        Ok(outcome) => outcome,
        Err(payload) => {
            let message = panic_message(payload);
            log::error!("effect {effect:?} panicked: {message}");
            EffectOutcome::Failed(format!("panic: {message}"))
        }
    }
}

fn panic_message(payload: Box<dyn std::any::Any + Send>) -> String {
    if let Some(text) = payload.downcast_ref::<&'static str>() {
        (*text).to_string()
    } else if let Some(text) = payload.downcast_ref::<String>() {
        text.clone()
    } else {
        "non string panic payload".to_string()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::boundary::event_loop::{event_channel, EventQueue};
    use crate::core::effect::{CpState, EffectIds, SessionEventReport};
    use crate::core::session::SessionEvent;
    use std::sync::atomic::{AtomicUsize, Ordering};

    const PATIENCE: Duration = Duration::from_millis(2000);

    /// One block of identities, allocated once from a single space.
    ///
    /// A fixture cannot write an `EffectId`, and these tests need a stable
    /// handle they can name in an assertion after dispatching under it, so the
    /// block is allocated up front and the fixture's number indexes into it.
    /// Allocation order follows the index, which is what keeps the sorted
    /// comparisons below meaningful.
    fn identities() -> &'static Vec<EffectId> {
        static ALL: std::sync::OnceLock<Vec<EffectId>> = std::sync::OnceLock::new();
        ALL.get_or_init(|| {
            let mut ids = EffectIds::one_space_for_tests();
            (0..1024).map(|_| ids.allocate().effect_id()).collect()
        })
    }

    /// An identity as the loop delivers it. Effects the core awaits carry one;
    /// these tests use it purely as a handle on which effect completed. The
    /// number is a fixture label, not the identity.
    fn carried(label: u64) -> Option<EffectId> {
        Some(
            *identities()
                .get(label as usize)
                .expect("a fixture label inside the allocated block"),
        )
    }

    /// Ordinary effects block for `block`. Safety effects return immediately, so
    /// any delay observed on a safety effect is queueing, not work.
    struct SlowOrdinary {
        block: Duration,
    }

    impl EffectRunner for SlowOrdinary {
        fn run(&self, effect: &Effect) -> EffectOutcome {
            if safety_context(effect).is_none() {
                std::thread::sleep(self.block);
            }
            EffectOutcome::Ok
        }
    }

    struct PanicOn(Effect);

    impl EffectRunner for PanicOn {
        fn run(&self, effect: &Effect) -> EffectOutcome {
            if *effect == self.0 {
                panic!("effect blew up");
            }
            EffectOutcome::Ok
        }
    }

    struct AlwaysOk;

    impl EffectRunner for AlwaysOk {
        fn run(&self, _effect: &Effect) -> EffectOutcome {
            EffectOutcome::Ok
        }
    }

    fn done_of(event: Event) -> (Option<EffectId>, EffectOutcome) {
        match event {
            Event::EffectDone { id, outcome } => (id, outcome),
            other => panic!("expected an effect completion, got {other:?}"),
        }
    }

    fn collect_ids(queue: &EventQueue, count: usize) -> Vec<Option<EffectId>> {
        (0..count)
            .map(|_| {
                done_of(
                    queue
                        .recv_timeout(PATIENCE)
                        .expect("effect must post completion"),
                )
                .0
            })
            .collect()
    }

    /// A session event publish carrying its dispatch index as the session uuid,
    /// which is what the ordering assertions read back.
    fn publish(index: usize) -> Effect {
        Effect::PublishSessionEvent(SessionEventReport {
            uuid: index.to_string(),
            event: SessionEvent::Authorized,
            started: None,
            payload: None,
        })
    }

    fn published_index(effect: &Effect) -> usize {
        match effect {
            Effect::PublishSessionEvent(report) => {
                report.uuid.parse().expect("the test writes an index")
            }
            other => panic!("expected a session event publish, got {other:?}"),
        }
    }

    /// Records the order publishes actually reached the wire, and how many were
    /// ever in flight together.
    ///
    /// A publish is synchronous on the calling thread, so the write lands at the
    /// end of the call. Later publishes are made shorter than earlier ones, so
    /// concurrent execution inverts the order rather than merely risking it.
    struct PublishTape {
        wrote: Mutex<Vec<usize>>,
        in_flight: Mutex<usize>,
        max_in_flight: AtomicUsize,
        step: Duration,
        count: usize,
    }

    impl PublishTape {
        fn new(count: usize, step: Duration) -> Arc<Self> {
            Arc::new(Self {
                wrote: Mutex::new(Vec::new()),
                in_flight: Mutex::new(0),
                max_in_flight: AtomicUsize::new(0),
                step,
                count,
            })
        }

        fn wrote(&self) -> Vec<usize> {
            self.wrote.lock().expect("tape poisoned").clone()
        }

        fn max_in_flight(&self) -> usize {
            self.max_in_flight.load(Ordering::Relaxed)
        }

        fn enter(&self) {
            let mut live = self.in_flight.lock().expect("tape poisoned");
            *live += 1;
            self.max_in_flight.fetch_max(*live, Ordering::Relaxed);
        }

        fn leave(&self, index: usize) {
            self.wrote.lock().expect("tape poisoned").push(index);
            *self.in_flight.lock().expect("tape poisoned") -= 1;
        }
    }

    impl EffectRunner for PublishTape {
        fn run(&self, effect: &Effect) -> EffectOutcome {
            if matches!(effect, Effect::PublishSessionEvent(_)) {
                let index = published_index(effect);
                self.enter();
                // Later publishes are made shorter than earlier ones, so
                // concurrent execution inverts the order rather than merely
                // risking it.
                std::thread::sleep(self.step * (self.count - index) as u32);
                self.leave(index);
                return EffectOutcome::Ok;
            }
            EffectOutcome::Ok
        }
    }

    #[test]
    fn consecutive_publishes_reach_the_wire_in_dispatch_order() {
        // The regression this lane exists for: two session events emitted by one
        // `Core::apply` were published in whichever order three shared workers
        // happened to finish in, and five SIL runs produced three orderings of
        // the same four events.
        const COUNT: usize = 6;
        let (sender, queue) = event_channel();
        let tape = PublishTape::new(COUNT, Duration::from_millis(15));
        let executor = Executor::start(Arc::clone(&tape) as Arc<dyn EffectRunner>, sender);

        for index in 0..COUNT {
            executor.dispatch(carried(index as u64), publish(index));
        }
        let _ = collect_ids(&queue, COUNT);
        assert!(executor.shutdown(PATIENCE));

        assert_eq!(
            tape.wrote(),
            (0..COUNT).collect::<Vec<_>>(),
            "publish order is dispatch order"
        );
        assert_eq!(
            tape.max_in_flight(),
            1,
            "one publish at a time, so no two can race onto the same topic"
        );
    }

    #[test]
    fn shutdown_runs_publishes_already_queued() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(AlwaysOk), sender);

        for index in 0..8 {
            executor.dispatch(carried(index as u64), publish(index));
        }
        executor.dispatch(carried(50), Effect::SupplyOff);

        assert!(
            executor.shutdown(PATIENCE),
            "the publish lane is drained like the others"
        );
        let ids = collect_ids(&queue, 9);
        assert_eq!(ids.len(), 9, "every queued publish posted its completion");
    }

    #[test]
    fn a_stuck_publish_lane_does_not_hold_shutdown_open() {
        struct StuckPublish;
        impl EffectRunner for StuckPublish {
            fn run(&self, effect: &Effect) -> EffectOutcome {
                if matches!(effect, Effect::PublishSessionEvent(_)) {
                    std::thread::sleep(Duration::from_secs(30));
                }
                EffectOutcome::Ok
            }
        }

        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(StuckPublish), sender);
        executor.dispatch(carried(1), publish(0));
        executor.dispatch(carried(2), Effect::SupplyOff);

        // Safe state still reaches the board while the publish lane is wedged.
        let (id, _) = done_of(queue.recv_timeout(PATIENCE).expect("safety completion"));
        assert_eq!(id, carried(2));

        assert!(
            !executor.shutdown(Duration::from_millis(100)),
            "a wedged publish lane is abandoned at the deadline, never joined"
        );
    }

    #[test]
    fn a_panicking_publish_leaves_the_publish_lane_alive() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(PanicOn(publish(0))), sender);

        executor.dispatch(carried(1), publish(0));
        executor.dispatch(carried(2), publish(1));

        let first = done_of(queue.recv_timeout(PATIENCE).expect("completion"));
        assert_eq!(first.0, carried(1));
        assert!(matches!(first.1, EffectOutcome::Failed(_)));

        let second = done_of(queue.recv_timeout(PATIENCE).expect("completion"));
        assert_eq!(second.0, carried(2));
        assert_eq!(second.1, EffectOutcome::Ok);
        assert!(executor.shutdown(PATIENCE));
    }

    #[test]
    fn every_dispatched_effect_posts_its_completion() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(AlwaysOk), sender);

        executor.dispatch(carried(1), Effect::SupplyOff);
        executor.dispatch(carried(2), Effect::PwmOff);

        let mut ids = collect_ids(&queue, 2);
        ids.sort();
        assert_eq!(ids, vec![carried(1), carried(2)]);
        assert!(executor.shutdown(PATIENCE));
    }

    #[test]
    fn a_panicking_effect_fails_and_leaves_the_worker_alive() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(PanicOn(Effect::PwmOff)), sender);

        // Repeated failures on one device must not lose its serial worker. More
        // failures than the lane has threads, which is one.
        let count = 4;
        for index in 0..count {
            executor.dispatch(carried(index as u64), Effect::PwmOff);
        }
        for _ in 0..count {
            let (_, outcome) = done_of(
                queue
                    .recv_timeout(PATIENCE)
                    .expect("a panicking effect must still post completion"),
            );
            match outcome {
                EffectOutcome::Failed(message) => assert!(
                    message.contains("effect blew up"),
                    "the panic message must survive: {message}"
                ),
                EffectOutcome::Ok => panic!("a panicking effect must not report Ok"),
            }
        }

        // The same device worker still works afterwards.
        executor.dispatch(carried(999), Effect::PwmOn(50.0));
        let (id, outcome) = done_of(
            queue
                .recv_timeout(PATIENCE)
                .expect("the pool must survive a panic"),
        );
        assert_eq!(id, carried(999));
        assert_eq!(outcome, EffectOutcome::Ok);
        assert!(executor.shutdown(PATIENCE));
    }

    #[test]
    fn a_panicking_safety_effect_leaves_the_safety_thread_alive() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(PanicOn(Effect::SupplyOff)), sender);

        executor.dispatch(carried(1), Effect::SupplyOff);
        executor.dispatch(carried(2), Effect::UnlockConnector);

        let first = done_of(queue.recv_timeout(PATIENCE).expect("completion"));
        assert_eq!(first.0, carried(1));
        assert!(matches!(first.1, EffectOutcome::Failed(_)));

        let second = done_of(queue.recv_timeout(PATIENCE).expect("completion"));
        assert_eq!(second.0, carried(2));
        assert_eq!(second.1, EffectOutcome::Ok);
        assert!(executor.shutdown(PATIENCE));
    }

    #[test]
    fn shutdown_runs_effects_already_queued() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(
            Arc::new(SlowOrdinary {
                block: Duration::from_millis(20),
            }),
            sender,
        );

        for index in 0..10u64 {
            executor.dispatch(carried(index), Effect::PwmOff);
        }
        executor.dispatch(carried(50), Effect::AllowPowerOn(false));
        executor.dispatch(carried(51), Effect::SupplyOff);

        assert!(
            executor.shutdown(PATIENCE),
            "queued safe state effects must run before teardown returns"
        );

        let ids = collect_ids(&queue, 12);
        assert!(ids.contains(&carried(50)));
        assert!(ids.contains(&carried(51)));
    }

    #[test]
    fn dispatch_after_shutdown_is_reported_not_silent() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(AlwaysOk), sender);
        assert!(executor.shutdown(PATIENCE));
        // The executor is consumed by shutdown, so a late dispatch is not
        // representable. Nothing arrived after teardown.
        assert!(queue.try_recv().is_err());
    }

    /// Poisons a mutex the only way it can be poisoned in production: a thread
    /// panics while holding the lock.
    fn poison<T>(lock: &Mutex<T>)
    where
        T: Send + Sync,
    {
        let panicked = std::thread::scope(|scope| {
            scope
                .spawn(|| {
                    let _held = lock.lock().expect("not poisoned yet");
                    panic!("poisoning the lock");
                })
                .join()
        });
        assert!(panicked.is_err(), "the poisoning thread must have panicked");
    }

    #[test]
    fn a_poisoned_liveness_count_does_not_wedge_shutdown() {
        // Every worker retires through this count and shutdown waits on it. A
        // panic treated as fatal here loses the workers and the bounded wait at
        // the same time, so teardown would report a failure it did not have.
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(AlwaysOk), sender);
        executor.dispatch(carried(1), Effect::SupplyOff);
        let _ = collect_ids(&queue, 1);

        poison(&executor.liveness.remaining);

        assert!(
            executor.shutdown(PATIENCE),
            "a poisoned worker count must not turn an orderly teardown into a timeout"
        );
    }

    #[test]
    fn a_poisoned_liveness_count_still_admits_effects_first() {
        let (sender, queue) = event_channel();
        let executor = Executor::start(Arc::new(AlwaysOk), sender);
        poison(&executor.liveness.remaining);

        executor.dispatch(carried(1), Effect::SupplyOff);
        executor.dispatch(carried(2), publish(0));
        executor.dispatch(carried(3), Effect::PwmOff);
        let mut ids = collect_ids(&queue, 3);
        ids.sort();
        assert_eq!(ids, vec![carried(1), carried(2), carried(3)]);
        assert!(executor.shutdown(PATIENCE));
    }
    /// Holds an invocation before its observable device/store write. This is
    /// deliberately after entry to the production runner interface, reproducing
    /// the review probes rather than asserting an effect vector.
    struct HeldInvocation {
        first: std::sync::atomic::AtomicBool,
        entered: Sender<()>,
        release: Mutex<std::sync::mpsc::Receiver<()>>,
        completed: Sender<Effect>,
        store: Mutex<BTreeMap<String, String>>,
    }

    impl EffectRunner for HeldInvocation {
        fn run(&self, effect: &Effect) -> EffectOutcome {
            if self.first.swap(false, Ordering::SeqCst) {
                self.entered.send(()).unwrap();
                self.release.lock().unwrap().recv_timeout(PATIENCE).unwrap();
            }
            match effect {
                Effect::Persist { key, value } => {
                    self.store
                        .lock()
                        .unwrap()
                        .insert(key.clone(), value.clone());
                }
                Effect::PersistDelete { key } => {
                    self.store.lock().unwrap().remove(key);
                }
                _ => {}
            }
            self.completed.send(effect.clone()).unwrap();
            EffectOutcome::Ok
        }
    }

    fn held_pair(first: Effect, second: Effect, ordered: bool) -> BTreeMap<String, String> {
        let (entered_tx, entered_rx) = channel();
        let (release_tx, release_rx) = channel();
        let (completed_tx, completed_rx) = channel();
        let runner = Arc::new(HeldInvocation {
            first: std::sync::atomic::AtomicBool::new(true),
            entered: entered_tx,
            release: Mutex::new(release_rx),
            completed: completed_tx,
            store: Mutex::new(BTreeMap::new()),
        });
        let (events, queue) = event_channel();
        let executor = Executor::start(runner.clone(), events);
        executor.dispatch(carried(1), first.clone());
        entered_rx.recv_timeout(PATIENCE).unwrap();
        executor.dispatch(carried(2), second.clone());
        let early = completed_rx.recv_timeout(if ordered {
            Duration::from_millis(50)
        } else {
            PATIENCE
        });
        // Always release and join before asserting, including a regression.
        release_tx.send(()).unwrap();
        assert!(executor.shutdown(PATIENCE));
        if ordered {
            assert_eq!(
                early,
                Err(std::sync::mpsc::RecvTimeoutError::Timeout),
                "{first:?} must finish before {second:?}"
            );
            assert_eq!(completed_rx.recv_timeout(PATIENCE).unwrap(), first);
            assert_eq!(completed_rx.recv_timeout(PATIENCE).unwrap(), second);
            assert_eq!(collect_ids(&queue, 2), vec![carried(1), carried(2)]);
        } else {
            assert_eq!(
                early.unwrap(),
                second,
                "independent lane must progress while {first:?} is held"
            );
            assert_eq!(completed_rx.recv_timeout(PATIENCE).unwrap(), first);
            let mut ids = collect_ids(&queue, 2);
            ids.sort();
            assert_eq!(ids, vec![carried(1), carried(2)]);
        }
        let stored = runner.store.lock().unwrap().clone();
        stored
    }

    #[test]
    fn commands_on_each_device_execute_fifo_through_completion() {
        use crate::core::effect::{ChargingPhase, SupplyMode};
        let setpoint = Effect::SetSupplySetpoint {
            mode: SupplyMode::Import,
            voltage_v: 400.0,
            current_a: 0.0,
        };
        let mode = Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::Other };
        for (first, second) in [
            (setpoint.clone(), mode.clone()),
            (mode, setpoint),
            (Effect::PwmOn(50.0), Effect::PwmOff),
            (Effect::PwmOff, Effect::PwmOn(50.0)),
            (Effect::AllowPowerOn(true), Effect::PwmOn(50.0)),
            (Effect::PwmOn(50.0), Effect::BspEnable(true)),
            (
                Effect::SetOvercurrentLimit(16.0),
                Effect::SwitchThreePhases(false),
            ),
            (Effect::ImdStart, Effect::ImdStop),
            (Effect::ImdStop, Effect::ImdStart),
            (
                Effect::ImdSelfTest {
                    id: identities()[0],
                    voltage_v: 400.0,
                },
                Effect::ImdStart,
            ),
            (Effect::OverVoltageStart, Effect::OverVoltageStop),
            (Effect::OverVoltageStop, Effect::OverVoltageStart),
            (Effect::LockConnector, Effect::LockConnector),
        ] {
            held_pair(first, second, true);
        }
    }

    #[test]
    fn a_completed_transaction_cannot_be_resurrected_by_a_late_store() {
        let stored = held_pair(
            Effect::Persist {
                key: "evse_session".into(),
                value: "old-session".into(),
            },
            Effect::PersistDelete {
                key: "evse_session".into(),
            },
            true,
        );
        assert!(stored.is_empty());
    }

    #[test]
    fn a_live_transaction_cannot_be_erased_by_an_older_delete() {
        let stored = held_pair(
            Effect::PersistDelete {
                key: "evse_session".into(),
            },
            Effect::Persist {
                key: "evse_session".into(),
                value: "new-session".into(),
            },
            true,
        );
        assert_eq!(
            stored.get("evse_session").map(String::as_str),
            Some("new-session")
        );
    }

    #[test]
    fn there_is_no_new_cross_device_or_device_store_publish_order() {
        use crate::core::effect::{ChargingPhase, SupplyMode};
        let supply = Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::Other };
        let store = Effect::PersistDelete {
            key: "evse_session".into(),
        };
        for (first, second) in [
            (supply.clone(), Effect::ImdStart),
            (Effect::ImdStart, Effect::PwmOn(50.0)),
            (Effect::PwmOn(50.0), Effect::OverVoltageStart),
            (Effect::OverVoltageStart, Effect::LockConnector),
            (Effect::LockConnector, store.clone()),
            (store, publish(0)),
            (publish(0), supply),
        ] {
            held_pair(first, second, false);
        }
    }

    #[test]
    fn deferred_safety_bypass_still_overtakes_an_in_flight_device_call() {
        use crate::core::effect::{ChargingPhase, SupplyMode};
        // This pins the explicitly deferred exception, NOT a safety closure:
        // stale device actuation can still land after the bypassed safety call.
        for (first, second) in [
            (Effect::SetSupplyMode { mode: SupplyMode::Export, phase: ChargingPhase::Other }, Effect::SupplyOff),
            (Effect::PwmOn(50.0), Effect::AllowPowerOn(false)),
            (Effect::PwmOn(50.0), Effect::BspEnable(false)),
            (Effect::PwmOn(50.0), Effect::SetCpState(CpState::F)),
            (Effect::LockConnector, Effect::UnlockConnector),
        ] {
            held_pair(first, second, false);
        }
    }
}
