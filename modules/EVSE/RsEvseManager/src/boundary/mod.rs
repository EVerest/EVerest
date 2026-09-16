// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! The boundary between the synchronous framework surface and the single writer.
//!
//! Framework callbacks do not touch `Core`. They append an `Event` to one
//! unbounded queue and return, which is why no lock protects core state and why
//! a slow callback cannot become a slow state machine.
//!
//! Threading topology, in full:
//!
//! - one writer thread owning `Core`,
//! - one safety thread executing only safety actuation,
//! - one ordered publish thread for this module's announcements and the
//!   metering/HLC calls whose order those announcements depend on,
//! - one FIFO worker per device and one for store writes/deletes; no new
//!   ordering between devices or between devices, store and publishes,
//!   `executor::safety_context` retains the safety bypass as an explicit
//!   exception to device FIFO pending the shutdown contract decision,
//! - one timer thread holding the deadline heap.
//!
//! Nothing else is spawned. Framework handler threads are the framework's.

pub mod event_loop;
pub mod executor;
pub mod random;
pub mod replies;
pub mod supply;
pub mod timers;

use std::sync::Arc;
use std::thread::JoinHandle;
use std::time::{Duration, Instant};

use crate::core::effect::{Effect, EffectId, TimerId};
use crate::core::event::{Event, ReplyToken};
use crate::core::Core;
use replies::Replies;

pub use event_loop::{
    drive, event_channel, EffectDispatch, EventQueue, EventSender, LoopStats, Reducer, StepOutcome,
    Writer, LATENCY_WARN, QUEUE_WARN_DEPTH,
};
pub use executor::{EffectRunner, Executor, SERIAL_LANES};
pub use timers::{TimerHandle, TimerService};

/// How long process shutdown waits for effect workers. This bounds the join,
/// not RPC execution: a worker still in a command is abandoned, not cancelled.
pub const EFFECT_DRAIN_TIMEOUT: Duration = Duration::from_secs(5);

impl Reducer for Core {
    fn apply(&mut self, event: Event, now: Instant) -> Vec<Effect> {
        Core::apply(self, event, now)
    }
}

/// The writer's outward face: effects to the executor, deadlines to the timer
/// thread.
pub struct Actuators {
    executor: Executor,
    timers: TimerHandle,
    replies: Arc<Replies>,
}

impl EffectDispatch for Actuators {
    fn dispatch(&self, id: Option<EffectId>, effect: Effect) {
        self.executor.dispatch(id, effect);
    }

    fn arm_timer(&self, id: TimerId, generation: u64, after: Duration) {
        self.timers.arm(id, generation, after);
    }

    fn cancel_timer(&self, id: TimerId) {
        self.timers.cancel(id);
    }

    fn answer(&self, reply: ReplyToken, answer: bool) {
        self.replies.answer(reply, answer);
    }
}

/// Handle on the running module. Holds intake so callbacks can be wired before
/// the loop exists, and owns the join.
pub struct LoopHandle {
    events: EventSender,
    thread: Option<JoinHandle<()>>,
}

impl LoopHandle {
    pub fn sender(&self) -> EventSender {
        self.events.clone()
    }

    pub fn stats(&self) -> Arc<LoopStats> {
        Arc::clone(self.events.stats())
    }

    /// Pushes `Event::Shutdown` and waits for safe state to be reached. The core
    /// emits its safe state effects, the executor drains them within
    /// `EFFECT_DRAIN_TIMEOUT`, then the timer thread stops.
    pub fn shutdown(mut self) {
        self.events.send(Event::Shutdown);
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }
}

impl Drop for LoopHandle {
    fn drop(&mut self) {
        if let Some(thread) = self.thread.take() {
            self.events.send(Event::Shutdown);
            let _ = thread.join();
        }
    }
}

/// Starts the writer, the effect threads and the timer thread.
///
/// `sender` and `queue` come from `event_channel` and are created before module
/// start, so events arriving from framework callbacks during startup queue up
/// rather than being lost.
pub fn start(
    core: Core,
    runner: Arc<dyn EffectRunner>,
    sender: EventSender,
    queue: EventQueue,
    replies: Arc<Replies>,
) -> LoopHandle {
    let events = sender.clone();
    let stats = Arc::clone(sender.stats());
    let thread = std::thread::Builder::new()
        .name("rsevse-writer".into())
        .spawn(move || {
            let timer_service = TimerService::start(sender.clone());
            let actuators = Actuators {
                executor: Executor::start(runner, sender),
                timers: timer_service.handle(),
                replies,
            };
            let mut writer = Writer::new(core, actuators, stats);

            drive(&mut writer, &queue);

            // Order matters: effects first, so safe state actuation actually
            // runs, then the timer thread, which nothing is waiting on any more.
            let (_core, actuators) = writer.into_parts();
            if !actuators.executor.shutdown(EFFECT_DRAIN_TIMEOUT) {
                log::error!("safe state effects did not complete within {EFFECT_DRAIN_TIMEOUT:?}");
            }
            timer_service.stop();
        })
        .expect("failed to spawn writer thread");

    LoopHandle {
        events,
        thread: Some(thread),
    }
}
