// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvSimulator.hpp"
#include "main/EvSimRuntime.hpp"
#include "main/FsmContext.hpp"

#include <everest_api_types/generic/codec.hpp>

namespace module {

namespace API_generic = everest::lib::API::V1_0::types::generic;

EvSimulator::EvSimulator(const ModuleInfo& info, Everest::MqttProvider& mqtt_provider,
                         std::unique_ptr<ev_managerImplBase> p_ev_manager_,
                         std::unique_ptr<ev_board_supportIntf> r_ev_board_support_,
                         std::vector<std::unique_ptr<ISO15118_evIntf>> r_ev_,
                         std::vector<std::unique_ptr<ev_slacIntf>> r_slac_,
                         std::vector<std::unique_ptr<kvsIntf>> r_kvs_, Conf& config_) :
    ModuleBase(info),
    mqtt(mqtt_provider),
    p_ev_manager(std::move(p_ev_manager_)),
    r_ev_board_support(std::move(r_ev_board_support_)),
    r_ev(std::move(r_ev_)),
    r_slac(std::move(r_slac_)),
    r_kvs(std::move(r_kvs_)),
    config(config_) {
}

void EvSimulator::init() {
    invoke_init(*p_ev_manager);

    topics.setup(info.id, "ev_simulator", 1);
}

void EvSimulator::ready() {
    invoke_ready(*p_ev_manager);

    runtime = std::make_unique<EvSimRuntime>(*this);
    runtime->register_m2e_subscriptions();
    runtime->register_peer_subscriptions();
    runtime->ctx_ptr()->kvs_load();

    comm_check.start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator();

    loop_thread = std::thread([this] { runtime->run(loop_online); });
}

EvSimulator::~EvSimulator() {
    loop_online = false;
    if (runtime) {
        runtime->wake();
    }
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
}

void EvSimulator::setup_heartbeat_generator() {
    auto topic = topics.everest_to_extern("heartbeat");
    auto action = [this, topic]() {
        mqtt.publish(topic, API_generic::serialize(hb_id++));
        return true;
    };
    comm_check.heartbeat(config.cfg_heartbeat_interval_ms, action);
}

} // namespace module
