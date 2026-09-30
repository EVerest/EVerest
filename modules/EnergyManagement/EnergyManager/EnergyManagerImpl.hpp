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

/// \brief The module's manifest options, each defaulted to its manifest default so a test
/// that does not set an option gets a defined value.
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

    /// \brief The aggregated leaf power meter reading of the most recent run_optimizer()
    /// call, by value: the worker thread may be running the next one.
    PowerMeterAggregator::AggregateResult get_leaf_aggregate() const;

private:
    void warn_about_meter_timestamps(const PowerMeterAggregator::AggregateResult& aggregate);

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

    PowerMeterAggregator::AggregateResult m_leaf_aggregate;

    // Meters already warned about, so each fault is logged once until the meter recovers.
    std::set<std::string> m_warned_unparsable_meters;
    std::set<std::string> m_warned_future_meters;
    std::set<std::string> m_warned_far_past_meters;
};

} // namespace module
