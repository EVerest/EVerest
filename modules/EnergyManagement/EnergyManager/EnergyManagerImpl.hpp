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
#include <everest/util/async/monitor.hpp>

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
    std::string broker_strategy;
    double redistribution_margin_A;
    bool redistribution_start_with_lower_limit;
    int redistribution_reduction_hold_s;
    int redistribution_measurement_max_age_s;
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

    /// \brief Asks the worker thread to end after its current run, without waiting for it.
    /// Called from the module's shutdown hook: the run may be blocked in an enforce_limits
    /// command, which only fails once that hook has returned.
    void request_stop();

    /// \brief request_stop(), then waits for the worker thread to finish. Idempotent, and
    /// safe to call when start() never ran.
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

private:
    EnergyManagerConfig config;
    BrokerStrategy broker_strategy;
    std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)> enforced_limits_callback;

    std::mutex energy_mutex;

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
};

} // namespace module
