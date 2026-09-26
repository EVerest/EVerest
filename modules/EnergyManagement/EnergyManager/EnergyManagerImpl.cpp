// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <EnergyManagerImpl.hpp>

#include <algorithm>
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
    // Default of the manifest option. An unknown value must not break energy distribution,
    // but it must not pass unnoticed either: the manifest enum rejects a typo, a config
    // built any other way does not.
    if (s != "FastCharging") {
        EVLOG_warning << "Unknown broker_strategy '" << s << "', falling back to FastCharging";
    }
    return BrokerStrategy::FastCharging;
}

// Creates the broker that trades on behalf of one EVSE. This is the single place that maps
// the configured strategy to a broker class.
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

static BrokerFastCharging::EnergyManagerConfig to_broker_fast_charging_config(const EnergyManagerConfig& config) {
    BrokerFastCharging::EnergyManagerConfig broker_conf;

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

void EnergyManagerImpl::warn_about_unparsable_meters(const std::vector<std::string>& unparsable) {
    // Warn once per meter, not once per optimizer cycle: a permanently broken meter would
    // otherwise produce a warning every second, around the clock. A meter that starts
    // delivering usable timestamps again is allowed to warn a second time later.
    const std::set<std::string> current(unparsable.begin(), unparsable.end());

    for (const auto& uuid : current) {
        if (warned_unparsable_meters.insert(uuid).second) {
            EVLOG_warning << "cannot parse the power meter timestamp of meter " << uuid
                          << ", treating its readings as stale until it recovers";
        }
    }

    for (auto it = warned_unparsable_meters.begin(); it != warned_unparsable_meters.end();) {
        it = current.count(*it) == 0 ? warned_unparsable_meters.erase(it) : std::next(it);
    }
}

#ifdef BUILD_TESTING_MODULE_ENERGY_MANAGER
PowerMeterAggregator::AggregateResult EnergyManagerImpl::get_site_aggregate() const {
    std::scoped_lock lock(energy_mutex);
    return site_aggregate;
}
#endif

EnergyManagerImpl::~EnergyManagerImpl() {
    stop();
}

#ifdef BUILD_TESTING_MODULE_ENERGY_MANAGER
RedistributionInference EnergyManagerImpl::get_redistribution_inference() const {
    std::scoped_lock lock(energy_mutex);
    return redistribution_inference;
}
#endif

namespace {

std::string format_W(const std::optional<float>& value) {
    return value.has_value() ? fmt::format("{:.0f} W", value.value()) : std::string("n/a");
}

} // namespace

void EnergyManagerImpl::infer_redistribution(const Market& market, const std::vector<std::shared_ptr<Broker>>& brokers,
                                             const std::vector<types::energy::EnforcedLimits>& limits) {
    const auto nominal_ac_voltage = static_cast<float>(config.nominal_ac_voltage);
    const auto connector_margin = static_cast<float>(config.power_redistribution_connector_margin);
    const auto site_margin = static_cast<float>(config.power_redistribution_site_margin);
    const auto gain = static_cast<float>(config.power_redistribution_gain);
    const auto hold_time = std::chrono::seconds(config.power_redistribution_hold_time_s);
    const auto aggregation_window = std::chrono::seconds(config.power_meter_aggregation_window_s);
    const auto now = globals.start_time;

    RedistributionInference inference;
    std::vector<SaturatedConnector> saturated;

    for (const auto& broker : brokers) {
        const auto& connector_market = broker->get_local_market();
        const auto& node = connector_market.energy_flow_request;
        // The broker loop above created an entry for every connector; at() rather than
        // operator[] so a future reordering fails loudly instead of quietly inferring on a
        // default constructed context.
        auto& ctx = contexts.at(node.uuid);
        const auto bounds = get_static_bounds_W(connector_market, nominal_ac_voltage);

        // The measurement observed this run is the EV's response to what the previous run
        // allotted, so those two are the pair to compare - but only while it is a live
        // reading. The same freshness rule the site aggregate applies holds here: a meter
        // that stopped publishing keeps reporting its last value in every request, and
        // without this check a five minute old reading reads as a connector that could give
        // power back. No measurement at all yields Unknown, which is what an unusable one
        // deserves too.
        std::optional<float> measured_W;
        const auto& observed = ctx.last_observed_measurement;
        if (observed.power_W.has_value() and is_fresh(observed.measured_at, now, aggregation_window)) {
            measured_W = observed.power_W.value().total;
        }
        auto connector = classify_connector(ctx.last_allocated_W, measured_W, bounds, connector_margin);

        const auto edge =
            ctx.under_consuming.update(connector.connector_class == ConnectorClass::UnderConsuming, now, hold_time);
        connector.held = ctx.under_consuming.held();

        if (edge == HoldLatch::Edge::Held) {
            EVLOG_info << fmt::format("{}: power can be reduced by {:.0f} W (allotted {}, measured {})", node.uuid,
                                      connector.reducible_W, format_W(connector.allocated_W),
                                      format_W(connector.measured_W));
        } else if (edge == HoldLatch::Edge::Released) {
            EVLOG_info << fmt::format("{}: power can no longer be reduced (allotted {}, measured {})", node.uuid,
                                      format_W(connector.allocated_W), format_W(connector.measured_W));
        }

        if (connector.connector_class == ConnectorClass::Saturated) {
            // Without an allocation or a static maximum there is nothing to clamp an
            // increase against, so such a connector is not a candidate - and must not be
            // counted among them either, or it would shrink the others' share.
            if (const auto candidate = to_saturated_connector(node.uuid, connector, bounds)) {
                saturated.push_back(candidate.value());
            }
        }

        // Remember this run's allocation for the next run's comparison. Only sessions count:
        // what an unplugged connector is allotted has no consumption to compare against, and
        // it must not leak into the first run of the next session as a false gap.
        ctx.last_allocated_W.reset();
        if (in_session(node)) {
            const auto limit =
                std::find_if(limits.begin(), limits.end(), [&node](const auto& l) { return l.uuid == node.uuid; });
            if (limit != limits.end()) {
                ctx.last_allocated_W = get_allocated_power_W(*limit, nominal_ac_voltage);
            }
        }

        inference.connectors[node.uuid] = connector;
    }

    auto site = infer_site(get_grid_limit_W(market, nominal_ac_voltage), site_aggregate, saturated, site_margin, gain);
    site.meter_source = site_meter_source;

    const auto site_edge = site_headroom.update(site.increase_W > 0.f, now, hold_time);
    site.held = site_headroom.held();

    const int granted = grant_site_headroom(site);

    if (site_edge == HoldLatch::Edge::Held) {
        EVLOG_info << fmt::format(
            "granting {:.0f} W of headroom to {} of {} saturated connector(s) (grid limit {}, measured {} from {})",
            site.increase_W, granted, site.saturated_connectors, format_W(site.grid_limit_W), format_W(site.measured_W),
            to_string(site.meter_source));
    } else if (site_edge == HoldLatch::Edge::Released) {
        EVLOG_info << fmt::format("power can no longer be increased (grid limit {}, measured {}, headroom {})",
                                  format_W(site.grid_limit_W), format_W(site.measured_W), format_W(site.headroom_W));
    }

    if (globals.debug) {
        EVLOG_info << fmt::format("Redistribution: grid limit {}, measured {} ({}), headroom {}, {} saturated, "
                                  "proposed increase {:.0f} W{}",
                                  format_W(site.grid_limit_W), format_W(site.measured_W), to_string(site.meter_source),
                                  format_W(site.headroom_W), site.saturated_connectors, site.increase_W,
                                  site.held ? " (held)" : "");
        for (const auto& [uuid, connector] : inference.connectors) {
            const auto& distributed = contexts.at(uuid).distributed_power_W;
            EVLOG_info << fmt::format(
                "  {}: {} allotted {}, measured {}, reducible {:.0f} W{}{}", uuid, to_string(connector.connector_class),
                format_W(connector.allocated_W), format_W(connector.measured_W), connector.reducible_W,
                connector.held ? " (held)" : "",
                distributed.has_value() ? fmt::format(", granted {:.0f} W", distributed.value()) : "");
        }
    }

    inference.site = site;
    redistribution_inference = inference;
}

int EnergyManagerImpl::grant_site_headroom(const SiteInference& site) {
    // The hold is what keeps a single optimizer cycle of headroom - an EV between two ramp
    // steps, a load that switched off for a second - from moving an allocation.
    const bool granting = site.held;

    // Every context, not only the connectors of this run: contexts outlive a run, and a
    // connector that has left the tree must not keep a grant that nothing will clear.
    int granted = 0;
    for (auto& [uuid, context] : contexts) {
        context.distributed_power_W.reset();
        if (not granting) {
            continue;
        }
        const auto share = site.increase_W_by_connector.find(uuid);
        if (share != site.increase_W_by_connector.end()) {
            context.distributed_power_W = share->second;
            granted++;
        }
    }
    return granted;
}

void EnergyManagerImpl::start() {
    {
        std::lock_guard<std::mutex> lock(mainloop_sleep_mutex);
        if (running) {
            return;
        }
        running = true;
    }

    // start thread to update energy optimization
    mainloop = std::thread([this] {
        while (running) {
            auto optimized_values = this->run_optimizer(energy_flow_request, date::utc_clock::now());
            enforced_limits_callback(optimized_values);
            {
                std::unique_lock<std::mutex> lock(mainloop_sleep_mutex);
                // Both reasons to wake early, under the lock that guards them. stop() and
                // on_energy_flow_request() set their flag while holding this same mutex, so
                // neither change can land between this predicate and the wait; without both
                // halves the notification is lost in that window.
                //
                // Both have to be named here: a predicated wait_for re-sleeps on every
                // notification its predicate does not cover, so a predicate that mentions
                // only the stop flag swallows the priority request wake-up and delays the
                // optimizer run it asks for by a full update_interval.
                mainloop_sleep_condvar.wait_for(lock, std::chrono::seconds(config.update_interval),
                                                [this] { return not running or wakeup; });
                wakeup = false;
            }
        }
    });
}

void EnergyManagerImpl::stop() {
    {
        // Under the same mutex the worker waits on: clearing the flag outside it leaves a
        // window where the worker has already tested the predicate but is not yet
        // registered on the condition variable, and the notification below is lost.
        std::lock_guard<std::mutex> lock(mainloop_sleep_mutex);
        if (not running) {
            return;
        }
        running = false;
    }

    mainloop_sleep_condvar.notify_all();
    if (mainloop.joinable()) {
        mainloop.join();
    }
}

void EnergyManagerImpl::on_energy_flow_request(const types::energy::EnergyFlowRequest& e) {
    // Received new energy object from a child.
    std::scoped_lock lock(energy_mutex);
    energy_flow_request = e;

    if (is_priority_request(e)) {
        // Trigger optimization now. The flag is set under the mutex the worker waits on,
        // for the same reason stop() clears running under it: notifying without it leaves a
        // window in which the worker has tested the predicate but is not yet registered on
        // the condition variable, and the request waits out the whole update_interval.
        {
            std::lock_guard<std::mutex> sleep_lock(mainloop_sleep_mutex);
            wakeup = true;
        }
        mainloop_sleep_condvar.notify_all();
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

    // Refresh the site measurement for this run. The aggregator is built from the tree each
    // time, so a meter that disappeared from it stops contributing without anything having
    // to remember to drop it.
    PowerMeterAggregator site_aggregator(std::chrono::seconds(config.power_meter_aggregation_window_s));
    site_meter_source = collect_site_measurement(request, site_aggregator);
    site_aggregate = site_aggregator.aggregate(globals.start_time);
    warn_about_unparsable_meters(site_aggregate.unparsable_meters);

    time_probe optimizer_start;
    optimizer_start.start();
    if (globals.debug)
        EVLOG_info << "\033[1;44m---------------- Run energy optimizer ---------------- \033[1;0m";

    if (globals.debug) {
        // Spell out the absence of a total rather than printing a zero that no meter reported.
        const auto power = site_aggregate.power_W.has_value() ? fmt::format("{}W", site_aggregate.power_W.value().total)
                                                              : std::string("no reading");
        EVLOG_info << fmt::format("Site power: {} from {} ({} meter(s), {} stale)", power, to_string(site_meter_source),
                                  site_aggregate.fresh_meters, site_aggregate.stale_meters);
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

        brokers.push_back(make_broker(broker_strategy, *m, contexts[m->energy_flow_request.uuid],
                                      to_broker_fast_charging_config(config)));
        // Read the connector state this run trades against, before the first trading round.
        // Explicit rather than a constructor side effect: a broker is built once per EVSE per
        // run in this loop, and a reader should not have to know that constructing one
        // mutates the session context.
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
            const auto* selected_schedule = &sold_energy.front();

            for (const auto& s : sold_energy) {
                const auto schedule_time = Everest::Date::from_rfc3339(s.timestamp);
                if (globals.start_time < schedule_time) {
                    // all further schedules will be further into the future
                    break;
                } else {
                    // use this schedule as the starting point
                    selected_schedule = &s;
                }
            }

            l.limits_root_side = selected_schedule->limits_to_root;

            if (globals.debug) {
                EVLOG_info << "Sending enforced limits (import) to :" << l.uuid << " " << l.limits_root_side;
            }

            optimized_values.push_back(std::move(l));
        }
    }

    if (broker_strategy == BrokerStrategy::PowerRedistribution) {
        infer_redistribution(market, brokers, optimized_values);
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
