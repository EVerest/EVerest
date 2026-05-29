// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_SIMULATOR_HPP
#define EV_SIMULATOR_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 2
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/ev_manager/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/ISO15118_ev/Interface.hpp>
#include <generated/interfaces/ev_board_support/Interface.hpp>
#include <generated/interfaces/ev_slac/Interface.hpp>
#include <generated/interfaces/kvs/Interface.hpp>

// ev@77e6c781-9137-44ed-847f-8c1c40e8e67b:v1
// insert your custom include headers here
#include <everest_api_types/utilities/CommCheckHandler.hpp>
#include <everest_api_types/utilities/Topics.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace ev_API = everest::lib::API;
// ev@77e6c781-9137-44ed-847f-8c1c40e8e67b:v1

namespace module {

// Forward declarations — full definitions live in main/EvSimRuntime.hpp /
// main/FsmContext.hpp. Keeping them out of this header keeps the ev-cli
// regeneration target free of runtime-internal types.
class EvSimRuntime;
class FsmContext;

struct Conf {
    int connector_id;
    double ac_nominal_voltage;
    double max_current_a;
    bool three_phases;
    int dc_max_current_limit;
    int dc_max_power_limit;
    int dc_max_voltage_limit;
    int dc_energy_capacity;
    int dc_target_current;
    int dc_target_voltage;
    int soc_initial_pct;
    int departure_time_s;
    int e_amount_wh;
    bool force_payment_option;
    bool keep_cross_boot_plugin_state;
    bool publish_bsp_measurements;
    int tick_interval_ms;
    std::string on_battery_full;
    double battery_full_threshold_pct;
    int cfg_communication_check_to_s;
    int cfg_heartbeat_interval_ms;
    bool enabled_at_startup;
};

class EvSimulator : public Everest::ModuleBase {
public:
    EvSimulator() = delete;
    EvSimulator(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                std::unique_ptr<ev_managerImplBase> p_ev_manager,
                std::unique_ptr<ev_board_supportIntf> r_ev_board_support,
                std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev, std::vector<std::unique_ptr<ev_slacIntf>> r_slac,
                std::vector<std::unique_ptr<kvsIntf>> r_kvs, Conf& config);

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support;
    const std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::vector<std::unique_ptr<kvsIntf>> r_kvs;
    const Conf& config;

    ~EvSimulator();

    // ev@c569b343-4c14-46db-9a11-2cf8364d0412:v1
    // insert your public definitions here
    const ev_API::Topics& get_topics() const {
        return topics;
    }

    ev_API::CommCheckHandler<ev_managerImplBase> comm_check{"generic/CommunicationFault",
                                                            "Bridge to implementation connection lost", p_ev_manager};
    // ev@c569b343-4c14-46db-9a11-2cf8364d0412:v1

protected:
    // ev@e7767f15-009e-4c62-ab6d-63188f976337:v1
    // insert your protected definitions here
    // ev@e7767f15-009e-4c62-ab6d-63188f976337:v1

private:
    friend class LdEverest;
    void init();
    void ready();

    // ev@7491c19c-2153-4b63-9fec-fae14e11ccde:v1
    // insert your private definitions here
    void setup_heartbeat_generator();

    ev_API::Topics topics;
    size_t hb_id{0};
    std::unique_ptr<EvSimRuntime> runtime;
    std::thread loop_thread;
    std::atomic_bool loop_online{true};
    // ev@7491c19c-2153-4b63-9fec-fae14e11ccde:v1
};

// ev@b7820927-ca8a-44d6-b0aa-ce59d6b3dff4:v1
// insert other definitions here
// ev@b7820927-ca8a-44d6-b0aa-ce59d6b3dff4:v1

} // namespace module

#endif // EV_SIMULATOR_HPP
