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

/// \brief Tracks how long a condition has held, and reports each edge exactly once per
/// stretch. Time is passed in (the optimizer's start_time), not read from the clock.
class HoldLatch {
public:
    /// What update() wants said about this run, at most once per stretch.
    enum class Edge {
        None,     ///< nothing to report
        Held,     ///< the condition has now held long enough; report it
        Released, ///< a condition that was reported has stopped holding; report that
    };

    Edge update(bool condition, date::utc_clock::time_point now, std::chrono::seconds hold_time) {
        if (not condition) {
            m_since.reset();
            m_condition_held = false;
            if (not m_reported) {
                return Edge::None;
            }
            m_reported = false;
            return Edge::Released;
        }

        if (not m_since.has_value()) {
            m_since = now;
        }
        m_condition_held = now - m_since.value() >= hold_time;

        if (not m_condition_held or m_reported) {
            return Edge::None;
        }
        m_reported = true;
        return Edge::Held;
    }

    /// \brief Whether the condition has held for the full hold time, as of the last update().
    bool held() const {
        return m_condition_held;
    }

    /// \brief Forgets the stretch in progress without reporting a release. For the end of a
    /// session, where there is no longer a condition to have stopped holding.
    void reset() {
        m_since.reset();
        m_reported = false;
        m_condition_held = false;
    }

private:
    // start_time of the run since which the condition has held continuously.
    std::optional<date::utc_clock::time_point> m_since;
    bool m_reported{false};
    bool m_condition_held{false};
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
        distributed_power_W = std::nullopt;
        last_allocated_W.reset();
        last_margin_W = 0.f;
        under_consuming.reset();
        phase_imbalance_cap_A = std::nullopt;
        phase_imbalance_cap_since = std::nullopt;
        phase_imbalance_arrived_at = std::nullopt;
        phases_in_use = ALL_GRID_PHASES;
        phases_narrower_since = std::nullopt;
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

    // Extra import power [W] the site inference granted this connector above measured plus
    // margin, for the broker of the next run. nullopt while there is nothing to grant.
    std::optional<float> distributed_power_W;

    // Import power [W] the previous optimizer run handed to this connector: the "allotted"
    // side of the power redistribution inference, compared against the measurement of the
    // following run. nullopt before the first run of a session and while not in a session.
    std::optional<float> last_allocated_W;

    // The part of last_allocated_W that is the cap's own margin above the measurement it
    // was computed from, on the phases that allocation was expressed in. Paired with
    // last_allocated_W because the classification has to know how much of the gap it is
    // looking at the broker put there itself.
    float last_margin_W{0.f};

    // How long this connector has continuously consumed less than allotted, and whether
    // that has already been reported for the current stretch.
    HoldLatch under_consuming;

    // Cap the phase imbalance limiting holds this connector at [A], 0 while it is paused,
    // the run that last changed it (the hold counts from there), and the run its session
    // was first seen in (the newest is paused first). Empty while the connector is not
    // being limited. Written by EnergyManagerImpl, applied by BrokerPowerRedistribution as
    // one more upper bound. Reset by clear() on unplug.
    std::optional<float> phase_imbalance_cap_A;
    std::optional<date::utc_clock::time_point> phase_imbalance_cap_since;
    std::optional<date::utc_clock::time_point> phase_imbalance_arrived_at;

    // Grid phases the connector's trades count on, from its own per phase measurement.
    // All three while it draws nothing or has no usable reading, since it may then start on
    // any. A phase is added the run it is drawn on, and dropped only once the narrower set
    // has held for the reduction hold (the run it first did is phases_narrower_since): an EV
    // ramping up may not yet draw on every phase it will use. Reset by clear() on unplug.
    PhaseSet phases_in_use{ALL_GRID_PHASES};
    std::optional<date::utc_clock::time_point> phases_narrower_since;
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
        std::chrono::seconds reduction_hold{10};
        // power_meter_aggregation_window_s: the module has one staleness rule, and a meter
        // that is stale for the site aggregate is stale for this connector's limit too.
        std::chrono::seconds measurement_max_age{5};
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

    // Grid phases this broker's trades count on in the market. All three unless the
    // strategy knows better.
    virtual PhaseSet trading_phases() const {
        return ALL_GRID_PHASES;
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
