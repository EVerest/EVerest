// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// headers for provided interface implementations
#include <generated/interfaces/energy_manager/Implementation.hpp>
// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

#include <mutex>

#include <Broker.hpp>
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
};

class EnergyManagerImpl {

public:
    EnergyManagerImpl(
        const EnergyManagerConfig& config,
        const std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)>& enforced_limits_callback);

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
    const PowerMeterAggregator::AggregateResult& get_leaf_aggregate() const;

private:
    EnergyManagerConfig config;
    std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)> enforced_limits_callback;

    std::mutex energy_mutex;
    std::condition_variable mainloop_sleep_condvar;
    std::mutex mainloop_sleep_mutex;

    // complete energy tree request
    types::energy::EnergyFlowRequest energy_flow_request;

    std::map<std::string, BrokerContext> contexts;

    // Aggregates the leaf power meter readings of the tree. Rebuilt on every optimizer run.
    std::unique_ptr<PowerMeterAggregator> leaf_aggregator;
    PowerMeterAggregator::AggregateResult leaf_aggregate;
};

} // namespace module
