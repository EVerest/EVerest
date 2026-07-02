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

// One step in a scenario's time-ordered script. `at` is the absolute offset
// from scenario start; steps with `at <= 0` fire immediately on start().
struct ScenarioStep {
    std::chrono::milliseconds at;
    Event ev;
};

// Stateful dispatcher driving multi-phase scenario presets. start() seeds a
// step-list and fires the offset-0 steps synchronously; on_timer_fire()
// advances through the remaining steps as their scenario timer expires.
// reset() clears any pending step-list so a fresh Unplugged session does not
// see stale timer fires.
class ScenarioDispatcher {
public:
    void start(everest::lib::API::V1_0::types::ev_simulator::ScenarioName name,
               const std::optional<everest::lib::API::V1_0::types::ev_simulator::ScenarioTimingOverrides>& timing,
               FsmContext& ctx);
    void on_timer_fire(FsmContext& ctx);
    void reset();
    bool active() const {
        return next_idx_ < steps_.size();
    }
    // Current number of steps in the step-list (used by callers to compute
    // loop-segment indices around an append).
    std::size_t step_count() const {
        return steps_.size();
    }

    // Append additional steps to the running (or to-be-started) step-list.
    // Used by Plugged to splice a ChargingCurve's SetChargingCurrent events
    // into the scenario at their `at` offsets. If the dispatcher is idle
    // (no prior start()), this seeds the step-list and arms the timer for
    // the first non-immediate step.
    void append_steps(std::vector<ScenarioStep> steps, FsmContext& ctx);

    // Mark a contiguous range of step indices [begin, end) as a loop segment.
    // When the dispatcher would advance past `end`, it rewinds next_idx_ to
    // `begin` and re-bases `start_at_` so subsequent offsets stay consistent.
    // Invalid ranges (begin >= end, end > step_count(), or begin out of range)
    // are logged and ignored.
    void mark_loop(std::size_t begin_idx, std::size_t end_idx);

private:
    void arm_next(FsmContext& ctx);
    // If next_idx_ has just advanced past the marked loop end, rewind it to
    // the loop start and rebase start_at_ so subsequent offsets are measured
    // from `now - at_of_first_loop_step`. Shared between arm_next's
    // inline-flush path and on_timer_fire.
    void try_rewind_loop();
    // True when next_idx_ lies inside a marked loop body. Used to choose
    // between move (one-shot) and copy (loop-bound, may revisit this index).
    bool within_loop_body() const;
    // Enqueue steps_[next_idx_].ev to the FSM, copying when inside a loop
    // body so a rewind can re-dispatch the same step with its payload intact,
    // and moving otherwise. Advances next_idx_.
    void dispatch_current(FsmContext& ctx);

    std::vector<ScenarioStep> steps_;
    std::size_t next_idx_{0};
    std::chrono::steady_clock::time_point start_at_{};
    std::optional<std::size_t> loop_start_idx_;
    std::optional<std::size_t> loop_end_idx_;
};

} // namespace module
