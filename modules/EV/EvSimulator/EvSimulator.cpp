// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvSimulator.hpp"
#include "main/EvSimRuntime.hpp"
#include "main/Events.hpp"
#include "main/FsmContext.hpp"

#include <everest/logging.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <exception>

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

    // Subscriptions are registered here, on the main thread, before the loop
    // thread starts. An MQTT / peer callback that fires in the window before
    // loop_thread runs runtime->run() (which registers the fd handlers and
    // builds the FSM) is not lost: EvSimRuntime::enqueue() pushes onto a
    // mutex-guarded thread_safe_queue and then increments the wake_fd eventfd
    // counter. The eventfd counter is durable kernel state independent of
    // whether an epoll handler is attached, so when run() later registers the
    // wake_fd handler and enters loop.run(), the still-non-zero counter makes
    // epoll report it readable immediately and on_wake flushes the whole queue.
    // No event in that startup window can be dropped. Token registration and
    // teardown both stay off the loop thread (the dtor invokes the stored
    // UnsubscribeTokens), so command_router_tokens needs no cross-thread
    // synchronization. kvs_load() must run before the queue is first flushed
    // (it seeds `persisted`); running it here, before loop_thread starts,
    // guarantees that ordering.
    runtime->register_m2e_subscriptions();
    runtime->register_peer_subscriptions();
    runtime->ctx_ptr()->kvs_load();

    // Auto-enable at startup when configured. Disabled by default in unit
    // tests so they observe the initial Disabled state before driving enable.
    if (config.enabled_at_startup) {
        runtime->enqueue(Event{EnableCmd{}});
    }

    comm_check.start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator();

    // The loop thread is a raw std::thread: an exception escaping its entry
    // function calls std::terminate() and takes the whole module process
    // down (the framework dispatches ready() in a thread with no catch, so
    // moving register_or_throw into ready() would terminate identically).
    // Contain any throw here so a failed fd registration / FSM construction
    // degrades to an inert loop instead of a process crash; loop_online is
    // cleared so the teardown/join path observes shutdown.
    loop_thread = std::thread([this] {
        try {
            runtime->run(loop_online);
        } catch (const std::exception& e) {
            EVLOG_error << "EvSimulator: loop thread aborting: " << e.what();
        } catch (...) {
            EVLOG_error << "EvSimulator: loop thread aborting: unknown exception";
        }
        loop_online = false;
    });
}

EvSimulator::~EvSimulator() {
    // Stop the heartbeat thread first so its action cannot observe a
    // half-destroyed `this` (the action dereferences `mqtt`, `topics`, and
    // `hb_id`). The destructor of `comm_check` would also stop the thread, but
    // by the time member destruction reaches it, `topics`/`runtime` are already
    // gone — so do it here explicitly.
    comm_check.stop_heartbeat();

    loop_online = false;
    if (runtime) {
        runtime->wake();
    }
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
    // `runtime` is destroyed next as part of normal member destruction. Its
    // destructor invokes the stored UnsubscribeTokens, detaching the m2e MQTT
    // handlers before the FsmContext / queue / event_fds they capture are torn
    // down.
}

void EvSimulator::setup_heartbeat_generator() {
    auto topic = topics.everest_to_extern("heartbeat");
    // MqttProvider::publish() returns void — the framework treats publishes
    // as fire-and-forget, so a transport-level failure is not observable
    // here. The one failure we can surface to comm_check is an exception out
    // of publish/serialize: report it as a failed heartbeat instead of
    // unconditionally claiming success.
    auto action = [this, topic]() {
        try {
            mqtt.publish(topic, API_generic::serialize(hb_id++));
        } catch (const std::exception& e) {
            EVLOG_error << "EvSimulator: heartbeat publish failed: " << e.what();
            return false;
        } catch (...) {
            EVLOG_error << "EvSimulator: heartbeat publish failed: unknown exception";
            return false;
        }
        return true;
    };
    comm_check.heartbeat(config.cfg_heartbeat_interval_ms, action);
}

} // namespace module
