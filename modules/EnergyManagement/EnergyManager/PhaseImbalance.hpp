// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Broker.hpp"

namespace module {

// ---------------------------------------------------------------- phase imbalance correction
//
// Keeps the difference between the most and the least loaded grid phase of the site under
// max_phase_imbalance_A by cutting the connectors that cause it. Measurement driven, in
// three steps per optimizer run: the site aggregate says how much each phase carries, the
// overshoot above the limit says how much has to go, and the connectors drawing on the
// overloaded phase share that cut equally.
//
// Pure functions over one run. EnergyManagerImpl builds the inputs from the site aggregate
// and the brokers' observations, applies the cuts as BrokerContext::phase_imbalance_cap_A,
// and owns the hold and release timing.

enum class Phase {
    L1,
    L2,
    L3,
};

/// \brief One connector as the correction sees it.
struct ImbalanceConnector {
    std::string uuid;
    /// Grid phases the connector draws on right now, from phases_drawn_on().
    std::set<Phase> draws_on;
    /// Highest current it draws on any of those phases [A]. What a cut is subtracted from.
    float measured_A{0.f};
    /// Below this the connector is not cut any further: a plugged-in EV keeps its minimum.
    float min_A{0.f};
    /// Cap a previous run's cut left on it, if any. A cut never raises it.
    std::optional<float> cap_A;
};

struct ImbalanceCut {
    std::string uuid;
    float new_cap_A;
    /// What this cut takes away [A]: the difference between what the connector drew (or
    /// was already capped at) and new_cap_A.
    float cut_A;
};

/// \brief What happened on one phase this run.
struct PhaseReport {
    /// Current above the reference (least loaded) phase [A]; 0 on the reference itself.
    float imbalance_A{0.f};
    /// Part of the imbalance above max_phase_imbalance_A [A].
    float overshoot_A{0.f};
    /// Overshoot the cuts on this phase take away [A].
    float corrected_A{0.f};
    /// Overshoot nothing can take away [A]: no connector involved, all at their minimum, or
    /// the skew sits in a load the manager does not control.
    float residual_A{0.f};
};

struct ImbalanceResult {
    std::vector<ImbalanceCut> cuts;
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

/// \brief Room a phase must have under the limit, beyond what a released cut would hand
/// back, before the cut is released. Keeps measurement noise from re-triggering a cut
/// right after its release.
constexpr float PHASE_IMBALANCE_RELEASE_HYSTERESIS_A = 1.0f;

/// \brief The grid phases a connector draws on, from its own per-phase measurement.
///
/// Per-phase current first, else per-phase power over \p nominal_ac_voltage. A reading that
/// carries only a total yields the empty set: a total says nothing about which phases carry
/// it, and spreading it evenly (as measured_phase_currents() does for the current cap)
/// would report a symmetric draw the correction must never act on.
std::set<Phase> phases_drawn_on(const ObservedMeasurement& measurement, float nominal_ac_voltage);

/// \brief Highest current the connector draws on any of \p phases, by the same precedence
/// as phases_drawn_on(). 0 when none of them is known.
float measured_current_on(const ObservedMeasurement& measurement, const std::set<Phase>& phases,
                          float nominal_ac_voltage);

/// \brief Computes the cuts that bring every phase's imbalance back to the limit.
///
/// The reference is the least loaded of the phases \p site_A reports; a phase's imbalance
/// is what it carries above that. Each phase whose imbalance exceeds \p max_phase_imbalance_A
/// is handled on its own: the overshoot is split equally over the connectors involved on it.
///
/// A connector is involved on a phase when it draws on that phase and not on the
/// reference. Cutting a connector lowers every phase it draws on by the same amount, so
/// one that also draws on the reference cannot change the difference - this is what keeps
/// three-phase loads out, and admits a two-phase load only when its other phase is not
/// the reference. A connector involved on two phases is cut by the larger of its two
/// shares rather than their sum, for the same reason.
///
/// The cut comes off the lower of the connector's previous cap and its measured current,
/// and never takes it below its minimum. What a connector cannot deliver because of that
/// floor is reported as residual, not spread further: the next run, after the hold, sees
/// the remaining overshoot in fresh measurements and cuts again. A phase the aggregate does
/// not report is neither reference nor candidate, and with fewer than two known phases
/// there is no imbalance to speak of.
ImbalanceResult correct_phase_imbalance(const PhaseCurrents& site_A, const std::vector<ImbalanceConnector>& connectors,
                                        float max_phase_imbalance_A);

const char* to_string(Phase phase);

} // namespace module
