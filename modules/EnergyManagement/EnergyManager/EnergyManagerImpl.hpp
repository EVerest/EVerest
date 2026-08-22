// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// headers for provided interface implementations
#include <generated/interfaces/energy_manager/Implementation.hpp>
// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

#include <mutex>

#include <Broker.hpp>
#include <MeasurementTrackingState.hpp>
#include <PowerMeterAggregator.hpp>

#include <memory>

namespace module {

struct EnergyManagerConfig {
    double nominal_ac_voltage;
    int update_interval;
    int schedule_interval_duration;
    int schedule_total_duration;
    double slice_ampere;
    double slice_watt;
    bool debug;
    std::string switch_3ph1ph_while_charging_mode;
    int switch_3ph1ph_max_nr_of_switches_per_session;
    std::string switch_3ph1ph_switch_limit_stickyness;
    int switch_3ph1ph_power_hysteresis_W;
    int switch_3ph1ph_time_hysteresis_s;
    bool use_power_meter_tracking;
    double power_meter_tracking_initial_current_A;
    double power_meter_tracking_margin_W;
    int power_meter_aggregation_window_s;
    double boost_threshold_W;
    double boost_step_A;
    int boost_hysteresis_cycles;
    bool phase_symmetry_enabled;
    double max_phase_imbalance_A;
};

class EnergyManagerImpl {

public:
    EnergyManagerImpl(
        const EnergyManagerConfig& config,
        const std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)>& enforced_limits_callback,
        const std::function<void(bool)>& power_can_be_reduced_callback = nullptr);

    /// \brief Starts and detaches worker thread that runs run_optimizer periodically or when energy flow request is
    /// updated
    void start();

    /// \brief Updates the energy_flow_request and notifies the worker thread
    /// \param e
    void on_energy_flow_request(const types::energy::EnergyFlowRequest& e);

    /// \brief Runs optimization on the given \p request
    /// \param request
    /// \param start_time
    /// \return a vector of limits to enforce at the individual nodes of the \p request
    std::vector<types::energy::EnforcedLimits> run_optimizer(const types::energy::EnergyFlowRequest& request,
                                                             date::utc_clock::time_point start_time,
                                                             const std::string& test_name = "");

    /// \brief The aggregated leaf power meter reading computed during the most recent
    /// run_optimizer() call. Readings older than power_meter_aggregation_window_s are
    /// excluded from the sums.
    /// Returned by value under the optimizer lock: run_optimizer() runs on a detached
    /// thread once start() has been called, so a reference into the live state would be a
    /// data race for any external caller.
    PowerMeterAggregator::AggregateResult get_leaf_aggregate() const;

    /// \brief True when the enforced allocation exceeds the measured consumption by more
    /// than boost_threshold_W, i.e. allocation could be released without curtailing charging.
    bool get_power_can_be_reduced() const;

    /// \brief Current widening of the tracking limit granted by the boosting state machine
    /// [A per phase]. Zero when boosting is inactive.
    float get_boost_offset_A() const;

private:
    EnergyManagerConfig config;
    std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)> enforced_limits_callback;

    mutable std::mutex energy_mutex;
    std::condition_variable mainloop_sleep_condvar;
    std::mutex mainloop_sleep_mutex;

    // complete energy tree request
    types::energy::EnergyFlowRequest energy_flow_request;

    std::map<std::string, BrokerContext> contexts;

    // Aggregates the leaf power meter readings of the tree. Rebuilt on every optimizer run.
    std::unique_ptr<PowerMeterAggregator> leaf_aggregator;
    PowerMeterAggregator::AggregateResult leaf_aggregate;

    std::function<void(bool)> power_can_be_reduced_callback;

    // Boosting state that must survive between optimizer runs.
    MeasurementTrackingState tracking_state;
    // Total power handed out by the previous optimizer run, used to decide reducibility.
    float last_allocated_W{0.f};
    // Last value handed to power_can_be_reduced_callback. Unset until the first publish,
    // so the initial value is always published even when it is false.
    std::optional<bool> last_published_power_can_be_reduced;
};

} // namespace module
