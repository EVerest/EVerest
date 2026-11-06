// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "CurvePlayer.hpp"

#include "Events.hpp"
#include "FsmContext.hpp"

#include <everest/logging.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace module {

void CurvePlayer::reset() {
    clear_loop();
    steps_.clear();
}

void CurvePlayer::clear_loop() {
    if (loop_start_idx_.has_value() && loop_end_idx_.has_value()) {
        // The body keeps the due times its rewinds gave it.
        for (std::size_t i = *loop_start_idx_; i < *loop_end_idx_; ++i) {
            steps_[i].step.at += loop_shift_;
        }
    }
    loop_start_idx_.reset();
    loop_end_idx_.reset();
    loop_shift_ = std::chrono::milliseconds{0};
}

std::optional<std::size_t> CurvePlayer::next_pending() const {
    std::optional<std::size_t> next;
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        if (steps_[i].pending && (!next.has_value() || due_at(i) < due_at(*next))) {
            next = i;
        }
    }
    return next;
}

void CurvePlayer::try_rewind_loop() {
    if (!loop_start_idx_.has_value() || !loop_end_idx_.has_value()) {
        return;
    }
    const auto body_begin = steps_.begin() + static_cast<std::ptrdiff_t>(*loop_start_idx_);
    const auto body_end = steps_.begin() + static_cast<std::ptrdiff_t>(*loop_end_idx_);
    if (std::any_of(body_begin, body_end, [](const Slot& s) { return s.pending; })) {
        return;
    }
    std::for_each(body_begin, body_end, [](Slot& s) { s.pending = true; });
    // Rewinding the body preserves due times of points outside it.
    const auto earliest =
        std::min_element(body_begin, body_end, [](const Slot& a, const Slot& b) { return a.step.at < b.step.at; });
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
    loop_shift_ = elapsed - earliest->step.at;
}

bool CurvePlayer::within_loop_body(std::size_t idx) const {
    return loop_start_idx_.has_value() && loop_end_idx_.has_value() && idx >= *loop_start_idx_ && idx < *loop_end_idx_;
}

std::chrono::milliseconds CurvePlayer::due_at(std::size_t idx) const {
    return steps_[idx].step.at + (within_loop_body(idx) ? loop_shift_ : std::chrono::milliseconds{0});
}

void CurvePlayer::dispatch(std::size_t idx, FsmContext& ctx) {
    // Inside a marked loop the same step will be dispatched again after a
    // rewind, so we must keep the stored Event intact and copy. The non-loop
    // path keeps the move to avoid an extra deep copy on the common case
    // (most variant alternatives currently hold nested vectors).
    if (within_loop_body(idx)) {
        ctx.enqueue(steps_[idx].step.ev);
    } else {
        ctx.enqueue(std::move(steps_[idx].step.ev));
    }
    steps_[idx].pending = false;
    try_rewind_loop();
}

void CurvePlayer::arm_next(FsmContext& ctx) {
    // Flush any past-due steps inline, then arm a positive ms delay for the
    // earliest future step. A zero arm would disarm the underlying timerfd
    // (timerfd_settime treats it_value=0 as disarm), so we must never call
    // arm_curve_timer with a non-positive delay.
    //
    // Forward-progress guard: a marked loop whose body is entirely past-due
    // (e.g. every step at offset 0) would spin here forever. Cap inline
    // iterations at a generous bound; if we hit it, clear the loop and run
    // the remaining pending steps once.
    const std::size_t max_iterations = steps_.size() + 128;
    std::size_t iterations = 0;
    while (const auto idx = next_pending()) {
        if (++iterations > max_iterations) {
            // Disarm the loop rather than throttle it. Arming 1 ms and returning leaves the
            // dispatcher spinning at 1 kHz forever, which is a guard failing open: the run is
            // already wrong, and the only way to stop it is to stop looping.
            EVLOG_warning << "CurvePlayer: marked loop has no forward progress; "
                             "disarming the loop and running the remaining steps once";
            clear_loop();
        }
        auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
        auto delay_ms = due_at(*idx) - elapsed;
        if (delay_ms > std::chrono::milliseconds(0)) {
            ctx.arm_curve_timer(delay_ms);
            return;
        }
        dispatch(*idx, ctx);
    }
}

std::size_t CurvePlayer::append_steps(std::vector<CurveStep> steps, bool loop, FsmContext& ctx) {
    if (steps.empty()) {
        return steps_.size();
    }
    if (!active()) {
        // No active curve: seed start_at_ now so the new steps' offsets
        // are measured from this moment.
        start_at_ = std::chrono::steady_clock::now();
    }

    // Rebase onto the running curve's clock. The offsets arrive relative to
    // this call; steps_ is keyed on time since start_at_.
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_at_);
    const auto first_idx = steps_.size();
    for (auto& s : steps) {
        s.at += elapsed;
        steps_.push_back(Slot{std::move(s)});
    }

    // Marked before arming, not after. arm_next dispatches everything already due, so a block
    // whose first point is at offset 0 would be drained before a caller that marks afterwards
    // could say it loops, and the loop would silently never repeat.
    if (loop) {
        mark_loop(first_idx, steps_.size());
    }

    arm_next(ctx);
    return first_idx;
}

void CurvePlayer::mark_loop(std::size_t begin_idx, std::size_t end_idx) {
    if (begin_idx >= end_idx || end_idx > steps_.size() || begin_idx >= steps_.size()) {
        EVLOG_warning << "CurvePlayer: invalid mark_loop range [" << begin_idx << ", " << end_idx
                      << ") for step_count=" << steps_.size() << "; ignoring";
        return;
    }
    clear_loop();
    loop_start_idx_ = begin_idx;
    loop_end_idx_ = end_idx;
}

void CurvePlayer::on_timer_fire(FsmContext& ctx) {
    const auto idx = next_pending();
    if (!idx.has_value()) {
        return;
    }
    dispatch(*idx, ctx);
    arm_next(ctx);
}

} // namespace module
