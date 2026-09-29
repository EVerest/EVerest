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
#include <BrokerPowerRedistribution.hpp>
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
    int redistribution_reduction_hold_s{10};
    int power_meter_aggregation_window_s{5};
    double power_redistribution_connector_margin{0.1};
    double power_redistribution_site_margin{0.1};
    double power_redistribution_gain{0.5};
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

#ifdef BUILD_TESTING_MODULE_ENERGY_MANAGER
    /// \brief The site power meter reading computed during the most recent run_optimizer()
    /// call: the grid connection's own meter where there is one, otherwise the sum of the
    /// EVSE meters. Readings older than power_meter_aggregation_window_s are excluded.
    ///
    /// Test observation only, like get_observed_measurement(); nothing in production reads
    /// it. Returned by value under the optimizer lock, since run_optimizer() runs on the
    /// worker thread once start() has been called.
    PowerMeterAggregator::AggregateResult get_site_aggregate() const;

    /// \brief The power redistribution inference of the most recent run_optimizer() call.
    /// Test observation only, returned by value under the optimizer lock.
    RedistributionInference get_redistribution_inference() const;
#endif

private:
    void warn_about_meter_timestamps(const PowerMeterAggregator::AggregateResult& aggregate);

    /// \brief Runs the power redistribution inference for one optimizer run, after trading.
    /// Compares each connector's measurement with the allocation of the previous run, the
    /// site aggregate with the grid limit, applies the hold time and logs candidates on
    /// change. Called under energy_mutex.
    void infer_redistribution(const Market& market, const std::vector<std::shared_ptr<Broker>>& brokers,
                              const std::vector<types::energy::EnforcedLimits>& limits);

    /// \brief Writes each connector's share of the site headroom into its BrokerContext, or
    /// clears it, so the brokers of the next run hand it to the EVs.
    ///
    /// It has to be the next run: the inference needs this run's enforced limits to know
    /// what each connector was allotted, and by the time those exist the trading is over.
    /// One optimizer interval of delay is also what makes the loop settle - a grant acts on
    /// a measurement taken before it was handed out, so applying it twice within one
    /// interval would count the same headroom twice.
    ///
    /// Clearing every run rather than only on change is what keeps a grant from outliving
    /// the condition it was granted under: a connector that stops being saturated, a meter
    /// that goes stale, or a headroom that closes all simply stop writing an entry.
    /// \returns the number of connectors that were granted an increase, 0 while the site
    /// has nothing to hand out
    int grant_site_headroom(const SiteInference& site);

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

    // Aggregated site power meter reading of the most recent optimizer run. The aggregator
    // that produces it is a local of that run: it holds nothing worth keeping between runs,
    // and a member would have to be cleared by hand to stop a departed meter contributing.
    PowerMeterAggregator::AggregateResult site_aggregate;
    SiteMeterSource site_meter_source{SiteMeterSource::None};

    // Meters already warned about, so each fault is logged once until the meter recovers.
    std::set<std::string> m_warned_unparsable_meters;
    std::set<std::string> m_warned_future_meters;

    RedistributionInference redistribution_inference;
    // How long the site has continuously had headroom to hand out, and whether that has
    // already been reported. The same latch BrokerContext uses per connector, so the two
    // cannot drift apart the way two hand-written copies did.
    HoldLatch site_headroom;
};

} // namespace module
