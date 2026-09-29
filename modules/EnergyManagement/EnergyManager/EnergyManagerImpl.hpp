// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

// headers for provided interface implementations
#include <generated/interfaces/energy_manager/Implementation.hpp>
// headers for required interface implementations
#include <generated/interfaces/energy/Interface.hpp>

#include <mutex>
#include <thread>

#include <Broker.hpp>
#include <PowerMeterAggregator.hpp>
#include <everest/util/async/monitor.hpp>

#include <memory>
#include <set>

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

    /// \brief Stops the worker thread and waits for it to finish. A run that has not yet
    /// called enforced_limits_callback skips it; one already inside it is waited for.
    /// Idempotent, and safe to call when start() never ran.
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
    /// \brief Test observation only: the reading the power redistribution broker last
    /// observed for connector \p uuid, all fields std::nullopt if there is none.
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
    /// \brief Logs the meters aggregate() reported as having an unparsable timestamp, once
    /// per meter rather than once per optimizer run.
    void warn_about_unparsable_meters(const std::vector<std::string>& unparsable);

    EnergyManagerConfig config;
    BrokerStrategy broker_strategy;
    std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)> enforced_limits_callback;

    mutable std::mutex energy_mutex;

    struct LoopState {
        bool running{false};
        // A priority request asks for a run before the update interval is up.
        bool wakeup{false};
    };
    everest::lib::util::monitor<LoopState> m_loop_state;
    // Joined, not detached: it reads this object's state on every run.
    std::thread m_mainloop;

    // complete energy tree request
    types::energy::EnergyFlowRequest energy_flow_request;

    std::map<std::string, BrokerContext> contexts;

    // Aggregated leaf power meter reading of the most recent optimizer run. The aggregator
    // that produces it is a local of that run: it holds nothing worth keeping between runs,
    // and a member would have to be cleared by hand to stop a departed meter contributing.
    PowerMeterAggregator::AggregateResult leaf_aggregate;

    // Meters already warned about for an unparsable timestamp. The warn-once decision needs
    // the history that a single aggregation does not have, so it lives here rather than in
    // the aggregator. An entry is dropped once the meter delivers a usable timestamp again.
    std::set<std::string> warned_unparsable_meters;
};

} // namespace module
