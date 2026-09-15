// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include "Events.hpp"

#include <everest_api_types/ev_simulator/API.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace module {

class FsmContext;

// Who a step belongs to. A preset's steps are the operator's run and outlive a
// session end that leaves the EV plugged; a session's charging curve ends with
// the session.
enum class StepOwner {
    Preset,
    SessionCurve,
};

// One step in a scenario's time-ordered script. `at` is the absolute offset
// from scenario start; steps with `at <= 0` fire immediately on start().
struct ScenarioStep {
    std::chrono::milliseconds at;
    Event ev;
    StepOwner owner{StepOwner::Preset};
};

// Stateful dispatcher driving multi-phase scenario presets. start() seeds a
// step-list and fires the offset-0 steps synchronously; on_timer_fire()
// dispatches the pending step due earliest, whatever its index or owner, and
// the scenario timer is armed for the next one. Steps due together fire in
// index order, which is the order they were added in.
// reset() clears any pending step-list so a fresh Unplugged session does not
// see stale timer fires.
class ScenarioDispatcher {
public:
    void start(everest::lib::API::V1_0::types::ev_simulator::ScenarioName name,
               const std::optional<everest::lib::API::V1_0::types::ev_simulator::ScenarioTimingOverrides>& timing,
               FsmContext& ctx);
    void on_timer_fire(FsmContext& ctx);
    void reset();
    // Drop the SessionCurve steps not yet dispatched, and a loop over them,
    // leaving the preset steps armed.
    void drop_session_steps(FsmContext& ctx);
    bool active() const {
        return next_pending().has_value();
    }
    // Current number of steps in the step-list (used by callers to compute
    // loop-segment indices around an append).
    std::size_t step_count() const {
        return steps_.size();
    }

    // Append steps to the running (or to-be-started) step-list, returning the index of the
    // first; the block stays contiguous. Offsets are relative to THIS call, not to scenario
    // start, and are rebased.
    //
    // `loop` is a parameter rather than a follow-up mark_loop call because this function
    // dispatches everything already due before returning, so a block starting at offset 0
    // would be drained and the range marked afterwards would name steps already passed.
    std::size_t append_steps(std::vector<ScenarioStep> steps, bool loop, FsmContext& ctx);

    // Mark a contiguous range of step indices [begin, end) as a loop segment.
    // Once every step of the body has fired, the body is pending again and its
    // clock moves so its earliest step is due at once; steps outside the body
    // keep their due times.
    // Invalid ranges (begin >= end, end > step_count(), or begin out of range)
    // are logged and ignored.
    void mark_loop(std::size_t begin_idx, std::size_t end_idx);

private:
    struct Slot {
        ScenarioStep step;
        bool pending{true};
    };

    void arm_next(FsmContext& ctx);
    // The pending step due earliest, the lowest index among those due together.
    std::optional<std::size_t> next_pending() const;
    // Once no step of the loop body is pending, make it pending again and shift
    // the body's clock so its earliest step is due now.
    void try_rewind_loop();
    void clear_loop();
    // True when idx lies inside a marked loop body. Used to choose between
    // move (one-shot) and copy (loop-bound, may revisit this index), and to
    // pick the clock a step is due on.
    bool within_loop_body(std::size_t idx) const;
    // When steps_[idx] is due, as an offset from start_at_.
    std::chrono::milliseconds due_at(std::size_t idx) const;
    // Enqueue steps_[idx]'s event to the FSM and mark it fired, then rewind a
    // loop it completes. Copies inside a loop body so a rewind can re-dispatch
    // the same step with its payload intact, and moves otherwise.
    void dispatch(std::size_t idx, FsmContext& ctx);

    // Fired steps stay in place, so indices handed to mark_loop stay valid.
    std::vector<Slot> steps_;
    std::chrono::steady_clock::time_point start_at_{};
    std::optional<std::size_t> loop_start_idx_;
    std::optional<std::size_t> loop_end_idx_;
    // How far the loop body's rewinds have moved its clock from start_at_.
    std::chrono::milliseconds loop_shift_{0};
};

} // namespace module
