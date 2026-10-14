// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_CONTROLLER_HPP
#define EV_CONTROLLER_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/ev_manager/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/ISO15118_ev/Interface.hpp>
#include <generated/interfaces/ev_board_support/Interface.hpp>
#include <generated/interfaces/ev_slac/Interface.hpp>
#include <generated/interfaces/kvs/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
// insert your custom include headers here
#include <everest_api_types/ev_controller/API.hpp>
#include <everest_api_types/utilities/CommCheckHandler.hpp>
#include <everest_api_types/utilities/Topics.hpp>

#include <atomic>
#include <memory>
#include <thread>

namespace ev_API = everest::lib::API;

namespace module {
// Forward declarations: full definitions live in main/EvCtlRuntime.hpp /
// main/FsmContext.hpp. Keeping them out of this header keeps the ev-cli
// regeneration target free of runtime-internal types.
class EvCtlRuntime;
class FsmContext;
} // namespace module
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string default_session;
    int iso_pwm_wait_s;
    int connector_id;
    double ac_nominal_voltage;
    double max_current_a;
    bool three_phases;
    int dc_max_current_limit;
    int dc_max_power_limit;
    int dc_max_voltage_limit;
    int dc_min_voltage_limit;
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

class EvController : public Everest::ModuleBase {
public:
    EvController() = delete;
    EvController(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                 std::unique_ptr<ev_managerImplBase> p_ev_manager,
                 std::unique_ptr<ev_board_supportIntf> r_ev_board_support,
                 std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev, std::vector<std::unique_ptr<ev_slacIntf>> r_slac,
                 std::vector<std::unique_ptr<kvsIntf>> r_kvs, Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_ev_manager(std::move(p_ev_manager)),
        r_ev_board_support(std::move(r_ev_board_support)),
        r_ev(std::move(r_ev)),
        r_slac(std::move(r_slac)),
        r_kvs(std::move(r_kvs)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<ev_managerImplBase> p_ev_manager;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support;
    const std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev;
    const std::vector<std::unique_ptr<ev_slacIntf>> r_slac;
    const std::vector<std::unique_ptr<kvsIntf>> r_kvs;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    // insert your public definitions here
    ~EvController();
    std::optional<everest::lib::API::V1_0::types::ev_controller::SessionConfigParams> m_default_session;

    const ev_API::Topics& get_topics() const {
        return topics;
    }

    ev_API::CommCheckHandler<ev_managerImplBase> comm_check{"generic/CommunicationFault",
                                                            "Bridge to implementation connection lost", p_ev_manager};
    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1

protected:
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1
    // insert your protected definitions here
    // ev@4714b2ab-a24f-4b95-ab81-36439e1478de:v1

private:
    friend class LdEverest;
    void init();
    void ready();
    void shutdown();

    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
    // insert your private definitions here
    void setup_heartbeat_generator();
    std::string m_default_session_error;

    ev_API::Topics topics;
    size_t hb_id{0};
    struct RuntimeDeleter {
        void operator()(EvCtlRuntime* runtime) const;
    };
    std::unique_ptr<EvCtlRuntime, RuntimeDeleter> runtime;
    std::thread loop_thread;
    std::atomic_bool loop_online{true};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EV_CONTROLLER_HPP
