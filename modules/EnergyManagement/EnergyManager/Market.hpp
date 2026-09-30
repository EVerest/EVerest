// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef MARKET_HPP
#define MARKET_HPP

// headers for required interface implementations
#include <array>
#include <generated/interfaces/energy/Interface.hpp>
#include <optional>
#include <set>
#include <utils/date.hpp>
#include <vector>

using namespace std::chrono_literals;

namespace module {

typedef std::vector<types::energy::ScheduleReqEntry> ScheduleReq;
typedef std::vector<types::energy::ScheduleResEntry> ScheduleRes;
typedef std::vector<types::energy::ScheduleSetpointEntry> ScheduleSetpoints;

enum class Phase {
    L1,
    L2,
    L3,
};

using PhaseSet = std::set<Phase>;

/// \brief What a connector counts as drawing on while nothing says otherwise.
inline const PhaseSet ALL_GRID_PHASES{Phase::L1, Phase::L2, Phase::L3};

/// \brief The current limit of \p limits on \p phase: the lower of ac_max_current_A and the
/// phase's entry in ac_max_current_per_phase_A. Empty when neither limits the phase.
std::optional<types::energy::NumberWithSource> phase_limit_A(const types::energy::LimitsReq& limits, Phase phase);

class globals_t {
public:
    void init(date::utc_clock::time_point _start_time, int _interval_duration, int _schedule_duration,
              float _slice_ampere, float _slice_watt, bool _debug,
              const types::energy::EnergyFlowRequest& energy_flow_request);
    date::utc_clock::time_point start_time; // common start point
    std::chrono::minutes interval_duration; // interval duration
    int schedule_length;                    // total forcast length (in counts of (non-regular) intervals)
    float slice_ampere;                     // ampere_slices for trades
    float slice_watt;                       // ampere_slices for trades
    bool debug{false};
    int active_slot{0}; // slot of every schedule that covers start_time
    ScheduleReq zero_schedule_req, empty_schedule_req;
    ScheduleRes zero_schedule_res, empty_schedule_res;
    ScheduleSetpoints empty_schedule_setpoints;

private:
    void create_timestamps(const types::energy::EnergyFlowRequest& energy_flow_request);
    void add_timestamps(const types::energy::EnergyFlowRequest& energy_flow_request);
    template <typename T> void create_empty_schedule(T& s);
    std::vector<date::utc_clock::time_point> timestamps;
};

extern globals_t globals;

class time_probe {
public:
    void start();
    void pause();
    int stop();

private:
    std::chrono::high_resolution_clock::time_point timepoint_start;
    std::chrono::nanoseconds total_duration{0};
    bool running{false};
};

class Market {
public:
    Market(const types::energy::EnergyFlowRequest& _energy_flow_request, const float __nominal_ac_voltage,
           Market* __parent = nullptr);

    /// \brief Books a trade here and on the path to the root, the current on \p phases only,
    /// the grid phases the connector draws on. The watts stay as traded here, where they are
    /// what the connector is sent, but count above for \p phases only: a single phase EV on a
    /// three phase connector draws on one phase what the connector converts for three.
    void trade(const ScheduleRes& s, const PhaseSet& phases = ALL_GRID_PHASES);

    bool is_root();

    void get_list_of_evses(std::vector<Market*>& list);
    std::vector<Market*> get_list_of_evses();
    /// \brief What is left to trade here for a connector drawing on \p phases: on the
    /// tightest of those phases, its ampere limit less what is sold on it.
    ScheduleReq get_available_energy_import(const PhaseSet& phases = ALL_GRID_PHASES);
    ScheduleReq get_available_energy_export(const PhaseSet& phases = ALL_GRID_PHASES);
    ScheduleSetpoints get_setpoints() {
        return setpoints;
    };

    const ScheduleRes& get_sold_energy() const;

    /// \brief The import offer this node trades against: the request schedule resampled onto
    /// the optimizer's timestamps, with both sides' limits merged and efficiency applied.
    /// Read limits here, not from energy_flow_request.schedule_import.
    const ScheduleReq& get_import_max_available() const {
        return import_max_available;
    };

    Market* parent();

    float nominal_ac_voltage();

    // local request only for this node
    const types::energy::EnergyFlowRequest& energy_flow_request;

private:
    Market* _parent;
    std::list<Market> _children;
    float _nominal_ac_voltage;

    // main data structures
    ScheduleReq import_max_available, export_max_available;
    ScheduleSetpoints setpoints;
    ScheduleRes sold_root;
    // Current sold through this node per slot and grid phase [A], signed like sold_root.
    std::vector<std::array<float, 3>> sold_phase_A;
    // Power sold through this node per slot on the phases actually drawn [W], signed like
    // sold_root. sold_root keeps the connector's declared phases, which is what its enforced
    // limit converts back with; availability is judged on this.
    std::vector<float> m_sold_drawn_W;
    std::vector<ScheduleRes> sold_leaves;

    ScheduleReq get_max_available_energy(const ScheduleReq& request);
    ScheduleReq get_available_energy(const ScheduleReq& available, bool add_sold, const PhaseSet& phases);
    void book(const ScheduleRes& traded, const ScheduleRes& drawn, const PhaseSet& phases);
    ScheduleSetpoints resample(const ScheduleSetpoints& request);
};

/// \brief Index of the schedule slot in force at globals.start_time: the last slot that has
/// started, including one starting exactly then.
///
/// A schedule that starts in the future reports its first slot. An empty schedule has no
/// slot and reports std::nullopt.
std::optional<ScheduleReq::size_type> active_slot_index(const ScheduleReq& schedule);

float get_watt_from_freq_table(const std::vector<types::energy::FrequencyWattPoint>& table, float freq);
void apply_limit_if_smaller(std::optional<types::energy::NumberWithSource>& base, float limit,
                            const std::string& source);
void apply_setpoints(ScheduleReq& imp, ScheduleReq& exp, const ScheduleSetpoints& setpoints, std::optional<float> freq);

} // namespace module

#endif // MARKET_HPP
