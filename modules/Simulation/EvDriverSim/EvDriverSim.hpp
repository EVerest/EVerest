// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef EV_DRIVER_SIM_HPP
#define EV_DRIVER_SIM_HPP

//
// AUTO GENERATED - MARKED REGIONS WILL BE KEPT
// template version 3
//

#include "ld-ev.hpp"

// headers for provided interface implementations
#include <generated/interfaces/ev_board_support/Implementation.hpp>

// headers for required interface implementations
#include <generated/interfaces/ev_board_support/Interface.hpp>

// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1
#include "main/PilotGate.hpp"
#include "main/ScenarioRunner.hpp"
#include "main/WorldFaults.hpp"
#include <everest/util/async/monitor.hpp>
#include <everest_api_types/utilities/CommCheckHandler.hpp>
#include <everest_api_types/utilities/Topics.hpp>
#include <thread>
// ev@4bf81b14-a215-475c-a1d3-0a484ae48918:v1

namespace module {

struct Conf {
    std::string ev_controller_module_id;
    bool plugged_at_startup;
    int heartbeat_wait_s;
    int cfg_heartbeat_interval_ms;
    int cfg_communication_check_to_s;
};

class EvDriverSim : public Everest::ModuleBase {
public:
    EvDriverSim() = delete;
    EvDriverSim(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                std::unique_ptr<ev_board_supportImplBase> p_board_support,
                std::unique_ptr<ev_board_supportIntf> r_ev_board_support, Conf& config) :
        ModuleBase(info),
        mqtt(mqtt_provider),
        p_board_support(std::move(p_board_support)),
        r_ev_board_support(std::move(r_ev_board_support)),
        config(config){};

    Everest::MqttProvider& mqtt;
    const std::unique_ptr<ev_board_supportImplBase> p_board_support;
    const std::unique_ptr<ev_board_supportIntf> r_ev_board_support;
    const Conf& config;

    // ev@1fce4c5e-0ab8-41bb-90f7-14277703d2ac:v1
    void enable(bool value);
    void set_cp(types::ev_board_support::EvCpState cp);
    void diode_fail(bool value);
    void set_rcd_error(double current_mA);
    ~EvDriverSim();
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
    struct State {
        PilotGate gate;
        WorldFaults faults;
        ScenarioRunner scenario;
        bool ready{false};
        bool controller_seen{false};
        bool controller_fault{false};
        bool stopping{false};
    };
    everest::lib::util::monitor<State> m_state{State{PilotGate{config.plugged_at_startup}, {}, {}}};
    void apply(const PilotGate::Out& out);
    void apply(const WorldFaults::Out& out);
    void command(const std::string& name, const std::string& payload);
    void publish_world_state(const State& state);
    std::optional<std::string> world_command(State& state, const std::string& name, const std::string& payload);
    ScenarioRunner::Actions scenario_actions(State& state);
    void fail_scenario(State& state, const std::string& failure);
    std::thread m_scenario_timer;
    everest::lib::API::Topics m_topics;
    std::vector<Everest::UnsubscribeToken> m_subscriptions;
    std::thread m_startup_watchdog;
    void stop();
    everest::lib::API::CommCheckHandler<ev_board_supportImplBase> m_comm_check{
        "generic/CommunicationFault", "Bridge to implementation connection lost", p_board_support};
    size_t m_heartbeat_id{0};
    // ev@211cfdbe-f69a-4cd6-a4ec-f8aaa3d1b6c8:v1
};

// ev@087e516b-124c-48df-94fb-109508c7cda9:v1
// insert other definitions here
// ev@087e516b-124c-48df-94fb-109508c7cda9:v1

} // namespace module

#endif // EV_DRIVER_SIM_HPP
