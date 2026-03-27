// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <everest/util/async/monitor.hpp>
#include <everest/util/async/thread_pool_scaling.hpp>
#include <everest/util/queue/thread_safe_queue.hpp>

#include <utils/message_queue.hpp>
#include <utils/types.hpp>

using MqttTopic = std::string;
using CmdId = std::string;

namespace Everest {

constexpr std::size_t THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS = 50;
constexpr std::chrono::seconds THREAD_POOL_SCALING_IDLE_TIMEOUT{2};
constexpr std::size_t THREAD_POOL_SCALING_MIN_THREAD_COUNT = 1;
constexpr std::size_t MAX_PENDING_MESSAGES_PER_TOPIC = 100;

/// \brief Handles message dispatching and thread-safe queuing of different message types.
///
/// Messages are routed to one of four channels based on their type:
///   - operation_message_queue → operation_dispatcher_thread → operation_thread_pool
///     (vars, cmds, errors, ConfigurationRequest, ModuleReady — parallel across topics, serial per topic)
///   - result_message_queue    → result_worker_thread (cmd results, ConfigurationResponse — serial)
///   - external_mqtt_message_queue → external_mqtt_worker_thread (external MQTT — serial)
///   - GlobalReady             → ready_thread (one-shot, spawned per message)
class MessageHandler {
public:
    MessageHandler();
    ~MessageHandler();

    /// \brief Adds given \p message to the message queue for processing
    void add(const ParsedMessage& message);

    /// \brief Stops all threads started by this handler
    void stop();

    /// \brief Registers a \p handler for a specific \p topic
    void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler);

    using SharedTypedHandler = std::shared_ptr<TypedHandler>;
    using SingleHandlerMap = std::map<MqttTopic, SharedTypedHandler>;
    using MultiHandlerMap = std::map<MqttTopic, std::vector<SharedTypedHandler>>;

private:
    struct OperationTopics {
        std::unordered_set<std::string> in_flight;
        std::unordered_map<std::string, everest::lib::util::simple_queue<ParsedMessage>> pending_messages;
    };

    struct ResponseHandlers {
        std::map<CmdId, std::shared_ptr<TypedHandler>> cmd; // cmd result handlers of module
        std::shared_ptr<TypedHandler> config;               // get module config response handler of module
    };

    struct GenericHandlers {
        MultiHandlerMap var;                    // var handlers of module
        SingleHandlerMap cmd;                   // cmd handlers of module
        MultiHandlerMap error;                  // error handlers with wildcard support
        SingleHandlerMap configuration_request; // configuration request handler of manager
        SharedTypedHandler global_ready;        // global ready handler of module
        SingleHandlerMap module_ready;          // module ready handlers of manager
        MultiHandlerMap external_var;           // external MQTT handlers of module
    };

    void run_operation_dispatcher();
    void run_result_message_worker();
    void run_external_mqtt_worker();

    void dispatch_operation_message(ParsedMessage&& message);
    void schedule_operation_message(ParsedMessage&& message);
    void on_operation_message_done(const std::string& topic);

    void handle_operation_message(const std::string& topic, const json& payload);
    void handle_result_message(const std::string& topic, const json& payload);

    // Individual message handler methods
    void handle_var_message(const std::string& topic, const json& data);
    void handle_cmd_message(const std::string& topic, const json& data);
    void handle_external_mqtt_message(const std::string& topic, const json& data);
    void handle_error_message(const std::string& topic, const json& data);
    void handle_get_config_message(const std::string& topic, const json& data);
    void handle_module_ready_message(const std::string& topic, const json& data);
    void handle_cmd_result(const std::string& topic, const json& payload);
    void handle_get_config_response(const std::string& topic, const json& payload);

    // Threads
    std::thread operation_dispatcher_thread; // processes vars, commands, external MQTT, errors, ConfigurationRequest
                                             // and ModuleReady messages
    std::thread result_worker_thread;        // processes cmd results and ConfigurationResponse messages
    std::thread external_mqtt_worker_thread; // processes external MQTT messages

    // Wrapped in a monitor so that concurrent GlobalReady arrivals in add() are safe:
    // the steal-then-join pattern moves the previous thread out under the lock and joins
    // outside the lock, preventing concurrent join/assignment races on the raw std::thread.
    everest::lib::util::monitor<std::thread> ready;

    using LatencyScaling = everest::lib::util::LatencyScaling<THREAD_POOL_SCALING_LATENCY_THRESHOLD_MS>;
    using ThreadPool = everest::lib::util::thread_pool_scaling<LatencyScaling, everest::lib::util::RethrowExceptions>;
    std::unique_ptr<ThreadPool> operation_thread_pool;

    using MessageQueue = everest::lib::util::thread_safe_queue<ParsedMessage>;
    MessageQueue operation_message_queue;
    MessageQueue result_message_queue;
    MessageQueue external_mqtt_message_queue;

    everest::lib::util::monitor<OperationTopics> operations;
    everest::lib::util::monitor<ResponseHandlers> responses;
    everest::lib::util::monitor<GenericHandlers> handlers;

    std::atomic<bool> running = true;
};

} // namespace Everest
