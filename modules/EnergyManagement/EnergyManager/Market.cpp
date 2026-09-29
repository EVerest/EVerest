// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "Market.hpp"
#include <algorithm>
#include <everest/logging.hpp>
#include <fmt/core.h>
#include <limits>

namespace module {

globals_t globals;

void globals_t::init(date::utc_clock::time_point _start_time, int _interval_duration, int _schedule_duration,
                     float _slice_ampere, float _slice_watt, bool _debug,
                     const types::energy::EnergyFlowRequest& energy_flow_request) {
    start_time = _start_time;
    interval_duration = std::chrono::minutes(_interval_duration);
    schedule_length = std::chrono::hours(_schedule_duration) / interval_duration;
    slice_ampere = _slice_ampere;
    slice_watt = _slice_watt;
    debug = _debug;

    create_timestamps(energy_flow_request);

    create_empty_schedule(zero_schedule_req);

    for (auto& a : zero_schedule_req) {
        a.limits_to_root.ac_max_current_A = {0.};
        a.limits_to_root.total_power_W = {0.};
    }

    create_empty_schedule(empty_schedule_req);
    active_slot = static_cast<int>(active_slot_index(empty_schedule_req).value_or(0));

    create_empty_schedule(zero_schedule_res);

    for (auto& a : zero_schedule_res) {
        a.limits_to_root.ac_max_current_A = {0.};
        a.limits_to_root.total_power_W = {0.};
    }

    create_empty_schedule(empty_schedule_res);

    create_empty_schedule(empty_schedule_setpoints);
}

void globals_t::create_timestamps(const types::energy::EnergyFlowRequest& energy_flow_request) {

    timestamps.clear();
    timestamps.reserve(schedule_length);

    auto minutes_overflow = start_time.time_since_epoch() % interval_duration;
    auto start = start_time - minutes_overflow;

    // Add leap seconds
    date::get_leap_second_info(start_time);
    auto timepoint = start + date::get_leap_second_info(start_time).elapsed;

    // Insert all our pre defined time slots
    for (int i = 0; i < schedule_length; i++) {
        timestamps.push_back(timepoint);
        timepoint += interval_duration;
    }

    // Insert timestamps of all requests
    add_timestamps(energy_flow_request);

    // sort
    std::sort(timestamps.begin(), timestamps.end());

    // remove duplicates
    timestamps.erase(unique(timestamps.begin(), timestamps.end()), timestamps.end());

    schedule_length = timestamps.size();
}

void globals_t::add_timestamps(const types::energy::EnergyFlowRequest& energy_flow_request) {
    // add local timestamps
    for (auto t : energy_flow_request.schedule_import) {
        // insert current timestamp
        timestamps.push_back(Everest::Date::from_rfc3339(t.timestamp));
    }

    for (auto t : energy_flow_request.schedule_export) {
        // insert current timestamp
        timestamps.push_back(Everest::Date::from_rfc3339(t.timestamp));
    }

    for (auto t : energy_flow_request.schedule_setpoints) {
        // insert current timestamp
        timestamps.push_back(Everest::Date::from_rfc3339(t.timestamp));
    }

    // recurse to all children
    for (auto& c : energy_flow_request.children)
        add_timestamps(c);
}

template <typename T> void globals_t::create_empty_schedule(T& s) {
    // initialize schedule with correct size
    typename T::value_type e;
    s = T(schedule_length, e);

    for (int i = 0; i < schedule_length; i++) {
        s[i].timestamp = Everest::Date::to_rfc3339(timestamps[i]);
    }
}

int time_probe::stop() {
    if (running) {
        pause();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(total_duration).count();
}

void time_probe::start() {
    timepoint_start = std::chrono::high_resolution_clock::now();
    running = true;
}

void time_probe::pause() {
    if (running) {
        total_duration += std::chrono::high_resolution_clock::now() - timepoint_start;
        running = false;
    }
}

// returns the smaller of two optionals. Note that comparison operators on optionals are a little weird if not both
// sides have a value, we explicitly want:
// - If both are not set, it should return an empty optional
// - If either a or b is set but not both, return the one set.
// - If both have a value, return the smaller one.
template <typename T> std::optional<T> min_optional(std::optional<T> a, std::optional<T> b) {

    if (a.has_value() and b.has_value()) {
        if (a.value().value < b.value().value) {
            return a;
        } else {
            return b;
        }
    }

    if (a.has_value()) {
        return a;
    }

    return b;
}

static std::optional<float> min_of(const std::optional<float>& a, const std::optional<float>& b) {
    if (a.has_value() and b.has_value()) {
        return std::min(a.value(), b.value());
    }
    return a.has_value() ? a : b;
}

static std::optional<types::energy::PhaseCurrentsWithSource>
min_per_phase(const std::optional<types::energy::PhaseCurrentsWithSource>& a,
              const std::optional<types::energy::PhaseCurrentsWithSource>& b) {
    if (not a.has_value() or not b.has_value()) {
        return a.has_value() ? a : b;
    }
    types::energy::PhaseCurrentsWithSource merged;
    merged.L1 = min_of(a.value().L1, b.value().L1);
    merged.L2 = min_of(a.value().L2, b.value().L2);
    merged.L3 = min_of(a.value().L3, b.value().L3);
    merged.source = a.value().source == b.value().source ? a.value().source : a.value().source + "," + b.value().source;
    return merged;
}

std::optional<types::energy::NumberWithSource> phase_limit_A(const types::energy::LimitsReq& limits, Phase phase) {
    std::optional<types::energy::NumberWithSource> on_phase;
    if (limits.ac_max_current_per_phase_A.has_value()) {
        const auto& per_phase = limits.ac_max_current_per_phase_A.value();
        const auto& value = phase == Phase::L1 ? per_phase.L1 : phase == Phase::L2 ? per_phase.L2 : per_phase.L3;
        if (value.has_value()) {
            on_phase = types::energy::NumberWithSource{value.value(), per_phase.source};
        }
    }
    return min_optional(limits.ac_max_current_A, on_phase);
}

template <typename T> std::optional<T> max_optional(std::optional<T> a, std::optional<T> b) {

    if (a.has_value() and b.has_value()) {
        if (a.value().value > b.value().value) {
            return a;
        } else {
            return b;
        }
    }

    if (a.has_value()) {
        return a;
    }

    return b;
}

ScheduleSetpoints Market::resample(const ScheduleSetpoints& request) {

    ScheduleSetpoints sp = globals.empty_schedule_setpoints;

    // First resample request to the timestamps in available and merge all limits on root sides
    for (auto& s : sp) {

        // find corresponding entry in request
        auto r = request.begin();
        auto tp_a = Everest::Date::from_rfc3339(s.timestamp);
        for (auto ir = request.begin(); ir != request.end(); ir++) {
            auto tp_r_1 = Everest::Date::from_rfc3339((*ir).timestamp);
            if ((ir + 1 == request.end())) {
                r = ir;
                break;
            }
            auto tp_r_2 = Everest::Date::from_rfc3339((*(ir + 1)).timestamp);
            if ((tp_a >= tp_r_1 && tp_a < tp_r_2) || (ir == request.begin() && tp_a < tp_r_1)) {
                r = ir;
                break;
            }
        }

        if (r != request.end()) {
            // copy setpoint if any
            s.setpoint = (*r).setpoint;
        }
    }

    return sp;
}

std::optional<ScheduleReq::size_type> active_slot_index(const ScheduleReq& schedule) {
    if (schedule.empty()) {
        return std::nullopt;
    }

    ScheduleReq::size_type active = 0;
    for (ScheduleReq::size_type n = 0; n < schedule.size(); n++) {
        if (Everest::Date::from_rfc3339(schedule[n].timestamp) > globals.start_time) {
            break;
        }
        active = n;
    }
    return active;
}

ScheduleReq Market::get_max_available_energy(const ScheduleReq& request) {

    ScheduleReq available = globals.empty_schedule_req;

    // First resample request to the timestamps in available and merge all limits on root sides
    for (auto& a : available) {

        // find corresponding entry in request
        auto r = request.begin();
        auto tp_a = Everest::Date::from_rfc3339(a.timestamp);
        for (auto ir = request.begin(); ir != request.end(); ir++) {
            auto tp_r_1 = Everest::Date::from_rfc3339((*ir).timestamp);
            if ((ir + 1 == request.end())) {
                r = ir;
                break;
            }
            auto tp_r_2 = Everest::Date::from_rfc3339((*(ir + 1)).timestamp);
            if ((tp_a >= tp_r_1 && tp_a < tp_r_2) || (ir == request.begin() && tp_a < tp_r_1)) {
                r = ir;
                break;
            }
        }

        if (r != request.end()) {

            {
                auto leaves_power_W = (*r).limits_to_leaves.total_power_W;
                if (leaves_power_W.has_value()) {
                    leaves_power_W.value().value =
                        leaves_power_W.value().value / (*r).conversion_efficiency.value_or(1.);
                }

                a.limits_to_root.total_power_W = min_optional(leaves_power_W, (*r).limits_to_root.total_power_W);
            }

            a.limits_to_root.ac_max_current_A =
                min_optional((*r).limits_to_leaves.ac_max_current_A, (*r).limits_to_root.ac_max_current_A);

            a.limits_to_root.ac_max_current_per_phase_A = min_per_phase(
                (*r).limits_to_leaves.ac_max_current_per_phase_A, (*r).limits_to_root.ac_max_current_per_phase_A);

            a.limits_to_root.ac_min_phase_count =
                max_optional((*r).limits_to_root.ac_min_phase_count, (*r).limits_to_leaves.ac_min_phase_count);

            a.limits_to_root.ac_max_phase_count =
                min_optional((*r).limits_to_root.ac_max_phase_count, (*r).limits_to_leaves.ac_max_phase_count);

            a.limits_to_root.ac_min_current_A =
                max_optional((*r).limits_to_root.ac_min_current_A, (*r).limits_to_leaves.ac_min_current_A);

            // all request limits have been merged on root side in available.
            // copy other information if any
            a.price_per_kwh = (*r).price_per_kwh;
            a.limits_to_root.ac_number_of_active_phases = (*r).limits_to_root.ac_number_of_active_phases;
        }
    }

    return available;
}

namespace {
// A connector whose phases are unknown may draw on any of them.
const PhaseSet& or_all_phases(const PhaseSet& phases) {
    return phases.empty() ? ALL_GRID_PHASES : phases;
}
} // namespace

ScheduleReq Market::get_available_energy(const ScheduleReq& max_available, bool add_sold,
                                         const PhaseSet& requested_phases) {
    const auto& phases = or_all_phases(requested_phases);
    ScheduleReq available = max_available;
    for (ScheduleReq::size_type i = 0; i < available.size(); i++) {
        // FIXME: sold_root is the sum of all energy sold, but we need to limit indivdual paths as well
        // add config option for pure star type of cabling here as well.
        auto& limits = available[i].limits_to_root;

        if (not phases.empty()) {
            // What is left on the tightest of the connector's phases, each phase against its
            // own limit and what is sold on it in the direction traded.
            std::optional<types::energy::NumberWithSource> tightest;
            for (const auto phase : phases) {
                auto left = phase_limit_A(limits, phase);
                if (not left.has_value()) {
                    continue;
                }
                const float sold = (add_sold ? 1 : -1) * sold_phase_A[i][static_cast<int>(phase)];
                left.value().value += std::min(sold, 0.f);
                if (not tightest.has_value() or left.value().value < tightest.value().value) {
                    tightest = left;
                }
            }
            limits.ac_max_current_A = tightest;
        }
        limits.ac_max_current_per_phase_A.reset();

        float sold_watt = (add_sold ? 1 : -1) * m_sold_drawn_W[i];
        if (sold_watt > 0)
            sold_watt = 0;

        if (limits.total_power_W.has_value())
            limits.total_power_W.value().value += sold_watt;
    }
    return available;
}

ScheduleReq Market::get_available_energy_import(const PhaseSet& phases) {
    return get_available_energy(import_max_available, false, phases);
}

ScheduleReq Market::get_available_energy_export(const PhaseSet& phases) {
    return get_available_energy(export_max_available, true, phases);
}

float get_watt_from_freq_table(const std::vector<types::energy::FrequencyWattPoint>& table, float freq) {
    // the table has to be sorted by freqency

    if (table.size() == 0) {
        return 0.;
    }

    if (table.size() == 1) {
        return table[0].total_power_W;
    }

    float watt1 = table[0].total_power_W;
    float watt2 = 0.;
    float freq1 = 0.;

    for (const auto e : table) {
        watt2 = e.total_power_W;
        if (e.frequency_Hz > freq) {
            break;
        }
        watt1 = e.total_power_W;
        freq1 = e.frequency_Hz;
    }
    return watt1 + (freq - freq1) * (watt2 - watt1);
}

void apply_limit_if_smaller(std::optional<types::energy::NumberWithSource>& base, float limit,
                            const std::string& source) {
    if (not base.has_value() or (base.has_value() and base.value().value > limit)) {
        base = {limit, source};
    }
}

void apply_setpoints(ScheduleReq& imp, ScheduleReq& exp, const ScheduleSetpoints& setpoints,
                     std::optional<float> freq) {
    if (setpoints.size() != imp.size()) {
        EVLOG_error << fmt::format("apply_setpoints: setpoints({}) and import({}) do not have the same size.",
                                   setpoints.size(), imp.size());
        return;
    }
    if (setpoints.size() != exp.size()) {
        EVLOG_error << fmt::format("apply_setpoints: setpoints({}) and export({}) do not have the same size.",
                                   setpoints.size(), exp.size());
        return;
    }

    for (ScheduleReq::size_type i = 0; i < setpoints.size(); i++) {
        // apply setpoints as limits
        if (setpoints[i].setpoint.has_value()) {
            const auto& sp = setpoints[i].setpoint.value();
            auto& imp_limits = imp[i].limits_to_root;
            auto& exp_limits = exp[i].limits_to_root;

            // Allow only one actual setpoint value to be set, in this priority order
            if (sp.ac_current_A.has_value()) {
                if (sp.ac_current_A.value() >= 0.) {
                    // Charging setpoint
                    apply_limit_if_smaller(imp_limits.ac_max_current_A, sp.ac_current_A.value(), sp.source);
                    exp_limits.ac_max_current_A = {0., sp.source};
                } else {
                    // Discharging setpoint
                    apply_limit_if_smaller(exp_limits.ac_max_current_A, -sp.ac_current_A.value(), sp.source);
                    imp_limits.ac_max_current_A = {0., sp.source};
                }
            } else if (sp.total_power_W.has_value()) {
                if (sp.total_power_W.value() >= 0.) {
                    // Charging setpoint
                    apply_limit_if_smaller(imp_limits.total_power_W, sp.total_power_W.value(), sp.source);
                    exp_limits.total_power_W = {0., sp.source};
                } else {
                    // Discharging setpoint
                    apply_limit_if_smaller(exp_limits.total_power_W, -sp.total_power_W.value(), sp.source);
                    imp_limits.total_power_W = {0., sp.source};
                }

            } else if (sp.frequency_table.has_value() and freq.has_value()) {
                // get actual watt limit from table and current frequency from meter
                float watt_limit = get_watt_from_freq_table(sp.frequency_table.value(), freq.value());
                if (watt_limit >= 0.) {
                    // Charging setpoint
                    apply_limit_if_smaller(imp_limits.total_power_W, watt_limit, sp.source);
                    exp_limits.total_power_W = {0., sp.source};
                } else {
                    // Discharging setpoint
                    apply_limit_if_smaller(exp_limits.total_power_W, -watt_limit, sp.source);
                    imp_limits.total_power_W = {0., sp.source};
                }
            }
        }
    }
}

Market::Market(const types::energy::EnergyFlowRequest& _energy_flow_request, const float __nominal_ac_voltage,
               Market* __parent) :
    energy_flow_request(_energy_flow_request), _parent(__parent), _nominal_ac_voltage(__nominal_ac_voltage) {

    // EVLOG_info << "Create market for " << _energy_flow_request.uuid;

    sold_root = globals.empty_schedule_res;
    sold_phase_A.assign(sold_root.size(), {0.f, 0.f, 0.f});
    m_sold_drawn_W.assign(sold_root.size(), 0.f);

    if (not energy_flow_request.schedule_import.empty()) {
        import_max_available = get_max_available_energy(energy_flow_request.schedule_import);
    } else {
        // nothing is available as nothing was requested
        import_max_available = globals.zero_schedule_req;
    }

    if (not energy_flow_request.schedule_export.empty()) {
        export_max_available = get_max_available_energy(energy_flow_request.schedule_export);
    } else {
        // nothing is available as nothing was requested
        export_max_available = globals.zero_schedule_req;
    }

    if (not energy_flow_request.schedule_setpoints.empty()) {
        setpoints = resample(energy_flow_request.schedule_setpoints);
    } else {
        // create an empty setpoint schedule
        setpoints = globals.empty_schedule_setpoints;
    }

    // Try to find a frequency measurement
    std::optional<float> freq;
    if (energy_flow_request.energy_usage_root.has_value() and
        energy_flow_request.energy_usage_root.value().frequency_Hz.has_value()) {
        freq = energy_flow_request.energy_usage_root.value().frequency_Hz.value().L1;
    } else if (energy_flow_request.energy_usage_leaves.has_value() and
               energy_flow_request.energy_usage_leaves.value().frequency_Hz.has_value()) {
        freq = energy_flow_request.energy_usage_leaves.value().frequency_Hz.value().L1;
    }

    // Apply setpoints as limit to both import and export schedules
    apply_setpoints(import_max_available, export_max_available, setpoints, freq);

    // Recursion: create one Market for each child
    for (auto& flow_child : _energy_flow_request.children) {
        _children.emplace_back(flow_child, _nominal_ac_voltage, this);
    }
}

const ScheduleRes& Market::get_sold_energy() const {
    return sold_root;
}

Market* Market::parent() {
    return _parent;
}

bool Market::is_root() {
    return _parent == nullptr;
}

void Market::get_list_of_evses(std::vector<Market*>& list) {
    if (energy_flow_request.node_type == types::energy::NodeType::Evse) {
        list.push_back(this);
    }

    for (auto& child : _children) {
        child.get_list_of_evses(list);
    }
}

std::vector<Market*> Market::get_list_of_evses() {
    std::vector<Market*> list;
    if (energy_flow_request.node_type == types::energy::NodeType::Evse) {
        list.push_back(this);
    }

    for (auto& child : _children) {
        child.get_list_of_evses(list);
    }
    return list;
}

static void schedule_add(ScheduleRes& a, const ScheduleRes& b) {
    if (a.size() != b.size()) {
        EVLOG_critical << "schedule_add: Schedules are not of the same size: a: " << a.size() << " b: " << b.size();
        return;
    }

    const types::energy::NumberWithSource NUMZERO = {0};

    for (ScheduleRes::size_type i = 0; i < a.size(); i++) {
        if (b[i].limits_to_root.ac_max_current_A.has_value()) {
            std::string source;

            if (b[i].limits_to_root.ac_max_current_A.value().value not_eq 0.) {
                source = b[i].limits_to_root.ac_max_current_A.value().source;
            } else if (a[i].limits_to_root.ac_max_current_A.has_value()) {
                source = a[i].limits_to_root.ac_max_current_A.value().source;
            }

            a[i].limits_to_root.ac_max_current_A = {b[i].limits_to_root.ac_max_current_A.value().value +
                                                        a[i].limits_to_root.ac_max_current_A.value_or(NUMZERO).value,
                                                    source};
        }

        if (b[i].limits_to_root.total_power_W.has_value()) {
            std::string source;

            if (b[i].limits_to_root.total_power_W.value().value not_eq 0.) {
                source = b[i].limits_to_root.total_power_W.value().source;
            } else if (a[i].limits_to_root.total_power_W.has_value()) {
                source = a[i].limits_to_root.total_power_W.value().source;
            }

            a[i].limits_to_root.total_power_W = {b[i].limits_to_root.total_power_W.value().value +
                                                     a[i].limits_to_root.total_power_W.value_or(NUMZERO).value,
                                                 source};
        }

        if (b[i].limits_to_root.ac_max_phase_count.has_value()) {
            if (a[i].limits_to_root.ac_max_phase_count.has_value()) {
                if (b[i].limits_to_root.ac_max_phase_count.value().value >
                    a[i].limits_to_root.ac_max_phase_count.value().value) {
                    a[i].limits_to_root.ac_max_phase_count = b[i].limits_to_root.ac_max_phase_count;
                }
            } else {
                a[i].limits_to_root.ac_max_phase_count = b[i].limits_to_root.ac_max_phase_count;
            }
        }
    }
}

void Market::book(const ScheduleRes& traded, const ScheduleRes& drawn, const PhaseSet& phases) {
    schedule_add(sold_root, traded);
    for (ScheduleRes::size_type i = 0; i < drawn.size() and i < m_sold_drawn_W.size(); i++) {
        if (drawn[i].limits_to_root.total_power_W.has_value()) {
            m_sold_drawn_W[i] += drawn[i].limits_to_root.total_power_W.value().value;
        }
    }
    for (ScheduleRes::size_type i = 0; i < traded.size() and i < sold_phase_A.size(); i++) {
        if (not traded[i].limits_to_root.ac_max_current_A.has_value()) {
            continue;
        }
        for (const auto phase : phases) {
            sold_phase_A[i][static_cast<int>(phase)] += traded[i].limits_to_root.ac_max_current_A.value().value;
        }
    }
}

void Market::trade(const ScheduleRes& traded, const PhaseSet& requested_phases) {
    const auto& phases = or_all_phases(requested_phases);

    // The watt figure on the phases drawn, which is what every node's watt limit is spent on.
    ScheduleRes upstream = traded;
    for (auto& entry : upstream) {
        auto& limits = entry.limits_to_root;
        if (not limits.total_power_W.has_value() or not limits.ac_max_phase_count.has_value()) {
            continue;
        }
        const int declared = limits.ac_max_phase_count.value().value;
        const int drawn = static_cast<int>(phases.size());
        if (declared > 0 and drawn < declared) {
            limits.total_power_W.value().value *= static_cast<float>(drawn) / static_cast<float>(declared);
        }
    }
    book(traded, upstream, phases);
    for (Market* node = parent(); node != nullptr; node = node->parent()) {
        node->book(upstream, upstream, phases);
    }
}

float Market::nominal_ac_voltage() {
    return _nominal_ac_voltage;
}

} // namespace module
