// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef UTILS_SHM_FRAMEWORK_TRANSPORT_HPP
#define UTILS_SHM_FRAMEWORK_TRANSPORT_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/shm/topic.hpp>
#include <everest/util/async/monitor.hpp>

#include <utils/config/mqtt_settings.hpp>
#include <utils/framework_transport.hpp>
#include <utils/message_handler.hpp>
#include <utils/message_queue.hpp>
#include <utils/types.hpp>

namespace everest::lib::io::shm {
class shm_client;
enum class io_status;
} // namespace everest::lib::io::shm

namespace Everest {
/// \brief Framework-local transport backed by shared memory.
class ShmFrameworkTransport : public FrameworkTransport {
public:
    ShmFrameworkTransport(const MQTTSettings& mqtt_settings);

    ~ShmFrameworkTransport() override;

    ShmFrameworkTransport(ShmFrameworkTransport const&) = delete;
    void operator=(ShmFrameworkTransport const&) = delete;

    bool connect() override;
    void disconnect() override;

    /// \brief Suppress expected SHM errors while the Manager is stopping the transport.
    bool begin_controlled_shutdown();
    bool is_controlled_shutdown() const;
    std::uint64_t suppressed_shutdown_error_count() const;
    std::uint64_t unexpected_error_count() const;
    std::uint64_t sequence_anomaly_count() const;

    void publish(const std::string& topic, const nlohmann::json& json) override;
    void publish(const std::string& topic, const nlohmann::json& json, QOS qos, bool retain = false) override;
    void publish(const std::string& topic, const std::string& data) override;
    void publish(const std::string& topic, const std::string& data, QOS qos, bool retain = false) override;
    void subscribe(const std::string& topic) override;
    void subscribe(const std::string& topic, QOS qos) override;
    void unsubscribe(const std::string& topic) override;
    void clear_retained_topics() override;
    nlohmann::json get(const MQTTRequest& request, std::size_t retries = 0) override;
    nlohmann::json get(const std::string& topic, QOS qos, std::size_t retries = 0) override;
    const std::string& get_everest_prefix() const override;
    const std::string& get_external_prefix() const override;
    std::shared_future<void> spawn_main_loop_thread() override;
    std::shared_future<void> get_main_loop_future() override;
    void register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS qos) override;
    void unregister_handler(const std::string& topic, const Token& token) override;

private:
    struct PendingGet {
        bool ready{false};
        nlohmann::json response;
    };

    struct TopicState {
        std::unordered_set<std::string> subscribed_topics;
        std::unordered_set<std::string> retained_topics;
        std::unordered_map<std::string, std::size_t> handler_subscription_ref_counts;
    };

    struct HandlerTokenState {
        std::map<std::string, std::vector<Token>> handler_tokens;
    };

    struct PendingGetState {
        std::map<std::string, std::shared_ptr<PendingGet>> pending_gets;
    };

    struct ResponseTopicLockState {};

    struct ResponseTopicLockRegistry {
        std::map<std::string, std::shared_ptr<everest::lib::util::monitor<ResponseTopicLockState>>> locks;
    };

    struct MainLoopState {
        std::thread thread;
        std::thread::id thread_id;
        bool stopping{false};
        std::shared_future<void> future;
    };

    void run_on_main_loop(std::function<void()> operation);
    std::vector<std::string> concrete_topics_for_handler_filter(const std::string& topic_filter) const;
    std::vector<std::string> register_concrete_handler_subscriptions(const std::vector<std::string>& concrete_topics,
                                                                     QOS qos);
    void unregister_concrete_handler_subscriptions(const std::vector<std::string>& concrete_topics);
    void publish_internal(const std::string& topic, const std::string& data, QOS qos, bool retain,
                          bool block_on_full_buffer);
    nlohmann::json get_internal(const MQTTRequest& request);
    nlohmann::json wait_for_get_response(const MQTTRequest& request, const std::shared_ptr<PendingGet>& pending);
    void drain_stale_get_responses();
    void drain_transport_queue();
    bool register_transport_queue_events();
    bool unregister_transport_queue_events();
    std::shared_ptr<everest::lib::util::monitor<ResponseTopicLockState>>
    response_topic_lock_for(const std::string& topic);
    void cleanup_response_topic_lock(
        const std::string& topic,
        const std::shared_ptr<everest::lib::util::monitor<ResponseTopicLockState>>& response_topic_lock);
    bool notify_pending_get(const Message& message, const nlohmann::json& payload);
    void on_shm_message(const Message& message);
    void on_shm_sequence_anomaly(const std::string& topic,
                                 const everest::lib::io::shm::topic::sequence_validation_result& result);
    bool suppress_expected_shutdown_failure(everest::lib::io::shm::io_status status, const std::string& message,
                                            bool direct_api_failure = false);

    std::unique_ptr<everest::lib::io::shm::shm_client> shm_client;
    MessageHandler message_handler;
    std::vector<std::string> registered_topics;
    std::string mqtt_everest_prefix;
    std::string mqtt_external_prefix;
    TransportCallbackQueue message_queue;
    everest::lib::util::monitor<TopicState> topics;
    everest::lib::util::monitor<HandlerTokenState> handlers;
    everest::lib::util::monitor<PendingGetState> pending_gets;
    everest::lib::util::monitor<ResponseTopicLockRegistry> response_topic_locks;
    std::atomic_bool running;
    std::atomic_bool controlled_shutdown{false};
    std::atomic<std::uint64_t> suppressed_shutdown_errors{0};
    std::atomic<std::uint64_t> unexpected_errors{0};
    std::atomic<std::uint64_t> sequence_anomalies{0};
    everest::lib::util::monitor<MainLoopState> main_loop;
    everest::lib::io::event::event_fd disconnect_event;
    everest::lib::io::event::fd_event_handler ev_handler;
    const int initial_parent_pid;

#ifdef EVEREST_FRAMEWORK_TRANSPORT_TESTING
public:
    bool enqueue_transport_message_for_test(Message message) {
        return this->message_queue.enqueue(std::move(message));
    }
    std::size_t drain_transport_queue_for_test() {
        return this->message_queue.drain([this](const Message& message) { this->on_shm_message(message); });
    }
    bool register_transport_queue_events_for_test() {
        return this->register_transport_queue_events();
    }
    bool unregister_transport_queue_events_for_test() {
        return this->unregister_transport_queue_events();
    }
    bool poll_transport_events_for_test(std::chrono::milliseconds timeout) {
        return this->ev_handler.poll(timeout);
    }
#endif
};

} // namespace Everest

#endif // UTILS_SHM_FRAMEWORK_TRANSPORT_HPP
