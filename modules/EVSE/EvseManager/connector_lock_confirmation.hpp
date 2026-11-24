// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <generated/types/connector_lock.hpp>

#include <everest/util/async/monitor.hpp>

#include <chrono>
#include <optional>

namespace module {

/// Tracks whether the connector lock confirmed the last lock command by reporting Locked within a timeout.
/// Thread-safe: lock commands arrive while the IECStateMachine holds its own lock, lock states from the
/// connector_lock driver and queries from the Charger and the HLC handlers.
class ConnectorLockConfirmation {
public:
    using LockState = types::connector_lock::LockState;
    using Clock = std::chrono::steady_clock;

    enum class Event {
        None,
        TimedOut,
        ConfirmedAfterTimeout,
    };

    /// 0 disables the check: the lock then always counts as confirmed.
    void set_timeout(std::chrono::milliseconds timeout) {
        state.handle()->timeout = timeout;
    }

    std::chrono::milliseconds timeout() const {
        return state.handle()->timeout;
    }

    void lock_commanded(Clock::time_point now = Clock::now()) {
        auto s = state.handle();
        if (not s->pending_since.has_value()) {
            s->pending_since = now;
        }
    }

    /// A Locked reported before the unlock command does not confirm a later lock command.
    void unlock_commanded() {
        auto s = state.handle();
        s->pending_since.reset();
        s->lock_state = LockState::Unknown;
    }

    /// The last reported state is kept until the next unlock command: a lock that stays Locked between sessions,
    /// as in captive cable mode, publishes no new state.
    void lock_state_reported(LockState lock_state) {
        auto s = state.handle();
        s->lock_state = lock_state;
        if (lock_state != LockState::Locked) {
            return;
        }
        s->pending_since.reset();
        if (s->timed_out) {
            s->timed_out = false;
            s->confirmed_after_timeout = true;
        }
    }

    bool confirmed() const {
        return confirmed(*state.handle());
    }

    /// For an authorized session that may only continue once the lock is confirmed. Without a pending lock
    /// command, e.g. because the lock never confirmed an earlier one, the timeout runs from now.
    bool confirmed_or_start_wait(Clock::time_point now = Clock::now()) {
        auto s = state.handle();
        if (confirmed(*s)) {
            return true;
        }
        if (not s->pending_since.has_value()) {
            s->pending_since = now;
        }
        return false;
    }

    /// Reports each timeout, and a Locked report after a timeout, exactly once.
    Event poll(Clock::time_point now = Clock::now()) {
        auto s = state.handle();
        if (s->confirmed_after_timeout) {
            s->confirmed_after_timeout = false;
            return Event::ConfirmedAfterTimeout;
        }
        if (s->timeout.count() == 0 or not s->pending_since.has_value()) {
            return Event::None;
        }
        // A lock that already reports Locked publishes nothing new on a further lock command.
        if (s->lock_state == LockState::Locked) {
            s->pending_since.reset();
            return Event::None;
        }
        if (now - s->pending_since.value() < s->timeout) {
            return Event::None;
        }
        s->pending_since.reset();
        s->timed_out = true;
        return Event::TimedOut;
    }

private:
    struct State {
        std::chrono::milliseconds timeout{0};
        LockState lock_state{LockState::Unknown};
        std::optional<Clock::time_point> pending_since;
        bool timed_out{false};
        bool confirmed_after_timeout{false};
    };

    static bool confirmed(const State& s) {
        return s.timeout.count() == 0 or s.lock_state == LockState::Locked;
    }

    mutable everest::lib::util::monitor<State> state;
};

} // namespace module
