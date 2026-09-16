// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

//! Where a blocked caller waits for the verdict its command returns.
//!
//! Five `evse_manager` commands return a bool the C++ derives from the real
//! decision. This port posts commands onto the single event queue, so intake
//! has to wait for the core to reach that decision rather than guess it.
//!
//! Waiting here is safe, and the reason is a property of the writer: it does
//! only `recv`, a pure `apply` and an `mpsc` send, and every effect goes to a
//! lane worker instead, so the writer performs no I/O, calls no peer and can
//! never be blocked by the caller it owes an answer to. See
//! `docs/effect-ordering-timing.md`.
//!
//! The wait is bounded all the same, because two paths give no answer at all:
//! after the writer is gone `EventSender::send` discards the event, and an
//! event enqueued after the loop has stopped is never processed. A breach
//! answers `false` - the safest of the two values the interface has, not a
//! fact the module established. See [`Pending::wait`].

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::mpsc::{sync_channel, Receiver, SyncSender};
use std::sync::Mutex;
use std::time::Duration;

use crate::core::event::ReplyToken;

/// How long intake waits for the core to answer.
///
/// Chosen so that a breach means *stopped* rather than *busy*: two orders of
/// magnitude above the 50ms the loop already warns at, and far inside the
/// framework's own 300 second command timeout (`everest.cpp:38`). The full
/// reasoning, and what a caller may and may not read into the `false` a breach
/// produces, are in `docs/effect-ordering-timing.md`.
pub const REPLY_TIMEOUT: Duration = Duration::from_secs(5);

/// The callers currently blocked on a verdict, keyed by the token they posted.
#[derive(Debug, Default)]
pub struct Replies {
    next: AtomicU64,
    waiting: Mutex<HashMap<ReplyToken, SyncSender<bool>>>,
}

/// A registered wait. Dropping it without calling [`Pending::wait`] deregisters.
pub struct Pending<'a> {
    replies: &'a Replies,
    token: ReplyToken,
    answers: Receiver<bool>,
}

impl Replies {
    /// Registers a caller and hands back the token to post with the command.
    pub fn register(&self) -> Pending<'_> {
        let token = ReplyToken(self.next.fetch_add(1, Ordering::Relaxed));
        // Buffered, so the writer never blocks handing over an answer whose
        // caller has already stopped waiting for it.
        let (tx, answers) = sync_channel(1);
        self.lock().insert(token, tx);
        Pending {
            replies: self,
            token,
            answers,
        }
    }

    /// Completes a waiting caller. Silent when there is none: the caller's
    /// bound can expire first, and that is a result rather than a fault.
    pub fn answer(&self, token: ReplyToken, answer: bool) {
        if let Some(tx) = self.lock().remove(&token) {
            let _ = tx.send(answer);
        }
    }

    fn lock(&self) -> std::sync::MutexGuard<'_, HashMap<ReplyToken, SyncSender<bool>>> {
        // Held only across a map operation, never across a wait, so the
        // poisoning a panicking caller could leave behind carries no broken
        // invariant with it.
        self.waiting
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
    }
}

impl Pending<'_> {
    pub fn token(&self) -> ReplyToken {
        self.token
    }

    /// Blocks until the core answers or [`REPLY_TIMEOUT`] expires, and answers
    /// `false` on the breach.
    ///
    /// The interface has one bit and no third value, so **a `false` from here
    /// is not distinguishable from a decided `false`**. That is a contract on
    /// the five commands rather than an implementation detail, and it is
    /// weakest for `pause_charging`, `resume_charging` and `stop_transaction`,
    /// whose `false` means "no transaction was active" - a claim about state
    /// that a breach asserts without having established it. The breach logs,
    /// which is the only place the two cases are separable.
    pub fn wait(self) -> bool {
        self.wait_for(REPLY_TIMEOUT)
    }

    fn wait_for(self, timeout: Duration) -> bool {
        let answered = self.answers.recv_timeout(timeout);
        // Deregister either way: on a breach nothing else would ever remove it.
        self.replies.lock().remove(&self.token);
        answered.unwrap_or_else(|_| {
            log::warn!("no verdict within {timeout:?}, answering false; the core is stopped or wedged");
            false
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;

    /// A verdict reaches the caller that asked for it.
    #[test]
    fn an_answer_reaches_the_waiting_caller() {
        let replies = Arc::new(Replies::default());
        let pending = replies.register();
        let token = pending.token();

        replies.answer(token, true);

        assert!(pending.wait());
    }

    /// And `false` is carried as a value, not as the absence of one, so a
    /// refusal is distinguishable from nobody answering.
    #[test]
    fn a_refusal_is_carried_as_a_value() {
        let replies = Arc::new(Replies::default());
        let pending = replies.register();
        let token = pending.token();

        replies.answer(token, false);

        assert!(!pending.wait());
    }

    /// The no-answer case: the core is gone or the event was never processed.
    /// The caller is released on the bound rather than held forever.
    #[test]
    fn a_caller_nobody_answers_is_released_false() {
        let replies = Replies::default();
        let pending = replies.register();

        assert!(!pending.wait_for(Duration::from_millis(50)));
    }

    /// Two callers in flight at once get their own verdicts. Distinct commands
    /// are distinct topics to the framework and so run concurrently; answering
    /// by arrival order rather than by token would cross them.
    #[test]
    fn concurrent_callers_get_their_own_verdicts() {
        let replies = Replies::default();
        let first = replies.register();
        let second = replies.register();
        assert_ne!(first.token(), second.token());

        // Answered in the opposite order to registration.
        replies.answer(second.token(), true);
        replies.answer(first.token(), false);

        assert!(!first.wait());
        assert!(second.wait());
    }

    /// A caller that has already given up does not block the writer, and does
    /// not leave its slot behind for the next token to trip over.
    #[test]
    fn answering_an_abandoned_caller_is_silent() {
        let replies = Replies::default();
        let pending = replies.register();
        let token = pending.token();
        assert!(!pending.wait_for(Duration::from_millis(50)));

        replies.answer(token, true);

        let left = replies.lock().len();
        assert_eq!(left, 0, "the slot was not left behind");
    }
}
