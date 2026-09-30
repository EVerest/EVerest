// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <EnergyManagerImpl.hpp>

#include <chrono>
#include <fstream>
#include <iterator>
#include <set>

#include "Broker.hpp"
#include "BrokerFastCharging.hpp"
#include "BrokerPowerRedistribution.hpp"
#include "Market.hpp"
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

static BrokerStrategy to_broker_strategy(const std::string& s) {
    if (s == "PowerRedistribution") {
        return BrokerStrategy::PowerRedistribution;
    }
    // The manifest enum rejects typos; configs built otherwise are not validated.
    if (s != "FastCharging") {
        EVLOG_warning << "Unknown broker_strategy '" << s << "', falling back to FastCharging";
    }
    return BrokerStrategy::FastCharging;
}

static std::shared_ptr<Broker> make_broker(BrokerStrategy strategy, Market& market, BrokerContext& context,
                                           const Broker::EnergyManagerConfig& broker_config) {
    switch (strategy) {
    case BrokerStrategy::PowerRedistribution:
        return std::make_shared<BrokerPowerRedistribution>(market, context, broker_config);
    case BrokerStrategy::FastCharging:
    default:
        return std::make_shared<BrokerFastCharging>(market, context, broker_config);
    }
}

static Broker::EnergyManagerConfig to_broker_config(const EnergyManagerConfig& config) {
    Broker::EnergyManagerConfig broker_conf;

    broker_conf.max_nr_of_switches_per_session = config.switch_3ph1ph_max_nr_of_switches_per_session;
    broker_conf.power_hysteresis_W = config.switch_3ph1ph_power_hysteresis_W;
    broker_conf.switch_1ph_3ph_mode = to_switch_1ph3ph_mode(config.switch_3ph1ph_while_charging_mode);
    broker_conf.time_hysteresis_s = config.switch_3ph1ph_time_hysteresis_s;
    broker_conf.stickyness = to_stickyness(config.switch_3ph1ph_switch_limit_stickyness);
    broker_conf.redistribution.margin_A = config.redistribution_margin_A;
    broker_conf.redistribution.start_with_lower_limit = config.redistribution_start_with_lower_limit;
    broker_conf.redistribution.reduction_hold = std::chrono::seconds(config.redistribution_reduction_hold_s);
    broker_conf.redistribution.measurement_max_age = std::chrono::seconds(config.redistribution_measurement_max_age_s);

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
    const std::function<void(const std::vector<types::energy::EnforcedLimits>& limits)>& enforced_limits_callback) :
    config(config),
    broker_strategy(to_broker_strategy(config.broker_strategy)),
    enforced_limits_callback(enforced_limits_callback) {
    this->energy_flow_request.node_type = types::energy::NodeType::Undefined;
}

namespace {

// Calls \p warn for every meter in \p current not yet in \p warned, and forgets meters that
// have recovered so they may warn again.
template <typename Warn>
void warn_once_per_meter(std::set<std::string>& warned, const std::vector<std::string>& current, Warn warn) {
    const std::set<std::string> now_faulty(current.begin(), current.end());
    for (const auto& uuid : now_faulty) {
        if (warned.insert(uuid).second) {
            warn(uuid);
        }
    }
    for (auto it = warned.begin(); it != warned.end();) {
        it = now_faulty.count(*it) == 0 ? warned.erase(it) : std::next(it);
    }
}

} // namespace

void EnergyManagerImpl::warn_about_meter_timestamps(const PowerMeterAggregator::AggregateResult& aggregate) {
    warn_once_per_meter(m_warned_unparsable_meters, aggregate.unparsable_meters, [](const std::string& uuid) {
        EVLOG_warning << "cannot parse the power meter timestamp of meter " << uuid
                      << ", treating its readings as stale until it recovers";
    });
    warn_once_per_meter(m_warned_future_meters, aggregate.future_meters, [](const std::string& uuid) {
        EVLOG_warning << "power meter timestamp of meter " << uuid
                      << " lies in the future beyond the aggregation window (clock or time zone error), "
                         "treating its readings as stale until it recovers";
    });
    warn_once_per_meter(m_warned_far_past_meters, aggregate.far_past_meters, [](const std::string& uuid) {
        EVLOG_warning << "power meter timestamp of meter " << uuid
                      << " lies more than 15 minutes before the aggregation window (frozen meter, or a UTC offset "
                         "the timestamp parser ignores), treating its readings as stale until it recovers";
    });
}

PowerMeterAggregator::AggregateResult EnergyManagerImpl::get_leaf_aggregate() const {
    std::scoped_lock lock(energy_mutex);
    return m_leaf_aggregate;
}

EnergyManagerImpl::~EnergyManagerImpl() {
    stop();
}

void EnergyManagerImpl::start() {
    {
        auto loop = m_loop_state.handle();
        if (loop->running) {
            return;
        }
        loop->running = true;
    }

    m_mainloop = std::thread([this] {
        while (true) {
            auto optimized_values = this->run_optimizer(energy_flow_request, date::utc_clock::now());
            if (not m_loop_state.handle()->running) {
                return;
            }
            try {
                enforced_limits_callback(optimized_values);
            } catch (const std::exception& e) {
                EVLOG_error << "Failed to enforce limits: " << e.what();
            }

            auto loop = m_loop_state.handle();
            loop.wait_for([&loop] { return not loop->running or loop->wakeup; },
                          std::chrono::seconds(config.update_interval));
            if (not loop->running) {
                return;
            }
            loop->wakeup = false;
        }
    });
}

void EnergyManagerImpl::stop() {
    m_loop_state.handle()->running = false;
    m_loop_state.notify_all();
    if (m_mainloop.joinable()) {
        m_mainloop.join();
    }
}

void EnergyManagerImpl::on_energy_flow_request(const types::energy::EnergyFlowRequest& e) {
    // Received new energy object from a child.
    std::scoped_lock lock(energy_mutex);
    energy_flow_request = e;

    if (is_priority_request(e)) {
        m_loop_state.handle()->wakeup = true;
        m_loop_state.notify_all();
    }
}

#ifdef BUILD_TESTING_MODULE_ENERGY_MANAGER
ObservedMeasurement EnergyManagerImpl::get_observed_measurement(const std::string& uuid) {
    std::scoped_lock lock(energy_mutex);

    const auto it = contexts.find(uuid);
    if (it == contexts.end()) {
        return {};
    }
    return it->second.last_observed_measurement;
}
#endif

std::vector<types::energy::EnforcedLimits>
EnergyManagerImpl::run_optimizer(const types::energy::EnergyFlowRequest& request,
                                 date::utc_clock::time_point start_time, const std::string& test_name) {
    std::scoped_lock lock(energy_mutex);

    globals.init(start_time, config.schedule_interval_duration, config.schedule_total_duration, config.slice_ampere,
                 config.slice_watt, config.debug, request);

    // Rebuilt every run, so a connector that left the tree stops contributing.
    PowerMeterAggregator leaf_aggregator(std::chrono::seconds(config.power_meter_aggregation_window_s));
    collect_leaf_measurements(request, leaf_aggregator);
    m_leaf_aggregate = leaf_aggregator.aggregate(globals.start_time);
    warn_about_meter_timestamps(m_leaf_aggregate);

    time_probe optimizer_start;
    optimizer_start.start();
    if (globals.debug)
        EVLOG_info << "\033[1;44m---------------- Run energy optimizer ---------------- \033[1;0m";

    if (globals.debug) {
        const auto power = m_leaf_aggregate.power_W.has_value()
                               ? fmt::format("{}W", m_leaf_aggregate.power_W.value().total)
                               : std::string("no reading");
        EVLOG_info << fmt::format("Aggregated leaf power: {} from {} meter(s), {} stale", power,
                                  m_leaf_aggregate.fresh_meters, m_leaf_aggregate.stale_meters);
    }

    time_probe market_tp;

    //  create market for trading energy based on the request tree
    market_tp.start();
    Market market(request, config.nominal_ac_voltage);
    market_tp.pause();

    // create brokers for all evses (they buy/sell energy on behalf of EvseManagers)
    std::vector<std::shared_ptr<Broker>> brokers;

    auto evse_markets = market.get_list_of_evses();

    for (auto m : evse_markets) {
        // Check if we need to clear the context
        // Note that context is created here if it does not exist implicitly by operator[] of the map
        if (not in_session(m->energy_flow_request)) {
            contexts[m->energy_flow_request.uuid].clear();
            contexts[m->energy_flow_request.uuid].ts_1ph_optimal =
                globals.start_time - std::chrono::seconds(config.switch_3ph1ph_time_hysteresis_s);
        }

        brokers.push_back(
            make_broker(broker_strategy, *m, contexts[m->energy_flow_request.uuid], to_broker_config(config)));
        brokers.back()->observe();
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
        EVLOG_info << fmt::format("\033[1;44m---------------- End energy optimizer ({} rounds, offer {}ms market {}ms "
                                  "broker {}ms total {}ms) ---------------- \033[1;0m",
                                  100 - max_number_of_trading_rounds, offer_tp.stop(), market_tp.stop(),
                                  broker_tp.stop(), optimizer_start.stop());
    }

    std::vector<types::energy::EnforcedLimits> optimized_values;
    optimized_values.reserve(brokers.size());

    for (auto& broker : brokers) {
        auto& local_market = broker->get_local_market();
        const auto& sold_energy = local_market.get_sold_energy();

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

    return optimized_values;
}

} // namespace module
