// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// headers for provided interface implementations
#include <generated/interfaces/energy_manager/Implementation.hpp>
// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

#include <atomic>
#include <mutex>
#include <thread>

#include <Broker.hpp>
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
    double redistribution_margin_A{2.0};
    bool redistribution_start_with_lower_limit{true};
    int redistribution_reduction_hold_s{30};
    int redistribution_measurement_max_age_s{10};
    int power_meter_aggregation_window_s{5};
};

/// \brief Broker selected by the broker_strategy config option (see manifest.yaml).
enum class BrokerStrategy {
    FastCharging,
    PowerRedistribution,
};

class EnergyManagerImpl {

public:
    EnergyManagerImpl(
        const EnergyManagerConfig& config,
        const std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)>& enforced_limits_callback);

    ~EnergyManagerImpl();

    /// \brief Starts the worker thread that runs run_optimizer periodically or when the
    /// energy flow request is updated. Calling it twice is a no-op.
    void start();

    /// \brief Stops the worker thread started by start() and waits for it to finish.
    /// Idempotent, and safe to call when start() never ran. Called from the module's
    /// shutdown hook and from the destructor, so the thread cannot outlive the object whose
    /// state it reads on every cycle.
    void stop();

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

#ifdef BUILD_TESTING_MODULE_ENERGY_MANAGER
    /// \brief Returns the reading the power redistribution broker last observed for
    /// connector \p uuid: total power [W], per-phase current [A] (L1/L2/L3) and the
    /// reading's own measurement time. Values without a measurement are std::nullopt (all
    /// of them if tracking is disabled, no measurement is available, or no active session).
    ///
    /// Test observation only. Nothing in production reads it, and the class it hangs off
    /// decides the current limit of every connector on the site, so it is not part of that
    /// class's API. The tests define BUILD_TESTING_MODULE_ENERGY_MANAGER (see
    /// tests/CMakeLists.txt).
    ObservedMeasurement get_observed_measurement(const std::string& uuid);
#endif

    /// \brief The aggregated leaf power meter reading computed during the most recent
    /// run_optimizer() call. Readings older than power_meter_aggregation_window_s are
    /// excluded from the sums.
    /// Returned by value under the optimizer lock: run_optimizer() runs on a detached
    /// thread once start() has been called, so a reference into the live state would be a
    /// data race for any external caller.
    PowerMeterAggregator::AggregateResult get_leaf_aggregate() const;

private:
    EnergyManagerConfig config;
    BrokerStrategy broker_strategy;
    std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)> enforced_limits_callback;

    mutable std::mutex energy_mutex;
    std::condition_variable mainloop_sleep_condvar;
    std::mutex mainloop_sleep_mutex;

    // Worker thread running the optimizer loop, and the flag that ends it. The thread is
    // joined rather than detached: it reads config, contexts and the energy flow request of
    // this object on every cycle, so it must not outlive it.
    // running is written only under mainloop_sleep_mutex, the mutex the worker waits on, so
    // a stop() cannot slip past the wait predicate; it is atomic so the loop condition can
    // read it without taking the lock every cycle.
    std::thread mainloop;
    std::atomic<bool> running{false};

    // Set by on_energy_flow_request() for a priority request, to run the optimizer before
    // the update interval is up; cleared by the worker once it has woken. Guarded by
    // mainloop_sleep_mutex, not atomic: unlike running it is only ever touched while
    // holding that mutex, and the wait predicate must see it and the notification together.
    bool wakeup{false};

    // complete energy tree request
    types::energy::EnergyFlowRequest energy_flow_request;

    std::map<std::string, BrokerContext> contexts;

    // Aggregates the leaf power meter readings of the tree. Rebuilt on every optimizer run.
    std::unique_ptr<PowerMeterAggregator> leaf_aggregator;
    PowerMeterAggregator::AggregateResult leaf_aggregate;
};

} // namespace module
