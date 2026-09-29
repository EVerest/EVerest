// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <array>
#include <random>
#include <sstream>

#include <gtest/gtest.h>

#include "PhaseImbalance.hpp"

namespace module {

namespace {

constexpr float LIMIT_A = 20.0f;
constexpr float MIN_A = 6.0f;
constexpr float MAX_A = 32.0f;
constexpr int SCENARIOS = 20000;
// A connector kept running within the pause tolerance draws its minimum, slightly above its
// share; the caps also resolve to CAP_RESOLUTION_A.
constexpr float TOLERANCE_PER_CONNECTOR_A = PHASE_IMBALANCE_PAUSE_TOLERANCE_A + 0.05f;

constexpr std::array<Phase, 3> PHASES{Phase::L1, Phase::L2, Phase::L3};

struct Scenario {
    std::array<float, 3> uncontrolled_A{};
    float unmeasured_A{0.f};
    std::vector<ImbalanceConnector> connectors;
};

Scenario make_scenario(std::mt19937& rng) {
    std::uniform_real_distribution<float> uncontrolled(0.f, 5.f);
    std::uniform_real_distribution<float> current(0.f, MAX_A);
    std::uniform_int_distribution<int> count(1, 6);
    std::uniform_int_distribution<int> phase_mask(0, 7);
    std::bernoulli_distribution coin(0.5);
    std::bernoulli_distribution rare(0.15);

    Scenario s;
    for (auto& u : s.uncontrolled_A) {
        u = uncontrolled(rng);
    }
    s.unmeasured_A = rare(rng) ? MIN_A + 2.f : 0.f;
    const int n = count(rng);
    for (int i = 0; i < n; i++) {
        ImbalanceConnector c;
        c.uuid = "cp" + std::to_string(i);
        const int mask = phase_mask(rng);
        for (int p = 0; p < 3; p++) {
            if (mask & (1 << p)) {
                c.draws_on.insert(PHASES[p]);
            }
        }
        c.min_A = MIN_A;
        if (coin(rng)) {
            c.cap_A = rare(rng) ? 0.f : MIN_A + (MAX_A - MIN_A) * current(rng) / MAX_A;
        }
        const float ceiling = c.cap_A.value_or(MAX_A);
        c.measured_A = c.draws_on.empty() ? 0.f : std::min(ceiling, current(rng));
        if (c.measured_A <= PHASE_NOISE_FLOOR_A) {
            // What phases_drawn_on() reports for a connector drawing nothing.
            c.draws_on.clear();
            c.measured_A = 0.f;
        }
        c.settling = c.cap_A.has_value() and rare(rng);
        c.arrived_at = date::utc_clock::time_point{} + std::chrono::seconds(i);
        s.connectors.push_back(c);
    }
    return s;
}

PhaseCurrents site_of(const Scenario& s) {
    std::array<float, 3> site = s.uncontrolled_A;
    for (const auto& c : s.connectors) {
        for (const auto phase : c.draws_on) {
            site[static_cast<int>(phase)] += c.measured_A;
        }
    }
    return {site[0], site[1], site[2]};
}

std::optional<float> final_cap(const ImbalanceResult& result, const ImbalanceConnector& c) {
    for (const auto& cap : result.caps) {
        if (cap.uuid == c.uuid) {
            return cap.new_cap_A;
        }
    }
    for (const auto& uuid : result.released) {
        if (uuid == c.uuid) {
            return std::nullopt;
        }
    }
    return c.cap_A;
}

std::string describe(const Scenario& s, const ImbalanceResult& result) {
    std::ostringstream out;
    out << "uncontrolled " << s.uncontrolled_A[0] << "/" << s.uncontrolled_A[1] << "/" << s.uncontrolled_A[2]
        << " unmeasured " << s.unmeasured_A << "\n";
    for (const auto& c : s.connectors) {
        out << c.uuid << " on {";
        for (const auto phase : c.draws_on) {
            out << to_string(phase) << " ";
        }
        out << "} measured " << c.measured_A << " cap " << (c.cap_A.has_value() ? std::to_string(*c.cap_A) : "-")
            << (c.settling ? " settling" : "") << " -> "
            << (final_cap(result, c).has_value() ? std::to_string(*final_cap(result, c)) : "-") << "\n";
    }
    return out.str();
}

} // namespace

// Every connector drawing its full cap on the phases it may load keeps every pair of phases
// within the limit. A phase is held up only by what its connectors draw now, and no more
// than their caps.
TEST(PhaseImbalanceProperty, CapsKeepThePhasesWithinTheLimit) {
    std::mt19937 rng(20260929);
    for (int scenario = 0; scenario < SCENARIOS; scenario++) {
        const auto s = make_scenario(rng);
        const auto result = correct_phase_imbalance(site_of(s), s.connectors, LIMIT_A, s.unmeasured_A);

        for (const auto p : PHASES) {
            for (const auto q : PHASES) {
                if (p == q) {
                    continue;
                }
                float load_p = s.uncontrolled_A[static_cast<int>(p)] + s.unmeasured_A;
                float load_q = s.uncontrolled_A[static_cast<int>(q)];
                int on_p = 0;
                for (const auto& c : s.connectors) {
                    if (c.draws_on.size() == PHASES.size()) {
                        continue;
                    }
                    const auto cap = final_cap(result, c);
                    ASSERT_TRUE(cap.has_value()) << "uncapped participant\n" << describe(s, result);
                    const bool unknown = c.draws_on.empty();
                    const bool loads_p = unknown or c.draws_on.count(p) > 0;
                    const bool loads_q = not unknown and c.draws_on.count(q) > 0;
                    if (loads_p and not loads_q) {
                        load_p += std::min(cap.value(), MAX_A);
                        on_p++;
                    } else if (loads_q and not loads_p) {
                        load_q += std::min(c.measured_A, cap.value());
                    }
                }
                const float allowed = LIMIT_A + on_p * TOLERANCE_PER_CONNECTOR_A;
                ASSERT_LE(load_p - load_q, allowed)
                    << "scenario " << scenario << ", " << to_string(p) << " over " << to_string(q) << "\n"
                    << describe(s, result);
            }
        }
    }
}

TEST(PhaseImbalanceProperty, CapsAreZeroOrAtLeastTheMinimum) {
    std::mt19937 rng(4711);
    for (int scenario = 0; scenario < SCENARIOS; scenario++) {
        const auto s = make_scenario(rng);
        const auto result = correct_phase_imbalance(site_of(s), s.connectors, LIMIT_A, s.unmeasured_A);
        for (const auto& cap : result.caps) {
            ASSERT_TRUE(cap.new_cap_A <= 0.f or cap.new_cap_A >= MIN_A - 1e-3f)
                << "scenario " << scenario << ": " << cap.uuid << " capped at " << cap.new_cap_A << "\n"
                << describe(s, result);
        }
    }
}

} // namespace module
