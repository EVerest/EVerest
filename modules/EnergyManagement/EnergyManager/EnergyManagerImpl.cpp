// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <EnergyManagerImpl.hpp>

#include <chrono>
#include <fstream>

#include "Broker.hpp"
#include "BrokerFastCharging.hpp"
#include "BrokerMeasurementTracking.hpp"
#include "Market.hpp"
#include "MeasurementTrackingState.hpp"
#include "PowerMeterAggregator.hpp"

namespace module {

static BrokerFastCharging::Switch1ph3phMode to_switch_1ph3ph_mode(const std::string& m) {
    if (m == "Both") {
        return BrokerFastCharging::Switch1ph3phMode::Both;
    } else if (m == "Oneway") {
        return BrokerFastCharging::Switch1ph3phMode::Oneway;
    } else {
        return BrokerFastCharging::Switch1ph3phMode::Never;
    }
}

static BrokerFastCharging::StickyNess to_stickyness(const std::string& m) {
    if (m == "DontChange") {
        return BrokerFastCharging::StickyNess::DontChange;
    } else if (m == "SinglePhase") {
        return BrokerFastCharging::StickyNess::SinglePhase;
    } else {
        return BrokerFastCharging::StickyNess::ThreePhase;
    }
}

static BrokerFastCharging::EnergyManagerConfig to_broker_fast_charging_config(const EnergyManagerConfig& config) {
    BrokerFastCharging::EnergyManagerConfig broker_conf;

    broker_conf.max_nr_of_switches_per_session = config.switch_3ph1ph_max_nr_of_switches_per_session;
    broker_conf.power_hysteresis_W = config.switch_3ph1ph_power_hysteresis_W;
    broker_conf.switch_1ph_3ph_mode = to_switch_1ph3ph_mode(config.switch_3ph1ph_while_charging_mode);
    broker_conf.time_hysteresis_s = config.switch_3ph1ph_time_hysteresis_s;
    broker_conf.stickyness = to_stickyness(config.switch_3ph1ph_switch_limit_stickyness);
    broker_conf.use_power_meter_tracking = config.use_power_meter_tracking;
    broker_conf.tracking_initial_current_A = static_cast<float>(config.power_meter_tracking_initial_current_A);
    broker_conf.tracking_margin_W = static_cast<float>(config.power_meter_tracking_margin_W);

    return broker_conf;
}

// Check if any node set the priority request flag
bool is_priority_request(const types::energy::EnergyFlowRequest& e) {
    bool prio = e.priority_request.has_value() and e.priority_request.value();

    // If this node has priority, no need to travese the tree any longer
    if (prio) {
        return true;
    }

    // recurse to all children
    for (auto& c : e.children) {
        if (is_priority_request(c)) {
            return true;
        }
    }

    return false;
}

EnergyManagerImpl::EnergyManagerImpl(
    const EnergyManagerConfig& config,
    const std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)>& enforced_limits_callback,
    const std::function<void(bool)>& power_can_be_reduced_callback) :
    config(config),
    enforced_limits_callback(enforced_limits_callback),
    power_can_be_reduced_callback(power_can_be_reduced_callback) {
    this->energy_flow_request.node_type = types::energy::NodeType::Undefined;
    this->leaf_aggregator =
        std::make_unique<PowerMeterAggregator>(std::chrono::seconds(config.power_meter_aggregation_window_s));
}

PowerMeterAggregator::AggregateResult EnergyManagerImpl::get_leaf_aggregate() const {
    std::scoped_lock lock(energy_mutex);
    return leaf_aggregate;
}

bool EnergyManagerImpl::get_power_can_be_reduced() const {
    std::scoped_lock lock(energy_mutex);
    return tracking_state.power_can_be_reduced;
}

float EnergyManagerImpl::get_boost_offset_A() const {
    std::scoped_lock lock(energy_mutex);
    return tracking_state.boost_offset_A;
}

void EnergyManagerImpl::start() {
    // start thread to update energy optimization
    std::thread([this] {
        while (true) {
            auto optimized_values = this->run_optimizer(energy_flow_request, date::utc_clock::now());
            enforced_limits_callback(optimized_values);
            {
                std::unique_lock<std::mutex> lock(mainloop_sleep_mutex);
                mainloop_sleep_condvar.wait_for(lock, std::chrono::seconds(config.update_interval));
            }
        }
    }).detach();
}

void EnergyManagerImpl::on_energy_flow_request(const types::energy::EnergyFlowRequest& e) {
    // Received new energy object from a child.
    std::scoped_lock lock(energy_mutex);
    energy_flow_request = e;

    if (is_priority_request(e)) {
        // trigger optimization now
        mainloop_sleep_condvar.notify_all();
    }
}

std::vector<types::energy::EnforcedLimits>
EnergyManagerImpl::run_optimizer(const types::energy::EnergyFlowRequest& request,
                                 date::utc_clock::time_point start_time, const std::string& test_name) {
    // The publish decision is made under the lock but the callback fires after it is
    // released, mirroring how start() invokes enforced_limits_callback: a blocking MQTT
    // publish must not stall on_energy_flow_request on the framework subscriber threads.
    std::optional<bool> publish_power_can_be_reduced;

    const auto optimized_values = [&]() {
        std::scoped_lock lock(energy_mutex);

        globals.init(start_time, config.schedule_interval_duration, config.schedule_total_duration, config.slice_ampere,
                     config.slice_watt, config.debug, request);

        // Refresh the aggregated leaf measurements for this run. Clearing first means a
        // connector that disappeared from the tree stops contributing straight away.
        leaf_aggregator->clear();
        collect_leaf_measurements(request, *leaf_aggregator);
        leaf_aggregate = leaf_aggregator->aggregate(globals.start_time);

        // Advance the boosting state before any broker trades, so this cycle's allocations
        // already use the new offset.
        {
            MeasurementTrackingInput tracking_input;
            tracking_input.aggregate = leaf_aggregate;
            tracking_input.grid_limit_W = get_grid_limit_W(request, static_cast<float>(config.nominal_ac_voltage));
            // Reducibility compares allocation against consumption, which is only meaningful
            // while tracking manages the allocation. With tracking off a static allocation
            // always exceeds consumption, and publishing a permanently-true flag would invite
            // external consumers to act on it. Zero here forces the flag to false.
            tracking_input.last_allocated_W = config.use_power_meter_tracking ? last_allocated_W : 0.f;
            tracking_input.boost_threshold_W = static_cast<float>(config.boost_threshold_W);
            tracking_input.boost_step_A =
                config.use_power_meter_tracking ? static_cast<float>(config.boost_step_A) : 0.f;
            tracking_input.boost_hysteresis_cycles = config.boost_hysteresis_cycles;

            // Boosting further than the whole grid limit is meaningless; bounding the offset
            // also keeps it from taking many cycles to unwind once headroom disappears. The
            // bound is derived from whatever form the grid limit takes - a watt-only root
            // (declaring total_power_W but no ampere limit) must not leave the offset unbounded.
            if (tracking_input.grid_limit_W.has_value()) {
                tracking_input.max_boost_offset_A =
                    tracking_input.grid_limit_W.value() / (3.0f * static_cast<float>(config.nominal_ac_voltage));
            }

            tracking_state = advance_tracking_state(tracking_state, tracking_input);
        }

        time_probe optimizer_start;
        optimizer_start.start();
        if (globals.debug)
            EVLOG_info << "\033[1;44m---------------- Run energy optimizer ---------------- \033[1;0m";

        if (globals.debug) {
            EVLOG_info << fmt::format("Aggregated leaf power: {}W from {} meter(s), {} stale", leaf_aggregate.power_W,
                                      leaf_aggregate.fresh_meters, leaf_aggregate.stale_meters);
        }

        time_probe market_tp;

        //  create market for trading energy based on the request tree
        market_tp.start();
        Market market(request, config.nominal_ac_voltage);
        market_tp.pause();

        // create brokers for all evses (they buy/sell energy on behalf of EvseManagers)
        std::vector<std::shared_ptr<Broker>> brokers;

        auto evse_markets = market.get_list_of_evses();

        auto broker_conf = to_broker_fast_charging_config(config);
        broker_conf.boost_offset_A = tracking_state.boost_offset_A;

        for (auto m : evse_markets) {
            // Check if we need to clear the context
            // Note that context is created here if it does not exist implicitly by operator[] of the map
            if (m->energy_flow_request.evse_state == types::energy::EvseState::Unplugged or
                m->energy_flow_request.evse_state == types::energy::EvseState::Finished) {
                contexts[m->energy_flow_request.uuid].clear();
                contexts[m->energy_flow_request.uuid].ts_1ph_optimal =
                    globals.start_time - std::chrono::seconds(config.switch_3ph1ph_time_hysteresis_s);
            }

            // FIXME: check for actual optimizer_targets and create correct broker for this evse
            if (config.use_power_meter_tracking) {
                brokers.push_back(std::make_shared<BrokerMeasurementTracking>(*m, contexts[m->energy_flow_request.uuid],
                                                                              broker_conf));
            } else {
                brokers.push_back(
                    std::make_shared<BrokerFastCharging>(*m, contexts[m->energy_flow_request.uuid], broker_conf));
            }
            // EVLOG_info << fmt::format("Created broker for {}", m->energy_flow_request.uuid);
        }

        // for each evse: create a custom offer at their local market place and ask the broker to buy a slice.
        // continue until no one wants to buy/sell anything anymore.

        int max_number_of_trading_rounds = 100;
        time_probe offer_tp;
        time_probe broker_tp;

        while (max_number_of_trading_rounds-- > 0) {
            bool trade_happend_in_this_round = false;
            for (auto const& broker : brokers) {
                // EVLOG_info << broker->get_local_market().energy_flow_request;
                //     create local offer at evse's marketplace

                offer_tp.start();
                Offer local_offer(broker->get_local_market());
                offer_tp.pause();

                // ask broker to trade
                broker_tp.start();
                if (broker->trade(local_offer))
                    trade_happend_in_this_round = true;
                broker_tp.pause();
            }
            if (!trade_happend_in_this_round)
                break;
        }

        if (max_number_of_trading_rounds <= 0) {
            EVLOG_error << "Trading: Maximum number of trading rounds reached.";
        }

        if (globals.debug) {
            EVLOG_info << fmt::format(
                "\033[1;44m---------------- End energy optimizer ({} rounds, offer {}ms market {}ms "
                "broker {}ms total {}ms) ---------------- \033[1;0m",
                100 - max_number_of_trading_rounds, offer_tp.stop(), market_tp.stop(), broker_tp.stop(),
                optimizer_start.stop());
        }

        std::vector<types::energy::EnforcedLimits> optimized_values;
        optimized_values.reserve(brokers.size());

        for (auto& broker : brokers) {
            auto& local_market = broker->get_local_market();
            const auto sold_energy = local_market.get_sold_energy();

            if (sold_energy.size() > 0) {
                types::energy::EnforcedLimits l;
                l.uuid = local_market.energy_flow_request.uuid;
                l.valid_for = config.update_interval * 10;

                l.schedule = sold_energy;

                // select root limit from schedule based on globals.start_time
                l.limits_root_side = sold_energy[0].limits_to_root;

                for (const auto& s : sold_energy) {
                    const auto schedule_time = Everest::Date::from_rfc3339(s.timestamp);
                    if (globals.start_time < schedule_time) {
                        // all further schedules will be further into the future
                        break;
                    } else {
                        // use this schedule as the starting point
                        l.limits_root_side = s.limits_to_root;
                    }
                }

                optimized_values.push_back(l);

                if (globals.debug) {
                    EVLOG_info << "Sending enforced limits (import) to :" << l.uuid << " " << l.limits_root_side;
                }
            }
        }

        // Print out test case file
        if (not test_name.empty()) {
            json test_case;
            test_case["start_time"] = Everest::Date::to_rfc3339(start_time);
            test_case["request"] = json(request);
            test_case["expected_result"] = json(optimized_values);
            std::ofstream out(test_name.c_str());
            out << test_case;
            out.close();
        }

        // Remember what we handed out so the next cycle can judge whether it can be reduced.
        last_allocated_W = sum_allocated_W(optimized_values, static_cast<float>(config.nominal_ac_voltage));

        // Publish the reducibility flag on change, and always once at start up.
        if (power_can_be_reduced_callback and
            (not last_published_power_can_be_reduced.has_value() or
             last_published_power_can_be_reduced.value() != tracking_state.power_can_be_reduced)) {
            last_published_power_can_be_reduced = tracking_state.power_can_be_reduced;
            publish_power_can_be_reduced = tracking_state.power_can_be_reduced;
        }

        return optimized_values;
    }();

    if (publish_power_can_be_reduced.has_value()) {
        power_can_be_reduced_callback(publish_power_can_be_reduced.value());
    }

    return optimized_values;
}

} // namespace module
