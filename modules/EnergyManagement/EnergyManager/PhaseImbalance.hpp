// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Broker.hpp"

namespace module {

// ---------------------------------------------------------------- phase imbalance limiting
//
// Keeps the difference between any two grid phases of the site within max_phase_imbalance_A
// as an invariant against rising load: every connector that can load some phases more than
// others is capped in advance, so the phases stay within the limit even when each of them
// draws its full cap. A connector that draws less or stops cannot be prevented from doing
// so; the next run shares the difference anew.
//
// Pure functions over one run. EnergyManagerImpl builds the inputs from the site aggregate
// and the brokers' observations, applies the caps as BrokerContext::phase_imbalance_cap_A
// (0 pauses the connector), and owns the hold timing.

enum class Phase {
    L1,
    L2,
    L3,
};

/// \brief One connector as the limiting sees it.
struct ImbalanceConnector {
    std::string uuid;
    /// Grid phases the connector draws on right now, from phases_drawn_on(). Empty while it
    /// draws nothing: it may then start on any single phase.
    std::set<Phase> draws_on;
    /// Highest current it draws on any of those phases [A].
    float measured_A{0.f};
    /// Below this the connector cannot charge: a share below it pauses the connector.
    float min_A{0.f};
    /// Cap a previous run left on it, if any; 0 while it is paused.
    std::optional<float> cap_A;
    /// Its cap changed within the hold, so its measurement may not reflect the cap yet: the
    /// cap may be lowered but not raised, and drawing below it does not count as the EV's
    /// own choice.
    bool settling{false};
    /// When the connector's session was first seen. The newest is paused first.
    date::utc_clock::time_point arrived_at{};
};

struct ImbalanceCap {
    std::string uuid;
    /// 0 pauses the connector.
    float new_cap_A;
    /// What this cap takes away from the previous cap, or from the measurement for a
    /// connector not capped before [A]. Negative when the cap hands current back.
    float cut_A;
};

/// \brief What happened on one phase this run.
struct PhaseReport {
    /// Current above the reference (least loaded) phase [A]; 0 on the reference itself.
    float imbalance_A{0.f};
    /// Part of the imbalance above max_phase_imbalance_A [A].
    float overshoot_A{0.f};
    /// Current the caps on this phase take away from what its connectors draw [A].
    float corrected_A{0.f};
    /// Imbalance above the limit that sits in load the manager does not control, and that
    /// capping connectors therefore cannot remove [A].
    float residual_A{0.f};
};

struct ImbalanceResult {
    /// Caps set or changed this run.
    std::vector<ImbalanceCap> caps;
    /// Connectors whose cap is dropped: they draw on all three phases, which cannot change
    /// the difference between any two.
    std::vector<std::string> released;
    PhaseReport L1;
    PhaseReport L2;
    PhaseReport L3;
    /// The least loaded phase, which the imbalance of the others is measured against.
    /// nullopt when fewer than two phases are known, in which case nothing else is set.
    std::optional<Phase> reference;

    const PhaseReport& report(Phase phase) const;
};

/// \brief Current above which a phase counts as drawn on. A meter's noise on an unused
/// phase must not make a single-phase EV look three-phase.
constexpr float PHASE_NOISE_FLOOR_A = 1.0f;

/// \brief Margin the caps keep under max_phase_imbalance_A, so measurement noise in the
/// uncontrolled load does not push the phases over the limit.
constexpr float PHASE_IMBALANCE_HYSTERESIS_A = 1.0f;

/// \brief Smallest change of an existing cap worth making while the caps keep the limit.
/// Keeps measurement noise from moving the caps every run.
constexpr float PHASE_IMBALANCE_DEADBAND_A = 0.5f;

/// \brief How far below its minimum a running connector's share may fall before it is
/// paused. A paused one resumes once its share reaches the minimum.
constexpr float PHASE_IMBALANCE_PAUSE_TOLERANCE_A = 0.2f;

/// \brief The grid phases a connector draws on, from its own per-phase measurement.
///
/// Per-phase current first, else per-phase power over \p nominal_ac_voltage. A reading that
/// carries only a total yields the empty set: a total says nothing about which phases carry
/// it, and spreading it evenly (as measured_phase_currents() does for the current cap)
/// would report a symmetric draw the limiting must never rely on.
std::set<Phase> phases_drawn_on(const ObservedMeasurement& measurement, float nominal_ac_voltage);

/// \brief Highest current the connector draws on any of \p phases, by the same precedence
/// as phases_drawn_on(). 0 when none of them is known.
float measured_current_on(const ObservedMeasurement& measurement, const std::set<Phase>& phases,
                          float nominal_ac_voltage);

/// \brief Computes the caps that keep every pair of phases within \p max_phase_imbalance_A.
///
/// The load the manager does not control on phase p, U_p, is what \p site_A reports on it
/// minus what the capped connectors draw there. For every ordered pair of known phases
/// (p, q), the connectors that can load p but not q share a budget of
/// max_phase_imbalance_A - PHASE_IMBALANCE_HYSTERESIS_A - (U_p - U_q), plus what the
/// connectors on q but not p draw now: a connector drawing on one or two phases counts where
/// it draws, one drawing nothing counts on every p, as it may start on any single phase. A
/// connector on all three phases loads every phase alike and is never capped.
///
/// The budgets are shared in equal shares. A connector held by its cap, not capped yet,
/// settling or drawing nothing counts as wanting more than any share; one drawing clearly
/// below its cap after the hold counts as wanting a little more than it draws, never less
/// than its minimum. A share below a connector's minimum
/// pauses the newest such connector (cap 0) and shares again.
///
/// Room is handed out only once it is free: a connector whose cap is lowered still counts
/// at what it draws until it has followed, so a connector whose cap rises, or a new one,
/// gets only what that leaves, and a new one waits at 0 until that reaches its minimum. A
/// cap is lowered at once when the caps as they stand break a budget with the margin used
/// up, and otherwise moves only by PHASE_IMBALANCE_DEADBAND_A or more; a settling cap is
/// not raised. A phase the aggregate does not report takes part in no budget, and with
/// fewer than two known phases nothing is decided.
ImbalanceResult correct_phase_imbalance(const PhaseCurrents& site_A, const std::vector<ImbalanceConnector>& connectors,
                                        float max_phase_imbalance_A);

const char* to_string(Phase phase);

} // namespace module
