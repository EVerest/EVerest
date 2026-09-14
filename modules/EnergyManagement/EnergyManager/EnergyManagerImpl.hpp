// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// headers for provided interface implementations
#include <generated/interfaces/energy_manager/Implementation.hpp>
// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

#include <mutex>

#include <Broker.hpp>
#include <BrokerPowerRedistribution.hpp>
#include <PowerMeterAggregator.hpp>

#include <memory>

namespace module {

/// \brief The module's manifest options.
///
/// Every member carries its manifest default, so an option a caller forgets to set reads as
/// that default instead of an indeterminate value. Production always assigns all of them
/// from the generated config; the defaults exist for tests, where a missed option used to
/// reach EnergyManagerImpl as garbage (a negative aggregation window silently switched the
/// staleness filter off).
struct EnergyManagerConfig {
    double nominal_ac_voltage{230.0};
    int update_interval{1};
    int schedule_interval_duration{60};
    int schedule_total_duration{1};
    double slice_ampere{0.5};
    double slice_watt{500};
    bool debug{false};
    std::string switch_3ph1ph_while_charging_mode{"Never"};
    int switch_3ph1ph_max_nr_of_switches_per_session{0};
    std::string switch_3ph1ph_switch_limit_stickyness{"DontChange"};
    int switch_3ph1ph_power_hysteresis_W{200};
    int switch_3ph1ph_time_hysteresis_s{600};
    std::string broker_strategy{"FastCharging"};
    int power_meter_aggregation_window_s{5};
    double power_redistribution_margin{0.1};
    double power_redistribution_gain{0.5};
    int power_redistribution_hold_time_s{10};
};

/// \brief Broker selected by the broker_strategy config option (see manifest.yaml).
enum class BrokerStrategy {
    FastCharging,
    PowerRedistribution,
};

/// \brief What the power redistribution inference concluded in the most recent optimizer
/// run: the site view and one entry per EVSE in the tree. Empty with the FastCharging
/// strategy.
struct RedistributionInference {
    SiteInference site;
    std::map<std::string, ConnectorInference> connectors;
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

    /// \brief Returns the reading the measurement tracking broker last observed for
    /// connector \p uuid: total power [W] and per-phase current [A] (L1/L2/L3). Values
    /// without a measurement are std::nullopt (all of them if tracking is disabled, no
    /// measurement is available, or no active session).
    ObservedMeasurement get_observed_measurement(const std::string& uuid);

    /// \brief The aggregated leaf power meter reading computed during the most recent
    /// run_optimizer() call. Readings older than power_meter_aggregation_window_s are
    /// excluded from the sums.
    /// Returned by value under the optimizer lock: run_optimizer() runs on a detached
    /// thread once start() has been called, so a reference into the live state would be a
    /// data race for any external caller.
    PowerMeterAggregator::AggregateResult get_leaf_aggregate() const;

    /// \brief The power redistribution inference of the most recent run_optimizer() call.
    /// Returned by value under the optimizer lock, like get_leaf_aggregate().
    RedistributionInference get_redistribution_inference() const;

private:
    /// \brief Runs the log-only power redistribution inference for one optimizer run, after
    /// trading. Compares each connector's measurement with the allocation of the previous
    /// run, the site aggregate with the grid limit, applies the hold time and logs
    /// candidates on change. Called under energy_mutex.
    void infer_redistribution(const types::energy::EnergyFlowRequest& request,
                              const std::vector<std::shared_ptr<Broker>>& brokers,
                              const std::vector<types::energy::EnforcedLimits>& limits);

    EnergyManagerConfig config;
    BrokerStrategy broker_strategy;
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

    RedistributionInference redistribution_inference;
    // start_time of the run since which the site has continuously had headroom to hand
    // out; nullopt while it has not. Site counterpart of BrokerContext::under_consuming_since.
    std::optional<date::utc_clock::time_point> headroom_since;
    bool increase_reported{false};
};

} // namespace module
