// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef BROKER_HPP
#define BROKER_HPP

#include <optional>

#include "Market.hpp"
#include "Offer.hpp"

namespace module {

enum class SlotType {
    Import,
    Export,
    Undecided
};

// True while a connector has a session worth observing. Unplugged and Finished are the
// two states with no consumption to compare an allocation against; a node that declares no
// state at all (the non-EVSE nodes of the tree) is not excluded. Shared by every site that
// needs this test so the three of them cannot drift apart - one of them used to spell the
// negation by hand.
inline bool in_session(const types::energy::EnergyFlowRequest& node) {
    return not node.evse_state.has_value() or (node.evse_state.value() != types::energy::EvseState::Unplugged and
                                               node.evse_state.value() != types::energy::EvseState::Finished);
}

// Snapshot of the power meter reading last observed by the power redistribution broker
// for one connector, refreshed on every optimizer run during an active session.
// Values the meter does not report are nullopt, never zero (a single-phase meter reports
// only current_A.L1). Per-phase current is what per-phase trading and asymmetry limits
// are expressed in, so it is kept per phase rather than collapsed to a total.
struct ObservedMeasurement {
    // Imported power [W] (types::units::Power): total plus optional per-phase L1/L2/L3.
    // nullopt while the meter reports no power at all.
    std::optional<types::units::Power> power_W;

    // Per-phase current [A] with named L1/L2/L3 properties (types::units::Current).
    types::units::Current current_A;

    // The reading's own measurement timestamp, carried so a consumer can tell a live
    // reading from one the meter stopped refreshing. This matters because absence and
    // staleness fail differently: a meter that stops publishing clears power_W on the
    // next run, but EnergyNode and EvseManager keep re-publishing the last Powermeter
    // they received, so a dead meter looks exactly like a live one holding steady.
    // Without this field that is indistinguishable, and every consumer of power_W would
    // have to trust an age it cannot see. nullopt when the meter reports no usable
    // timestamp, which must be treated like a missing measurement, not like a fresh one.
    std::optional<date::utc_clock::time_point> measured_at;
};

// All context data that is stored in between optimization runs
struct BrokerContext {
    BrokerContext() {
        clear();
    };

    void clear() {
        number_1ph3ph_cycles = 0;
        last_ac_number_of_active_phases_import = 0;
        ts_1ph_optimal = date::utc_clock::now();
        tracking_warned_no_measurement = false;
        last_observed_measurement = {};
    };

    int number_1ph3ph_cycles;
    int last_ac_number_of_active_phases_import;
    std::chrono::time_point<date::utc_clock> ts_1ph_optimal;

    // True once the missing-measurement warning has been logged for this session, so a
    // meterless connector warns once instead of once per optimizer run.
    bool tracking_warned_no_measurement;

    // Reading last observed by the power redistribution broker for this connector.
    // Empty (all nullopt) while no measurement is available. Reset by clear() on unplug.
    ObservedMeasurement last_observed_measurement;
};

// base class for different Brokers
class Broker {
public:
    // Enums and config for 3ph switching
    // Check manifest.yaml of this module for description
    enum class Switch1ph3phMode {
        Never,
        Oneway,
        Both,
    };

    enum class StickyNess {
        SinglePhase,
        ThreePhase,
        DontChange,
    };

    struct EnergyManagerConfig {
        Switch1ph3phMode switch_1ph_3ph_mode{Switch1ph3phMode::Never};
        StickyNess stickyness{StickyNess::DontChange};
        int max_nr_of_switches_per_session{0};
        int power_hysteresis_W{200};
        int time_hysteresis_s{600};
    };

    Broker(Market& market, BrokerContext& context, EnergyManagerConfig config);
    virtual ~Broker(){};

    // Asks this broker to trade based on the given offer.
    // The broker will decide how much / if it wants to trade and
    // execute the trade directly on its local market (which was passed in the constructor)
    // This function is called from the optimization loop whenever a new offer is available for this
    // broker.
    bool trade(Offer& offer);

    // Actual implementation of the trading algorithm. This function must be overriden by the
    // specific implementation class. It will be called from the trade() function of the base class.
    virtual void tradeImpl() = 0;

    // Reads whatever this broker wants to know about the current state of its connector,
    // before any trading round runs. Called exactly once per optimizer run, from the same
    // loop that creates the brokers. Trading must not depend on it: the default does
    // nothing, and a strategy that only trades never overrides it.
    virtual void observe() {
    }

    Market& get_local_market();

protected:
    void buy_ampere_unchecked(int index, types::energy::NumberWithSource ampere,
                              types::energy::IntegerWithSource number_of_phases);
    void buy_watt_unchecked(int index, types::energy::NumberWithSource watt);

    bool buy_ampere_import(int index, float ampere, bool allow_less, types::energy::IntegerWithSource number_of_phases);
    bool buy_ampere_export(int index, float ampere, bool allow_less, types::energy::IntegerWithSource number_of_phases);
    bool buy_ampere(const types::energy::ScheduleReqEntry& _offer, int index, float ampere, bool allow_less,
                    bool import, types::energy::IntegerWithSource number_of_phases);

    bool buy_watt_import(int index, float watt, bool allow_less);
    bool buy_watt_export(int index, float watt, bool allow_less);
    bool buy_watt(const types::energy::ScheduleReqEntry& _offer, int index, float watt, bool allow_less, bool import);

    date::utc_clock::time_point to_timestamp(const types::energy::ScheduleReqEntry& entry);
    bool time_slot_active(const int i, const ScheduleReq& offer);

    // reference to local market at the broker's node
    Market& local_market;
    std::vector<bool> first_trade;
    std::vector<SlotType> slot_type;
    std::vector<types::energy::IntegerWithSource> num_phases;
    Offer* offer{nullptr};
    BrokerContext& context;

    ScheduleRes trading;

    bool traded{false};

    EnergyManagerConfig config;
};

} // namespace module

#endif // BROKER_HPP
