// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "BrokerPowerRedistribution.hpp"
#include "PhaseImbalance.hpp"

#include <algorithm>

#include <everest/logging.hpp>

namespace module {

namespace {

// Outside Charging a connector measures zero for reasons unrelated to what the EV will draw.
bool consumption_is_demand(const types::energy::EnergyFlowRequest& node) {
    return not node.evse_state.has_value() or node.evse_state.value() == types::energy::EvseState::Charging;
}

PhaseCurrents uniform_phases(float value) {
    return {value, value, value};
}

std::optional<float> add_margin(const std::optional<float>& phase, float margin_A) {
    if (not phase.has_value()) {
        return std::nullopt;
    }
    return phase.value() + margin_A;
}

// The per-phase ampere a watt grant is worth, spread over the phases the connector uses.
// 0 for anything unusable.
float distributed_current_A(const std::optional<float>& distributed_W, float nominal_ac_voltage, int active_phases) {
    if (not distributed_W.has_value() or distributed_W.value() <= 0.f or nominal_ac_voltage <= 0.f) {
        return 0.f;
    }
    return distributed_W.value() / nominal_ac_voltage / static_cast<float>(std::max(active_phases, 1));
}

std::optional<float> clamp_phase(const std::optional<float>& phase, float max_A) {
    if (not phase.has_value()) {
        return std::nullopt;
    }
    return std::min(phase.value(), max_A);
}

// Phase count to assume when a limit declares none. One, not three: an undeclared phase
// count is missing information, and the safe reading of missing information about a limit
// is the smaller limit. EnergyNode always declares it (energyImpl.cpp), so this only
// covers a node that does not.
constexpr int ASSUMED_PHASE_COUNT = 1;

// An allocation this close to the static maximum counts as at the maximum. Trading happens
// in slices of slice_ampere (0.5 A x 230 V = 115 W by default), so 1 W only absorbs
// floating point noise of the two conversions.
constexpr float AT_MAXIMUM_TOLERANCE_W = 1.f;

// The limits in force at this node right now, from the offer the brokers traded against
// rather than from the raw request. Returns nullptr when the node has no schedule at all.
const types::energy::LimitsReq* active_limits(const Market& market) {
    const auto& offer = market.get_import_max_available();
    const auto slot = active_slot_index(offer);
    if (not slot.has_value()) {
        return nullptr;
    }
    return &offer[slot.value()].limits_to_root;
}

// A limit in watt: the lower of the watt value and ampere x phases x nominal voltage, since
// both apply. LimitsReq and LimitsRes name these fields identically. \p phases_drawn, when
// fewer than the limit declares, scales the result to the share of phases in use.
template <typename Limits>
std::optional<float> limits_to_W(const Limits& limits, float nominal_ac_voltage,
                                 std::optional<int> phases_drawn = std::nullopt) {
    const auto declared =
        limits.ac_max_phase_count.has_value() ? limits.ac_max_phase_count.value().value : ASSUMED_PHASE_COUNT;
    const float share = (phases_drawn.has_value() and declared > 0 and phases_drawn.value() < declared)
                            ? static_cast<float>(phases_drawn.value()) / static_cast<float>(declared)
                            : 1.f;
    std::optional<float> limit_W;
    if (limits.total_power_W.has_value()) {
        limit_W = limits.total_power_W.value().value * share;
    }
    if (limits.ac_max_current_A.has_value()) {
        const float current_W =
            limits.ac_max_current_A.value().value * static_cast<float>(declared) * nominal_ac_voltage * share;
        limit_W = limit_W.has_value() ? std::min(limit_W.value(), current_W) : current_W;
    }
    return limit_W;
}

} // namespace

std::optional<float> get_grid_limit_W(const Market& root, float nominal_ac_voltage) {
    const auto* limits = active_limits(root);
    if (limits == nullptr) {
        return std::nullopt;
    }
    const auto declared =
        limits->ac_max_phase_count.has_value() ? limits->ac_max_phase_count.value().value : ASSUMED_PHASE_COUNT;
    if (limits->total_power_W.has_value() or not limits->ac_max_current_per_phase_A.has_value() or declared < 3) {
        return limits_to_W(*limits, nominal_ac_voltage);
    }
    // A limit that differs per phase is worth what its phases add up to.
    float total_A = 0.f;
    for (const auto phase : ALL_GRID_PHASES) {
        const auto on_phase = phase_limit_A(*limits, phase);
        if (not on_phase.has_value()) {
            return limits_to_W(*limits, nominal_ac_voltage);
        }
        total_A += on_phase.value().value;
    }
    return total_A * nominal_ac_voltage;
}

std::optional<float> get_allocated_power_W(const types::energy::EnforcedLimits& limit, float nominal_ac_voltage,
                                           std::optional<int> phases_drawn) {
    return limits_to_W(limit.limits_root_side, nominal_ac_voltage, phases_drawn);
}

float get_margin_power_W(const types::energy::EnforcedLimits& limit, float margin_A, float nominal_ac_voltage,
                         std::optional<int> phases_drawn) {
    const auto& limits = limit.limits_root_side;
    if (not limits.ac_max_current_A.has_value() or margin_A <= 0.f) {
        return 0.f;
    }
    auto phases = limits.ac_max_phase_count.has_value() ? limits.ac_max_phase_count.value().value : ASSUMED_PHASE_COUNT;
    if (phases_drawn.has_value()) {
        phases = std::min(phases, phases_drawn.value());
    }
    return margin_A * static_cast<float>(phases) * nominal_ac_voltage;
}

StaticBoundsW get_static_bounds_W(const Market& connector, float nominal_ac_voltage, std::optional<int> phases_drawn) {
    StaticBoundsW bounds;
    const auto* limits = active_limits(connector);
    if (limits == nullptr) {
        return bounds;
    }

    bounds.max_W = limits_to_W(*limits, nominal_ac_voltage, phases_drawn);

    if (limits->ac_min_current_A.has_value()) {
        // The minimum purchase uses the smallest phase count the connector accepts; a
        // connector that can charge single phase only needs min current on one phase.
        const auto phases = limits->ac_min_phase_count.has_value()   ? limits->ac_min_phase_count.value().value
                            : limits->ac_max_phase_count.has_value() ? limits->ac_max_phase_count.value().value
                                                                     : ASSUMED_PHASE_COUNT;
        bounds.min_W = limits->ac_min_current_A.value().value * static_cast<float>(phases) * nominal_ac_voltage;
    }
    return bounds;
}

const char* to_string(ConnectorClass c) {
    switch (c) {
    case ConnectorClass::UnderConsuming:
        return "UnderConsuming";
    case ConnectorClass::Saturated:
        return "Saturated";
    case ConnectorClass::AtMaximum:
        return "AtMaximum";
    case ConnectorClass::Unknown:
    default:
        return "Unknown";
    }
}

ConnectorInference classify_connector(std::optional<float> allocated_W, std::optional<float> measured_W,
                                      const StaticBoundsW& bounds, float margin, float broker_margin_W) {
    ConnectorInference result;
    result.allocated_W = allocated_W;
    result.measured_W = measured_W;

    if (not allocated_W.has_value() or not measured_W.has_value() or allocated_W.value() <= 0.f) {
        return result;
    }

    // Negative is export (see types/units.yaml). This inference is about the import
    // schedule only, and a discharging connector has no import consumption to compare
    // against its import allocation. Clamping it to zero instead would read as "consuming
    // none of what it was allotted" and report the entire allocation as reducible.
    if (measured_W.value() < 0.f) {
        return result;
    }

    const float allocated = allocated_W.value();
    const float measured = measured_W.value();
    const float gap = allocated - measured;

    const float deadband = std::max(margin * allocated, broker_margin_W);

    if (gap > deadband) {
        result.connector_class = ConnectorClass::UnderConsuming;
        // Shrink to the measurement plus margin, but never below what the EV needs to keep
        // charging at all: reducing is meant to free unused power, not to starve a session.
        // Nor below what the cap hands out anyway - the measurement plus the broker's own
        // margin - which the next run would restore immediately.
        float target = std::max(measured * (1.f + margin), measured + broker_margin_W);
        if (bounds.min_W.has_value()) {
            target = std::max(target, bounds.min_W.value());
        }
        result.reducible_W = std::max(0.f, allocated - target);
        return result;
    }

    if (bounds.max_W.has_value() and allocated + AT_MAXIMUM_TOLERANCE_W >= bounds.max_W.value()) {
        result.connector_class = ConnectorClass::AtMaximum;
    } else {
        result.connector_class = ConnectorClass::Saturated;
    }
    return result;
}

std::optional<SaturatedConnector> to_saturated_connector(const std::string& uuid, const ConnectorInference& connector,
                                                         const StaticBoundsW& bounds) {
    if (not connector.allocated_W.has_value() or not bounds.max_W.has_value()) {
        return std::nullopt;
    }
    return SaturatedConnector{uuid, connector.allocated_W.value(), bounds.max_W.value()};
}

SiteInference infer_site(std::optional<float> grid_limit_W, const PowerMeterAggregator::AggregateResult& aggregate,
                         const std::vector<SaturatedConnector>& saturated, float margin, float gain) {
    SiteInference site;
    site.grid_limit_W = grid_limit_W;
    site.saturated_connectors = static_cast<int>(saturated.size());

    // Same trust rule as the aggregator's no-data contract: with any meter stale the sum
    // undercounts consumption and overstates headroom, so no claim is made at all.
    if (aggregate.power_W.has_value() and aggregate.fresh_meters > 0 and aggregate.stale_meters == 0) {
        site.measured_W = aggregate.power_W.value().total;
    }

    if (not grid_limit_W.has_value() or not site.measured_W.has_value()) {
        return site;
    }

    const float headroom = grid_limit_W.value() - site.measured_W.value();
    site.headroom_W = headroom;

    const float deadband = margin * grid_limit_W.value();
    if (headroom <= deadband or saturated.empty() or gain <= 0.f) {
        return site;
    }

    const float share = gain * (headroom - deadband) / static_cast<float>(saturated.size());
    for (const auto& connector : saturated) {
        const float room = std::max(0.f, connector.max_W - connector.allocated_W);
        const float granted = std::min(share, room);
        if (granted <= 0.f) {
            // A connector already at its maximum is left out rather than recorded as 0 W:
            // the map is what a broker acts on, and an entry there means "you may take
            // more". It still counted towards the split, which is what makes the site total
            // the sum of what was actually granted.
            continue;
        }
        site.increase_W += granted;
        site.increase_W_by_connector[connector.uuid] = granted;
    }
    return site;
}

ObservedMeasurement read_measurement(const types::energy::EnergyFlowRequest& node) {
    const auto* reading = select_reading(node);
    if (reading == nullptr) {
        return {};
    }

    ObservedMeasurement measurement;
    measurement.power_W = reading->power_W;
    measurement.current_A = reading->current_A.value_or(types::units::Current{});
    measurement.measured_at = parse_meter_timestamp(reading->timestamp);
    return measurement;
}

PhaseCurrents measured_phase_currents(const ObservedMeasurement& measurement, float nominal_ac_voltage,
                                      int active_phases) {
    const auto& current = measurement.current_A;
    if (current.L1.has_value() or current.L2.has_value() or current.L3.has_value()) {
        return {current.L1, current.L2, current.L3};
    }

    if (not measurement.power_W.has_value() or nominal_ac_voltage <= 0.0f) {
        return {};
    }

    const auto& power = measurement.power_W.value();
    const auto phase_power_to_current = [nominal_ac_voltage](const std::optional<float>& watt) -> std::optional<float> {
        if (not watt.has_value()) {
            return std::nullopt;
        }
        return watt.value() / nominal_ac_voltage;
    };
    if (power.L1.has_value() or power.L2.has_value() or power.L3.has_value()) {
        return {phase_power_to_current(power.L1), phase_power_to_current(power.L2), phase_power_to_current(power.L3)};
    }

    // Which phases carry a total is unknown; only the magnitude matters for a scalar cap.
    const int phases = std::max(active_phases, 1);
    const float per_phase = power.total / nominal_ac_voltage / static_cast<float>(phases);
    PhaseCurrents currents;
    currents.L1 = per_phase;
    if (phases >= 2) {
        currents.L2 = per_phase;
    }
    if (phases >= 3) {
        currents.L3 = per_phase;
    }
    return currents;
}

bool measurement_can_limit(const ObservedMeasurement& measurement, date::utc_clock::time_point now,
                           std::chrono::seconds max_age) {
    const bool has_value = measurement.power_W.has_value() or measurement.current_A.L1.has_value() or
                           measurement.current_A.L2.has_value() or measurement.current_A.L3.has_value();
    if (not has_value) {
        return false;
    }
    return is_fresh(measurement.measured_at, now, max_age);
}

std::optional<float> to_scalar_cap(const PhaseCurrents& cap) {
    std::optional<float> scalar;
    for (const auto& phase : {cap.L1, cap.L2, cap.L3}) {
        if (phase.has_value() and (not scalar.has_value() or phase.value() > scalar.value())) {
            scalar = phase;
        }
    }
    return scalar;
}

BrokerPowerRedistribution::BrokerPowerRedistribution(Market& market, BrokerContext& context,
                                                     EnergyManagerConfig config) :
    BrokerFastCharging(market, context, config) {
}

void BrokerPowerRedistribution::observe() {
    const auto& request = local_market.energy_flow_request;

    if (not in_session(request)) {
        return;
    }

    context.last_observed_measurement = read_measurement(request);

    const auto& measurement = context.last_observed_measurement;
    const bool has_reading = measurement.power_W.has_value() or measurement.current_A.L1.has_value() or
                             measurement.current_A.L2.has_value() or measurement.current_A.L3.has_value();
    if (measurement.power_W.has_value()) {
        EVLOG_debug << request.uuid << ": measured power " << measurement.power_W.value().total << " W";
    } else if (not has_reading and not context.redistribution_warned_no_measurement) {
        context.redistribution_warned_no_measurement = true;
        EVLOG_warning << request.uuid << ": power redistribution has no measurement, capping at minimum current";
    }

    track_phases_in_use();
    decide_cap(request);
}

void BrokerPowerRedistribution::track_phases_in_use() {
    const auto now = globals.start_time;
    const auto& measurement = context.last_observed_measurement;
    PhaseSet drawn;
    if (measurement_can_limit(measurement, now, config.redistribution.measurement_max_age)) {
        drawn = phases_drawn_on(measurement, local_market.nominal_ac_voltage());
    }
    if (drawn.empty()) {
        drawn = ALL_GRID_PHASES;
    }

    auto& in_use = context.phases_in_use;
    const bool wider = std::any_of(drawn.begin(), drawn.end(), [&in_use](Phase p) { return in_use.count(p) == 0; });
    if (wider) {
        in_use.insert(drawn.begin(), drawn.end());
        context.phases_narrower_since.reset();
        return;
    }
    if (drawn == in_use) {
        context.phases_narrower_since.reset();
        return;
    }
    if (not context.phases_narrower_since.has_value()) {
        context.phases_narrower_since = now;
    }
    if (now - context.phases_narrower_since.value() >= config.redistribution.reduction_hold) {
        in_use = drawn;
        context.phases_narrower_since.reset();
    }
}

void BrokerPowerRedistribution::decide_cap(const types::energy::EnergyFlowRequest& request) {
    if (not consumption_is_demand(request)) {
        // A resume starts over like a new session, start value included.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    // observe() runs before any trading round, so the available energy is the full offer.
    const auto available = local_market.get_available_energy_import();
    const auto& limits = available[globals.active_slot].limits_to_root;

    if (not limits.ac_max_current_A.has_value()) {
        // Watt-only node (DC): traded like FastCharging.
        context.redistribution_cap_A = std::nullopt;
        context.redistribution_reduction_pending_since = std::nullopt;
        return;
    }

    const auto& redistribution = config.redistribution;
    const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
    const float lower_limit_A = min_current_A + redistribution.margin_A;

    if (not context.redistribution_cap_A.has_value()) {
        const float start_A =
            redistribution.start_with_lower_limit ? lower_limit_A : limits.ac_max_current_A.value().value;
        context.redistribution_cap_A = uniform_phases(start_A);
        m_run_cap_source = "BrokerPowerRedistribution_SessionStart";
    }

    // A watt grant turns into current on the phases the EV draws on, not the ones offered.
    const int active_phases = std::min(limits.ac_number_of_active_phases.value_or(ASSUMED_PHASE_COUNT),
                                       static_cast<int>(context.phases_in_use.size()));

    PhaseCurrents candidate;
    std::string source;
    if (measurement_can_limit(context.last_observed_measurement, globals.start_time,
                              redistribution.measurement_max_age)) {
        const auto measured = measured_phase_currents(context.last_observed_measurement,
                                                      local_market.nominal_ac_voltage(), active_phases);
        // The site's grant rides on top of the margin. Without a fresh measurement there is
        // no grant either, since it is an allowance above what was measured.
        const float distributed_A =
            distributed_current_A(context.distributed_power_W, local_market.nominal_ac_voltage(), active_phases);
        const float step_A = redistribution.margin_A + distributed_A;
        candidate = {add_margin(measured.L1, step_A), add_margin(measured.L2, step_A), add_margin(measured.L3, step_A)};

        // Never above what this connector may draw anyway. The market would clamp it too,
        // but an unclamped cap would be carried in the context as if the connector had been
        // allowed that much, which is not what the next run should depart from.
        const float max_A = limits.ac_max_current_A.value().value;
        candidate = {clamp_phase(candidate.L1, max_A), clamp_phase(candidate.L2, max_A),
                     clamp_phase(candidate.L3, max_A)};

        source = distributed_A > 0.f ? "BrokerPowerRedistribution_MeasuredPlusDistributed"
                                     : "BrokerPowerRedistribution_MeasuredPlusMargin";
    }
    if (not to_scalar_cap(candidate).has_value()) {
        // A dead meter must not hold an allocation open.
        candidate = uniform_phases(lower_limit_A);
        source = "BrokerPowerRedistribution_NoMeasurement";
    }

    const auto applied_scalar = to_scalar_cap(context.redistribution_cap_A.value());
    const auto candidate_scalar = to_scalar_cap(candidate);
    if (applied_scalar.has_value() and candidate_scalar.value() < applied_scalar.value()) {
        if (not context.redistribution_reduction_pending_since.has_value()) {
            context.redistribution_reduction_pending_since = globals.start_time;
        }
        if (globals.start_time - context.redistribution_reduction_pending_since.value() <
            redistribution.reduction_hold) {
            m_run_cap_A = context.redistribution_cap_A;
            if (m_run_cap_source.empty()) {
                m_run_cap_source = "BrokerPowerRedistribution_ReductionHold";
            }
            return;
        }
    }

    context.redistribution_reduction_pending_since = std::nullopt;
    context.redistribution_cap_A = candidate;
    m_run_cap_A = candidate;
    m_run_cap_source = source;
}

void BrokerPowerRedistribution::tradeImpl() {
    // Paused by the phase imbalance limiting: nothing is bought, so the zero trade stands.
    // Capping the offer cannot express this, it never goes below the connector's minimum.
    const auto& imbalance_cap = context.phase_imbalance_cap_A;
    if (imbalance_cap.has_value() and imbalance_cap.value() <= 0.f) {
        return;
    }
    limit_offer_to_cap();
    BrokerFastCharging::tradeImpl();
}

void BrokerPowerRedistribution::limit_offer_to_cap() {
    std::optional<float> cap;
    std::string cap_source = m_run_cap_source;
    if (m_run_cap_A.has_value()) {
        cap = to_scalar_cap(m_run_cap_A.value());
    }
    // The phase imbalance correction's cap is one more upper bound, whichever is lower.
    const auto& imbalance_cap = context.phase_imbalance_cap_A;
    if (imbalance_cap.has_value() and (not cap.has_value() or imbalance_cap.value() < cap.value())) {
        cap = imbalance_cap;
        cap_source = "PhaseImbalance";
    }
    if (not cap.has_value()) {
        return;
    }

    const int slot = globals.active_slot;
    auto& limits = offer->import_offer[slot].limits_to_root;
    if (not limits.ac_max_current_A.has_value()) {
        return;
    }

    // Never below what the all-or-nothing first trade must buy, and never 0: FastCharging
    // reads a max current of 0 as "cannot import" and flips the slot to export.
    const float min_current_A = limits.ac_min_current_A.has_value() ? limits.ac_min_current_A.value().value : 0.0f;
    const float floor_A = std::max(min_current_A, globals.slice_ampere);

    // The offer is what is left after earlier trading rounds, so the cap is too.
    const float allowance_A = std::max(cap.value(), floor_A) - sold_current_A(slot);

    apply_limit_if_smaller(limits.ac_max_current_A, std::max(0.0f, allowance_A), cap_source);
}

float BrokerPowerRedistribution::sold_current_A(int slot) const {
    const auto& sold = local_market.get_sold_energy();
    if (slot >= static_cast<int>(sold.size()) or not sold[slot].limits_to_root.ac_max_current_A.has_value()) {
        return 0.0f;
    }
    return std::max(0.0f, sold[slot].limits_to_root.ac_max_current_A.value().value);
}

} // namespace module
