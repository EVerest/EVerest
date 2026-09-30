// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef BROKER_HPP
#define BROKER_HPP

#include <chrono>
#include <optional>

#include "Market.hpp"
#include "Offer.hpp"

namespace module {

enum class SlotType {
    Import,
    Export,
    Undecided
};

// False for Unplugged and Finished. Nodes without a state (non-EVSE nodes) count as in session.
inline bool in_session(const types::energy::EnergyFlowRequest& node) {
    return not node.evse_state.has_value() or (node.evse_state.value() != types::energy::EvseState::Unplugged and
                                               node.evse_state.value() != types::energy::EvseState::Finished);
}

// Power meter reading last observed for one connector. Values the meter does not report
// are nullopt, never zero.
struct ObservedMeasurement {
    std::optional<types::units::Power> power_W;
    types::units::Current current_A;
    // The reading's own timestamp: EnergyNode and EvseManager republish the last reading, so
    // only this tells a frozen meter from a steady one. nullopt when unparsable.
    std::optional<date::utc_clock::time_point> measured_at;
};

// Current [A] per phase. nullopt is unknown, never zero, and constrains nothing.
struct PhaseCurrents {
    std::optional<float> L1;
    std::optional<float> L2;
    std::optional<float> L3;
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
        redistribution_warned_no_measurement = false;
        last_observed_measurement = {};
        redistribution_cap_A = std::nullopt;
        redistribution_reduction_pending_since = std::nullopt;
    };

    int number_1ph3ph_cycles;
    int last_ac_number_of_active_phases_import;
    std::chrono::time_point<date::utc_clock> ts_1ph_optimal;

    // Warn about a missing measurement once per session, not once per run.
    bool redistribution_warned_no_measurement;
    ObservedMeasurement last_observed_measurement;
    // nullopt while the connector is not capped.
    std::optional<PhaseCurrents> redistribution_cap_A;
    // When the candidate cap first fell below the applied one.
    std::optional<date::utc_clock::time_point> redistribution_reduction_pending_since;
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

    // PowerRedistribution strategy, see the redistribution_* options in manifest.yaml.
    struct RedistributionConfig {
        float margin_A{2.0f};
        bool start_with_lower_limit{true};
        std::chrono::seconds reduction_hold{30};
        std::chrono::seconds measurement_max_age{10};
    };

    struct EnergyManagerConfig {
        Switch1ph3phMode switch_1ph_3ph_mode{Switch1ph3phMode::Never};
        StickyNess stickyness{StickyNess::DontChange};
        int max_nr_of_switches_per_session{0};
        int power_hysteresis_W{200};
        int time_hysteresis_s{600};
        RedistributionConfig redistribution;
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

    // Called once per optimizer run, before the first trading round.
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

    bool time_slot_active(const int i);

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
