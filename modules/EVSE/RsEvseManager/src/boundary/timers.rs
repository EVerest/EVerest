// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! One thread, one min-heap of deadlines.
//!
//! The core never sleeps and never reads a clock, so every wait it needs is an
//! `Effect::StartTimer` and every expiry re-enters as `Event::Timer` on the same
//! queue as everything else. Cancellation is a generation bump rather than a heap
//! removal: a superseded entry stays in the heap and is discarded on expiry, so
//! arming is O(log n) with no scan.

use std::cmp::Ordering;
use std::collections::{BinaryHeap, HashMap};
use std::sync::{Arc, Condvar, Mutex};
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use crate::boundary::event_loop::EventSender;
use crate::core::effect::TimerId;
use crate::core::event::Event;

/// A deadline as it sits in the heap. Ordered by deadline; id and generation only
/// break ties so the ordering is total.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Armed {
    deadline: Instant,
    id: TimerId,
    generation: u64,
}

impl Ord for Armed {
    fn cmp(&self, other: &Self) -> Ordering {
        self.deadline
            .cmp(&other.deadline)
            .then(self.id.cmp(&other.id))
            .then(self.generation.cmp(&other.generation))
    }
}

impl PartialOrd for Armed {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

/// A heap entry ordered so that `BinaryHeap::peek` yields the nearest deadline.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
struct Nearest(Armed);

impl Ord for Nearest {
    fn cmp(&self, other: &Self) -> Ordering {
        other.0.cmp(&self.0)
    }
}

impl PartialOrd for Nearest {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Default)]
struct State {
    heap: BinaryHeap<Nearest>,
    /// The generation currently live per timer id. Absence means cancelled or
    /// already delivered, so nothing for that id may fire.
    live: HashMap<TimerId, u64>,
    stop: bool,
    /// Ends the thread the way a defect would, so the death notice is exercised
    /// through the real thread rather than only through the guard in isolation.
    /// Compiled out of every non test build.
    #[cfg(test)]
    kill: bool,
}

struct Shared {
    state: Mutex<State>,
    wake: Condvar,
}

/// Arms and cancels timers. Cheap to clone and safe to call from the writer
/// thread; every operation is a lock plus a notify and never blocks on I/O.
#[derive(Clone)]
pub struct TimerHandle {
    shared: Arc<Shared>,
}

impl TimerHandle {
    /// Arm `id` to fire after `after` carrying `generation`. Supersedes any
    /// earlier arming of the same id.
    pub fn arm(&self, id: TimerId, generation: u64, after: Duration) {
        let deadline = Instant::now() + after;
        {
            let mut state = self
                .shared
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            if state.stop {
                return;
            }
            state.live.insert(id, generation);
            state.heap.push(Nearest(Armed {
                deadline,
                id,
                generation,
            }));
        }
        // Woken unconditionally: the thread recomputes the nearest deadline, so
        // this covers the nearer-timer case without comparing here.
        self.shared.wake.notify_one();
    }

    /// Cancel `id`. Any entry already in the heap becomes stale and is dropped on
    /// expiry rather than delivered.
    pub fn cancel(&self, id: TimerId) {
        {
            let mut state = self
                .shared
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            state.live.remove(&id);
        }
        self.shared.wake.notify_one();
    }

    /// Ends the timer thread without telling it to stop, which is what a defect
    /// in it would look like from outside.
    #[cfg(test)]
    fn kill_the_thread_for_test(&self) {
        self.shared
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .kill = true;
        self.shared.wake.notify_all();
        // The thread poisons the state on its way out, so wait for that rather
        // than for a duration.
        while !self.shared.state.is_poisoned() {
            std::thread::yield_now();
        }
    }

    /// Timers still eligible to fire. Diagnostics and tests only.
    pub fn live_count(&self) -> usize {
        self.shared
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .live
            .len()
    }
}

/// Owns the timer thread. Dropping without `stop` leaves the thread parked on the
/// condvar, so `stop` is the only correct teardown.
pub struct TimerService {
    shared: Arc<Shared>,
    thread: Option<JoinHandle<()>>,
}

impl TimerService {
    pub fn start(events: EventSender) -> Self {
        let shared = Arc::new(Shared {
            state: Mutex::new(State::default()),
            wake: Condvar::new(),
        });
        let worker = Arc::clone(&shared);
        let thread = std::thread::Builder::new()
            .name("rsevse-timers".into())
            .spawn(move || run(worker, events))
            .expect("failed to spawn timer thread");
        Self {
            shared,
            thread: Some(thread),
        }
    }

    pub fn handle(&self) -> TimerHandle {
        TimerHandle {
            shared: Arc::clone(&self.shared),
        }
    }

    /// Stops the thread and joins it. Called last in shutdown, after the effect
    /// threads are done, so no safe state effect can still be waiting on a timer.
    pub fn stop(mut self) {
        {
            let mut state = self
                .shared
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            state.stop = true;
            state.heap.clear();
            state.live.clear();
        }
        self.shared.wake.notify_all();
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

impl Drop for TimerService {
    fn drop(&mut self) {
        if self.thread.is_none() {
            return;
        }
        {
            let mut state = self
                .shared
                .state
                .lock()
                .unwrap_or_else(|poisoned| poisoned.into_inner());
            state.stop = true;
        }
        self.shared.wake.notify_all();
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

/// Reports a timer thread that stopped without being told to.
///
/// This thread is the only source of deadlines in the module, so if it goes the
/// cable check stage timeouts, the control pilot X1 pause, the five percent
/// fallback window and the bounded wait before a fatal error releases a latched
/// vehicle all stop at once, and all silently. The writer cannot notice by
/// itself: it wakes on events, and the events it is waiting for are exactly the
/// ones that stopped arriving.
///
/// A guard rather than a supervisor, deliberately. It adds no thread, no poll
/// and no liveness registry; it costs one clone of a sender that already exists
/// and it reports on the way out of the frame that failed. `Drop` runs while a
/// panic unwinds, and the workspace sets no `panic` strategy, so unwinding is
/// what a panic does here. `the_guard_fires_while_a_panic_unwinds` holds that.
///
/// Posting the notice is all this does. `Core::apply` takes the port to safe
/// state on `Event::TimerThreadDied` itself, de-energizing and releasing the
/// vehicle, which
/// `a_dead_timer_thread_de_energizes_and_releases_the_vehicle_at_once` holds.
struct DeathNotice {
    events: EventSender,
    /// Set only by the thread itself, immediately before it returns because it
    /// was told to stop. Teardown joins the thread, so without this every clean
    /// shutdown would report a death.
    orderly: bool,
}

impl DeathNotice {
    fn new(events: EventSender) -> Self {
        Self {
            events,
            orderly: false,
        }
    }

    fn stopping_on_purpose(&mut self) {
        self.orderly = true;
    }
}

impl Drop for DeathNotice {
    fn drop(&mut self) {
        if self.orderly {
            return;
        }
        // The send result is deliberately not examined. `EventSender::send`
        // already logs a closed receiver, and a closed receiver means the writer
        // is gone and the process is tearing down regardless.
        self.events.send(Event::TimerThreadDied);
    }
}

fn run(shared: Arc<Shared>, events: EventSender) {
    let mut notice = DeathNotice::new(events.clone());
    loop {
        let mut state = shared
            .state
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner());
        if state.stop {
            notice.stopping_on_purpose();
            return;
        }
        #[cfg(test)]
        if state.kill {
            panic!("timer thread ended by a test");
        }

        let next = state.heap.peek().map(|Nearest(armed)| armed.deadline);
        let now = Instant::now();
        match next {
            None => {
                let _unused = shared
                    .wake
                    .wait(state)
                    .unwrap_or_else(|poisoned| poisoned.into_inner());
            }
            Some(deadline) if deadline > now => {
                let _unused = shared
                    .wake
                    .wait_timeout(state, deadline - now)
                    .unwrap_or_else(|poisoned| poisoned.into_inner());
            }
            Some(_) => {
                let Nearest(armed) = state.heap.pop().expect("peek implies pop");
                let deliverable = state.live.get(&armed.id).copied() == Some(armed.generation);
                if deliverable {
                    // One shot. Nothing fires twice without being re-armed.
                    state.live.remove(&armed.id);
                }
                drop(state);
                if deliverable {
                    events.send(Event::Timer {
                        id: armed.id,
                        generation: armed.generation,
                    });
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::boundary::event_loop::event_channel;

    const SHORT: Duration = Duration::from_millis(30);
    const PATIENCE: Duration = Duration::from_millis(2000);

    /// Drains the queue and answers whether a death notice is among what
    /// arrived. Reads to exhaustion rather than taking the first event, so an
    /// ordinary expiry ahead of a notice cannot hide one.
    fn saw_death_notice(queue: &crate::boundary::event_loop::EventQueue) -> bool {
        let mut saw = false;
        while let Ok(event) = queue.recv_timeout(SHORT) {
            if matches!(event, Event::TimerThreadDied) {
                saw = true;
            }
        }
        saw
    }

    /// Poisons the timer state the only way it can be poisoned in production:
    /// a thread panics while holding the lock.
    fn poison(shared: &Arc<Shared>) {
        let shared = Arc::clone(shared);
        let panicked = std::thread::spawn(move || {
            let _held = shared.state.lock().expect("not poisoned yet");
            panic!("poisoning the timer state");
        })
        .join();
        assert!(panicked.is_err(), "the poisoning thread must have panicked");
    }

    fn timer_of(event: Event) -> (TimerId, u64) {
        match event {
            Event::Timer { id, generation } => (id, generation),
            other => panic!("expected a timer event, got {other:?}"),
        }
    }

    #[test]
    fn an_armed_timer_delivers_once() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        timers.arm(TimerId(7), 1, SHORT);

        let event = queue.recv_timeout(PATIENCE).expect("timer must deliver");
        assert_eq!(timer_of(event), (TimerId(7), 1));
        assert!(
            queue.recv_timeout(SHORT * 4).is_err(),
            "a one shot timer must not fire twice"
        );
        service.stop();
    }

    #[test]
    fn a_cancelled_timer_does_not_deliver() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        timers.arm(TimerId(11), 1, SHORT);
        timers.cancel(TimerId(11));

        assert!(
            queue.recv_timeout(SHORT * 10).is_err(),
            "a cancelled timer must never reach the queue"
        );
        assert_eq!(timers.live_count(), 0);
        service.stop();
    }

    #[test]
    fn a_re_armed_timer_delivers_only_the_newest_generation() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        // The first arming is deliberately the nearer deadline, so the stale
        // entry is the one the heap reaches first.
        timers.arm(TimerId(3), 1, SHORT);
        timers.arm(TimerId(3), 2, SHORT * 4);

        let event = queue.recv_timeout(PATIENCE).expect("timer must deliver");
        assert_eq!(
            timer_of(event),
            (TimerId(3), 2),
            "the superseded generation must be discarded, not delivered"
        );
        assert!(
            queue.recv_timeout(SHORT * 4).is_err(),
            "only one delivery per re-armed timer"
        );
        service.stop();
    }

    #[test]
    fn arming_a_nearer_timer_wakes_the_thread() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        // The thread parks on the far deadline first. Without a wake on arm, the
        // near timer would be reported roughly ten seconds late.
        timers.arm(TimerId(1), 1, Duration::from_secs(10));
        std::thread::sleep(SHORT);
        timers.arm(TimerId(2), 1, SHORT);

        let event = queue
            .recv_timeout(PATIENCE)
            .expect("near timer must deliver");
        assert_eq!(timer_of(event), (TimerId(2), 1));
        service.stop();
    }

    #[test]
    fn timers_fire_in_deadline_order_not_arming_order() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        timers.arm(TimerId(30), 1, SHORT * 3);
        timers.arm(TimerId(10), 1, SHORT);
        timers.arm(TimerId(20), 1, SHORT * 2);

        let mut order = Vec::new();
        for _ in 0..3 {
            let event = queue.recv_timeout(PATIENCE).expect("timer must deliver");
            order.push(timer_of(event).0);
        }
        assert_eq!(order, vec![TimerId(10), TimerId(20), TimerId(30)]);
        service.stop();
    }

    #[test]
    fn stop_is_idempotent_against_a_pending_deadline() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        timers.arm(TimerId(5), 1, Duration::from_secs(30));
        service.stop();

        // The thread is joined, so nothing can arrive afterwards.
        assert!(queue.recv_timeout(SHORT).is_err());
        // Arming after stop is a no-op rather than a panic.
        timers.arm(TimerId(6), 1, SHORT);
        assert!(queue.recv_timeout(SHORT * 4).is_err());
    }

    #[test]
    fn a_poisoned_timer_state_still_arms_and_delivers() {
        // A poisoned lock here carries no corruption: the heap and the live map
        // are whatever the panicking thread left, and both are consistent. The
        // deadline must still arrive, because this thread is the only source of
        // deadlines in the module.
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        poison(&service.shared);

        timers.arm(TimerId(21), 1, SHORT);
        let event = queue
            .recv_timeout(PATIENCE)
            .expect("a deadline must survive a poisoned lock");
        assert_eq!(timer_of(event), (TimerId(21), 1));
        service.stop();
    }

    #[test]
    fn a_poisoned_timer_state_still_cancels_and_reports() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();

        timers.arm(TimerId(22), 1, Duration::from_secs(30));
        poison(&service.shared);

        assert_eq!(timers.live_count(), 1);
        timers.cancel(TimerId(22));
        assert_eq!(timers.live_count(), 0);
        assert!(queue.recv_timeout(SHORT).is_err());
        service.stop();
    }

    #[test]
    fn a_poisoned_timer_state_still_stops() {
        let (sender, _queue) = event_channel();
        let service = TimerService::start(sender);
        poison(&service.shared);
        // Teardown joins the thread, so this hangs or panics if either side
        // treats poisoning as fatal.
        service.stop();
    }

    #[test]
    fn a_timer_thread_that_dies_says_so() {
        // The whole point: the deadlines stop and somebody has to be told. The
        // writer cannot notice on its own, because it is asleep waiting for the
        // very events that stopped arriving.
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();
        timers.arm(TimerId(40), 1, Duration::from_secs(30));

        timers.kill_the_thread_for_test();

        assert!(
            saw_death_notice(&queue),
            "a timer thread that stopped without being told must post a death notice"
        );
        // The deadline it was holding is indeed gone, which is what the notice
        // is reporting.
        assert!(queue.recv_timeout(SHORT).is_err());
    }

    #[test]
    fn an_orderly_stop_posts_no_death_notice() {
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        service
            .handle()
            .arm(TimerId(41), 1, Duration::from_secs(30));

        service.stop();

        assert!(
            !saw_death_notice(&queue),
            "a clean shutdown must not raise a false alarm"
        );
    }

    #[test]
    fn dropping_the_service_posts_no_death_notice() {
        // `Drop` is the other orderly teardown: it sets the stop flag and joins,
        // so the thread it ends is not a thread that died.
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        service
            .handle()
            .arm(TimerId(42), 1, Duration::from_secs(30));

        drop(service);

        assert!(!saw_death_notice(&queue), "teardown by drop is not a death");
    }

    #[test]
    fn a_death_notice_survives_a_writer_that_is_already_gone() {
        // Shutdown order is not guaranteed. If the receiver went first there is
        // nobody to tell, and saying so must not itself become a second panic.
        let (sender, queue) = event_channel();
        let service = TimerService::start(sender);
        let timers = service.handle();
        drop(queue);

        timers.kill_the_thread_for_test();
        service.stop();
    }

    #[test]
    fn the_guard_fires_while_a_panic_unwinds() {
        // The claim the whole design rests on, checked rather than assumed: the
        // workspace sets no `panic` strategy, so unwinding is the default and a
        // guard on the stack of a panicking thread still runs.
        let (sender, queue) = event_channel();
        let panicked = std::thread::spawn(move || {
            let _notice = DeathNotice::new(sender);
            panic!("the timer thread blew up");
        })
        .join();

        assert!(panicked.is_err(), "the thread must have panicked");
        assert!(
            saw_death_notice(&queue),
            "unwinding runs Drop; if this fails the build turned on panic = abort"
        );
    }
}
