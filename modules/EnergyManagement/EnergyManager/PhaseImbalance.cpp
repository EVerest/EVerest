// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "PhaseImbalance.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace module {

namespace {

constexpr std::array<Phase, 3> ALL_PHASES{Phase::L1, Phase::L2, Phase::L3};

std::optional<float> phase_value(const PhaseCurrents& currents, Phase phase) {
    switch (phase) {
    case Phase::L1:
        return currents.L1;
    case Phase::L2:
        return currents.L2;
    case Phase::L3:
    default:
        return currents.L3;
    }
}

// Per-phase current of a measurement, by the precedence phases_drawn_on() documents. A
// total is deliberately not spread: it cannot say which phase carries it.
PhaseCurrents per_phase_current(const ObservedMeasurement& measurement, float nominal_ac_voltage) {
    const auto& current = measurement.current_A;
    if (current.L1.has_value() or current.L2.has_value() or current.L3.has_value()) {
        return {current.L1, current.L2, current.L3};
    }
    if (not measurement.power_W.has_value() or nominal_ac_voltage <= 0.f) {
        return {};
    }
    const auto& power = measurement.power_W.value();
    const auto to_current = [nominal_ac_voltage](const std::optional<float>& watt) -> std::optional<float> {
        if (not watt.has_value()) {
            return std::nullopt;
        }
        return watt.value() / nominal_ac_voltage;
    };
    return {to_current(power.L1), to_current(power.L2), to_current(power.L3)};
}

PhaseReport& report_of(ImbalanceResult& result, Phase phase) {
    switch (phase) {
    case Phase::L1:
        return result.L1;
    case Phase::L2:
        return result.L2;
    case Phase::L3:
    default:
        return result.L3;
    }
}

constexpr float UNBOUNDED_A = std::numeric_limits<float>::infinity();

// Cap changes smaller than this are not worth a new limit.
constexpr float CAP_RESOLUTION_A = 0.05f;

// Rounds for the support to settle; past them the budgets count on no support at all.
constexpr int MAX_SUPPORT_ROUNDS = 16;

// One ordered pair of phases (p, q): the connectors that can load p but not q may add at
// most capacity_A to the difference between them.
struct Budget {
    Phase p;
    Phase q;
    float capacity_A;
    std::vector<std::size_t> members;
};

// Progressive filling: every free participant rises at a common level, clamped to [lo, hi],
// until a budget it takes part in is used up; its members stop there and the others rise
// on. Participants that are not free contribute load[i] to their budgets. Returns the
// value of every participant: the level it stopped at for the free ones, load[i] for the
// others. A free participant in no budget ends at hi.
std::vector<float> fill(const std::vector<Budget>& budgets, const std::vector<bool>& free, const std::vector<float>& lo,
                        const std::vector<float>& hi, const std::vector<float>& load) {
    const auto n = free.size();
    std::vector<float> value(n);
    std::vector<bool> open(n);
    for (std::size_t i = 0; i < n; i++) {
        value[i] = free[i] ? lo[i] : load[i];
        open[i] = free[i];
    }
    const auto at = [&](std::size_t i, double level) {
        return std::min<double>(hi[i], std::max<double>(lo[i], level));
    };

    while (std::find(open.begin(), open.end(), true) != open.end()) {
        double lowest = UNBOUNDED_A;
        std::vector<const Budget*> tight;
        for (const auto& budget : budgets) {
            double fixed = 0.0;
            std::vector<std::size_t> rising;
            for (const auto i : budget.members) {
                if (open[i]) {
                    rising.push_back(i);
                } else {
                    fixed += value[i];
                }
            }
            if (rising.empty()) {
                continue;
            }
            const auto total_at = [&](double level) {
                double total = fixed;
                for (const auto i : rising) {
                    total += at(i, level);
                }
                return total;
            };
            double level = 0.0;
            if (total_at(0.0) >= budget.capacity_A) {
                level = 0.0;
            } else if (total_at(UNBOUNDED_A) <= budget.capacity_A) {
                continue;
            } else {
                double low = 0.0;
                double high = std::max<double>(budget.capacity_A, 0.0);
                for (const auto i : rising) {
                    high = std::max<double>(high, lo[i]);
                }
                for (int k = 0; k < 64; k++) {
                    const double mid = (low + high) / 2.0;
                    (total_at(mid) < budget.capacity_A ? low : high) = mid;
                }
                level = (low + high) / 2.0;
            }
            if (level < lowest - 1e-6) {
                lowest = level;
                tight = {&budget};
            } else if (level <= lowest + 1e-6) {
                tight.push_back(&budget);
            }
        }
        if (tight.empty()) {
            for (std::size_t i = 0; i < n; i++) {
                if (open[i]) {
                    value[i] = hi[i];
                    open[i] = false;
                }
            }
            break;
        }
        for (const auto* budget : tight) {
            for (const auto i : budget->members) {
                if (open[i]) {
                    value[i] = static_cast<float>(at(i, lowest));
                    open[i] = false;
                }
            }
        }
    }
    return value;
}

} // namespace

const PhaseReport& ImbalanceResult::report(Phase phase) const {
    switch (phase) {
    case Phase::L1:
        return L1;
    case Phase::L2:
        return L2;
    case Phase::L3:
    default:
        return L3;
    }
}

const char* to_string(Phase phase) {
    switch (phase) {
    case Phase::L1:
        return "L1";
    case Phase::L2:
        return "L2";
    case Phase::L3:
    default:
        return "L3";
    }
}

std::set<Phase> phases_drawn_on(const ObservedMeasurement& measurement, float nominal_ac_voltage) {
    const auto currents = per_phase_current(measurement, nominal_ac_voltage);
    std::set<Phase> phases;
    for (const auto phase : ALL_PHASES) {
        const auto value = phase_value(currents, phase);
        if (value.has_value() and value.value() > PHASE_NOISE_FLOOR_A) {
            phases.insert(phase);
        }
    }
    return phases;
}

float measured_current_on(const ObservedMeasurement& measurement, const std::set<Phase>& phases,
                          float nominal_ac_voltage) {
    const auto currents = per_phase_current(measurement, nominal_ac_voltage);
    float highest = 0.f;
    for (const auto phase : phases) {
        const auto value = phase_value(currents, phase);
        if (value.has_value()) {
            highest = std::max(highest, value.value());
        }
    }
    return highest;
}

ImbalanceResult correct_phase_imbalance(const PhaseCurrents& site_A, const std::vector<ImbalanceConnector>& connectors,
                                        float max_phase_imbalance_A, float unmeasured_A) {
    ImbalanceResult result;

    std::optional<float> reference_A;
    std::vector<Phase> known;
    for (const auto phase : ALL_PHASES) {
        const auto value = phase_value(site_A, phase);
        if (not value.has_value()) {
            continue;
        }
        known.push_back(phase);
        if (not reference_A.has_value() or value.value() < reference_A.value()) {
            reference_A = value;
            result.reference = phase;
        }
    }
    if (known.size() < 2) {
        result.reference.reset();
        return result;
    }

    // Connectors on all three phases change no difference and are left alone; the others
    // take part, those drawing nothing as possibly on any single phase.
    std::vector<const ImbalanceConnector*> participants;
    for (const auto& connector : connectors) {
        if (connector.draws_on.size() == ALL_PHASES.size()) {
            if (connector.cap_A.has_value()) {
                result.released.push_back(connector.uuid);
            }
            continue;
        }
        participants.push_back(&connector);
    }
    const auto n = participants.size();
    const auto on = [&](std::size_t i, Phase phase) {
        return participants[i]->draws_on.empty() or participants[i]->draws_on.count(phase) > 0;
    };

    std::map<Phase, float> uncontrolled_A;
    for (const auto phase : known) {
        float controlled = 0.f;
        for (const auto* connector : participants) {
            if (connector->draws_on.count(phase) > 0) {
                controlled += connector->measured_A;
            }
        }
        uncontrolled_A[phase] = phase_value(site_A, phase).value() - controlled;
    }

    // support[i]: what a connector on q alone holds q up by, which is what it draws now but
    // no more than the cap it ends up with.
    const auto make_budgets = [&](float limit_A, const std::vector<float>& support) {
        std::vector<Budget> budgets;
        for (const auto p : known) {
            for (const auto q : known) {
                if (p == q) {
                    continue;
                }
                // Connectors without a measurement may load p, on whichever phase they are.
                Budget budget{p, q, limit_A - (uncontrolled_A[p] - uncontrolled_A[q]) - unmeasured_A, {}};
                for (std::size_t i = 0; i < n; i++) {
                    const bool unknown = participants[i]->draws_on.empty();
                    if (unknown or (on(i, p) and not on(i, q))) {
                        budget.members.push_back(i);
                    } else if (on(i, q) and not on(i, p)) {
                        budget.capacity_A += support[i];
                    }
                }
                if (not budget.members.empty()) {
                    budgets.push_back(budget);
                }
            }
        }
        return budgets;
    };

    const auto decide = [&](const std::vector<float>& support) {
        const auto budgets = make_budgets(max_phase_imbalance_A - PHASE_IMBALANCE_HYSTERESIS_A, support);
        const auto hard_budgets = make_budgets(max_phase_imbalance_A, support);

        std::vector<float> demand(n);
        std::vector<bool> was_paused(n);
        for (std::size_t i = 0; i < n; i++) {
            const auto& connector = *participants[i];
            // Below its cap is only a choice of the EV once it had the hold to ramp up to it.
            const bool held = not connector.cap_A.has_value() or connector.draws_on.empty() or connector.settling or
                              connector.measured_A >= connector.cap_A.value() - PHASE_IMBALANCE_HYSTERESIS_A;
            demand[i] = held ? UNBOUNDED_A
                             : std::max(connector.min_A, connector.measured_A + 2.f * PHASE_IMBALANCE_HYSTERESIS_A);
            was_paused[i] = connector.cap_A.has_value() and connector.cap_A.value() <= 0.f;
        }

        // Equal shares, pausing the newest connector whose share falls below its minimum.
        std::vector<bool> active(n, true);
        const std::vector<float> zero(n, 0.f);
        std::vector<float> share;
        while (true) {
            share = fill(budgets, active, zero, demand, zero);
            std::optional<std::size_t> newest;
            for (std::size_t i = 0; i < n; i++) {
                const auto& connector = *participants[i];
                const float needed_A = connector.min_A - (was_paused[i] ? 0.f : PHASE_IMBALANCE_PAUSE_TOLERANCE_A);
                if (not active[i] or share[i] >= demand[i] or share[i] >= needed_A) {
                    continue;
                }
                if (not newest.has_value() or connector.arrived_at > participants[newest.value()]->arrived_at or
                    (connector.arrived_at == participants[newest.value()]->arrived_at and
                     connector.uuid > participants[newest.value()]->uuid)) {
                    newest = i;
                }
            }
            if (not newest.has_value()) {
                break;
            }
            active[newest.value()] = false;
        }
        for (std::size_t i = 0; i < n; i++) {
            if (not active[i]) {
                share[i] = 0.f;
            }
        }

        // What each connector holds now, and whether the caps as they stand break a budget.
        std::vector<float> holds(n);
        for (std::size_t i = 0; i < n; i++) {
            const auto& connector = *participants[i];
            holds[i] = connector.cap_A.value_or(connector.draws_on.empty() ? 0.f : connector.measured_A);
        }
        std::vector<bool> broken(n, false);
        for (const auto& budget : hard_budgets) {
            float total = 0.f;
            for (const auto i : budget.members) {
                total += holds[i];
            }
            if (total > budget.capacity_A + CAP_RESOLUTION_A) {
                for (const auto i : budget.members) {
                    broken[i] = true;
                }
            }
        }

        // Lowered and kept caps count at what the connector still draws above them; the rising
        // ones share what that leaves, starting from what they hold.
        std::vector<bool> rising(n, false);
        std::vector<float> load(n), lo(n), grant(n);
        for (std::size_t i = 0; i < n; i++) {
            const auto& connector = *participants[i];
            const bool capped = connector.cap_A.has_value();
            float decided = holds[i];
            if (not active[i]) {
                decided = 0.f;
            } else if (share[i] < holds[i]) {
                if (not capped or broken[i] or holds[i] - share[i] >= PHASE_IMBALANCE_DEADBAND_A) {
                    decided = share[i];
                }
            } else if (share[i] > holds[i]) {
                const bool worth_it = not capped or was_paused[i] or share[i] - holds[i] >= PHASE_IMBALANCE_DEADBAND_A;
                rising[i] = worth_it and not connector.settling;
            }
            grant[i] = decided;
            lo[i] = holds[i];
            load[i] = std::max(decided, connector.measured_A);
        }
        const auto risen = fill(hard_budgets, rising, lo, share, load);
        for (std::size_t i = 0; i < n; i++) {
            if (rising[i]) {
                grant[i] = risen[i];
                // A connector starting from nothing waits until it can have its minimum.
                if (holds[i] <= 0.f and grant[i] < participants[i]->min_A) {
                    grant[i] = 0.f;
                }
            }
            // Below its minimum a connector cannot charge: within the pause tolerance it keeps
            // its minimum, further below it pauses.
            const float min_A = participants[i]->min_A;
            if (grant[i] > 0.f and grant[i] < min_A) {
                grant[i] = grant[i] >= min_A - PHASE_IMBALANCE_PAUSE_TOLERANCE_A ? min_A : 0.f;
            }
        }
        return grant;
    };

    // A lower cap on q lowers the support q's budgets counted on; repeat until it holds.
    std::vector<float> support(n);
    for (std::size_t i = 0; i < n; i++) {
        support[i] = participants[i]->measured_A;
    }
    const auto lower_support = [&](const std::vector<float>& grant) {
        bool lowered = false;
        for (std::size_t i = 0; i < n; i++) {
            const float held_up = std::min(participants[i]->measured_A, grant[i]);
            if (held_up < support[i] - CAP_RESOLUTION_A) {
                support[i] = held_up;
                lowered = true;
            }
        }
        return lowered;
    };
    auto grant = decide(support);
    for (int round = 0; lower_support(grant); round++) {
        if (round == MAX_SUPPORT_ROUNDS) {
            grant = decide(std::vector<float>(n, 0.f));
            result.support_settled = false;
            break;
        }
        grant = decide(support);
    }

    for (std::size_t i = 0; i < n; i++) {
        const auto& connector = *participants[i];
        if (connector.cap_A.has_value() and std::fabs(connector.cap_A.value() - grant[i]) < CAP_RESOLUTION_A) {
            continue;
        }
        const float base_A = connector.cap_A.value_or(connector.measured_A);
        result.caps.push_back({connector.uuid, grant[i], base_A - grant[i]});
    }

    const float lowest_uncontrolled_A =
        std::min_element(uncontrolled_A.begin(), uncontrolled_A.end(), [](const auto& a, const auto& b) {
            return a.second < b.second;
        })->second;
    for (const auto phase : known) {
        auto& report = report_of(result, phase);
        report.imbalance_A = phase_value(site_A, phase).value() - reference_A.value();
        report.overshoot_A = std::max(0.f, report.imbalance_A - max_phase_imbalance_A);
        for (std::size_t i = 0; i < n; i++) {
            if (participants[i]->draws_on.count(phase) > 0) {
                report.corrected_A += std::max(0.f, participants[i]->measured_A - grant[i]);
            }
        }
        report.residual_A = std::max(0.f, uncontrolled_A[phase] - lowest_uncontrolled_A - max_phase_imbalance_A);
    }

    return result;
}

} // namespace module
