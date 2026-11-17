// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "EvController.hpp"
#include "main/EvCtlRuntime.hpp"
#include "main/Events.hpp"
#include "main/FsmContext.hpp"

#include <everest/exceptions.hpp>
#include <everest/logging.hpp>
#include <everest_api_types/ev_controller/codec.hpp>
#include <everest_api_types/generic/codec.hpp>

#include <exception>

namespace module {

namespace API_generic = everest::lib::API::V1_0::types::generic;

void EvController::RuntimeDeleter::operator()(EvCtlRuntime* runtime) const {
    delete runtime;
}

void EvController::init() {
    if (const auto error = cross_boot_config_error(config, !r_kvs.empty())) {
        EVLOG_AND_THROW(Everest::EverestConfigError("EvController: " + *error));
    }
    if (!config.default_session.empty()) {
        try {
            m_default_session = API_types::ev_controller::deserialize<API_types::ev_controller::SessionConfigParams>(
                config.default_session);
        } catch (const std::exception& e) {
            m_default_session_error = e.what();
        }
    }
    invoke_init(*p_ev_manager);

    topics.setup(info.id, "ev_controller", 1);

    // Peers publish from their own ready(), possibly before this module's, and publications are
    // not retained. The queue holds what arrives until run() drains it: enqueue() increments the
    // wake_fd eventfd, which run() then sees readable at once. The enable goes first so that a
    // startup pilot event reaches Unplugged rather than Disabled.
    runtime.reset(new EvCtlRuntime(*this));
    if (config.enabled_at_startup) {
        runtime->enqueue(Event{EnableCmd{}});
    }
    runtime->register_m2e_subscriptions();
    runtime->register_peer_subscriptions();
}

void EvController::ready() {
    invoke_ready(*p_ev_manager);

    auto& default_session = runtime->ctx_ptr()->default_session;
    if (default_session && !runtime->ctx_ptr()->validate_session_config(*default_session, m_default_session_error)) {
        default_session.reset();
    }
    if (!m_default_session_error.empty()) {
        p_ev_manager->raise_error(p_ev_manager->error_factory->create_error(
            "generic/CommunicationFault", "default_session", "default_session: " + m_default_session_error,
            Everest::error::Severity::High));
    }

    // kvs_load() seeds `persisted` and so must run before the first flush, which is why it is
    // here rather than on the loop thread.
    runtime->ctx_ptr()->kvs_load();

    comm_check->start(config.cfg_communication_check_to_s);
    setup_heartbeat_generator();

    // The loop thread is a raw std::thread: an exception escaping its entry
    // function calls std::terminate() and takes the whole module process
    // down (the framework dispatches ready() in a thread with no catch, so
    // moving register_or_throw into ready() would terminate identically).
    // Contain any throw here so a failed fd registration / FSM construction
    // degrades to an inert loop instead of a process crash, reported by a
    // stopped heartbeat.
    loop_thread = std::thread([this] {
        try {
            runtime->run(loop_online);
        } catch (const std::exception& e) {
            on_loop_abort(e.what());
        } catch (...) {
            on_loop_abort("unknown exception");
        }
        loop_online = false;
    });
}

void EvController::on_loop_abort(const std::string& reason) {
    EVLOG_error << "EvController: loop thread aborting: " << reason;
    loop_online = false;
}

void EvController::shutdown() {
    invoke_shutdown(*p_ev_manager);
}

EvController::~EvController() {
    loop_online = false;
    if (runtime) {
        runtime->wake();
    }
    if (loop_thread.joinable()) {
        loop_thread.join();
    }
    // The runtime destructor invokes the stored UnsubscribeTokens, detaching
    // the m2e MQTT handlers (one of them reaches `comm_check`) before the
    // FsmContext / queue / event_fds they capture are torn down.
    runtime.reset();
    // Destroying `comm_check` stops and joins the heartbeat thread, whose
    // action dereferences `mqtt` and `hb_id`. Member destruction would reach it
    // only after `hb_id` is gone, so destroy it here.
    comm_check.reset();
}

void EvController::setup_heartbeat_generator() {
    auto topic = topics.everest_to_extern("heartbeat");
    // MqttProvider::publish() returns void; the framework treats publishes
    // as fire-and-forget, so a transport-level failure is not observable
    // here. A false return, after a publish/serialize exception or once the
    // loop thread is gone, ends the heartbeat thread for good, so clients see
    // the failure as missing heartbeats.
    auto action = [this, topic]() {
        if (!loop_online) {
            return false;
        }
        try {
            mqtt.publish(topic, API_generic::serialize(hb_id++));
        } catch (const std::exception& e) {
            EVLOG_error << "EvController: heartbeat publish failed: " << e.what();
            return false;
        } catch (...) {
            EVLOG_error << "EvController: heartbeat publish failed: unknown exception";
            return false;
        }
        return true;
    };
    comm_check->heartbeat(config.cfg_heartbeat_interval_ms, action);
}

} // namespace module
