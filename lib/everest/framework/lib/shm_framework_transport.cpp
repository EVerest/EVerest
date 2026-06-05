#include <algorithm>
#include <atomic>
#include <chrono>
#include <utils/shm_framework_transport.hpp>

#include <fmt/format.h>
#include <unistd.h>

#include <everest/exceptions.hpp>
#include <everest/io/shm/shm_client.hpp>
#include <everest/io/shm/topic_filter.hpp>
#include <everest/logging.hpp>

namespace Everest {
namespace {

namespace shm = everest::lib::io::shm;

bool use_abstract_namespace_for_control_socket(const std::string& control_socket_name) {
    return control_socket_name.empty() || control_socket_name.front() != '/';
}

std::string make_client_id() {
    static std::atomic_uint64_t next_id{0};
    return fmt::format("everest-framework-shm-{}-{}", ::getpid(), next_id.fetch_add(1, std::memory_order_relaxed));
}

shm::client_options make_client_options(const MQTTSettings& mqtt_settings) {
    shm::client_options options;
    options.client_id = make_client_id();
    const auto control_socket_name = mqtt_settings.shm_control_socket_path.empty()
                                         ? mqtt_settings.broker_socket_path
                                         : mqtt_settings.shm_control_socket_path;
    options.control.server_name = control_socket_name;
    options.control.server_abstract_namespace = use_abstract_namespace_for_control_socket(control_socket_name);
    return options;
}

void throw_on_shm_failure(const shm::io_result& result, const std::string& action, const std::string& topic) {
    if (result.status == shm::io_status::ok) {
        return;
    }

    EVLOG_AND_THROW(EverestInternalError(fmt::format("SHM {} failed for topic '{}': {} {}", action, topic,
                                                     shm::to_string(result.status), result.message)));
}

json payload_for_get_response(const json& payload) {
    const auto msg_type = payload.is_object() ? payload.find("msg_type") : payload.end();
    if (payload.is_object() && msg_type != payload.end() && msg_type->is_string() &&
        msg_type->get<std::string>() == mqtt_message_type_to_string(MqttMessageType::GetConfigResponse) &&
        payload.contains("data")) {
        return payload.at("data");
    }
    return payload;
}

json parse_request_data(const MQTTRequest& request) {
    if (!request.request_data.has_value()) {
        return {};
    }

    auto parsed = json::parse(request.request_data.value(), nullptr, false);
    if (parsed.is_discarded()) {
        EVLOG_AND_THROW(
            EverestInternalError(fmt::format("Invalid JSON request payload for SHM get({})", request.response_topic)));
    }
    return parsed;
}

std::shared_future<void> make_ready_main_loop_future() {
    std::promise<void> promise;
    promise.set_value();
    return promise.get_future().share();
}

std::string_view sequence_status_to_string(shm::topic::sequence_status status) {
    switch (status) {
    case shm::topic::sequence_status::accepted:
        return "accepted";
    case shm::topic::sequence_status::stale:
        return "stale";
    case shm::topic::sequence_status::gap:
        return "gap";
    }
    return "unknown";
}

} // namespace

ShmFrameworkTransport::ShmFrameworkTransport(const MQTTSettings& mqtt_settings) :
    shm_client(std::make_unique<shm::shm_client>(make_client_options(mqtt_settings))),
    registered_topics(mqtt_settings.shm_registered_topics),
    mqtt_everest_prefix(mqtt_settings.everest_prefix),
    mqtt_external_prefix(mqtt_settings.external_prefix),
    running(false),
    initial_parent_pid(::getppid()) {
    this->main_loop.handle()->future = make_ready_main_loop_future();
    this->shm_client->set_error_handler([this](shm::io_status status, std::string_view message) {
        if (status == shm::io_status::ok || status == shm::io_status::already_open) {
            return;
        }

        if (this->suppress_expected_shutdown_failure(status, std::string(message))) {
            return;
        }

        this->unexpected_errors.fetch_add(1, std::memory_order_relaxed);
        EVLOG_error << fmt::format("SHM client error: {} {}", shm::to_string(status), message);
    });
}

ShmFrameworkTransport::~ShmFrameworkTransport() {
    this->disconnect();
}

bool ShmFrameworkTransport::begin_controlled_shutdown() {
    return this->controlled_shutdown.exchange(true, std::memory_order_acq_rel);
}

bool ShmFrameworkTransport::is_controlled_shutdown() const {
    return this->controlled_shutdown.load(std::memory_order_acquire);
}

std::uint64_t ShmFrameworkTransport::suppressed_shutdown_error_count() const {
    return this->suppressed_shutdown_errors.load(std::memory_order_relaxed);
}

std::uint64_t ShmFrameworkTransport::unexpected_error_count() const {
    return this->unexpected_errors.load(std::memory_order_relaxed);
}

std::uint64_t ShmFrameworkTransport::sequence_anomaly_count() const {
    return this->sequence_anomalies.load(std::memory_order_relaxed);
}

bool ShmFrameworkTransport::suppress_expected_shutdown_failure(shm::io_status status, const std::string& message,
                                                               bool direct_api_failure) {
    const auto manager_parent_gone = this->initial_parent_pid != ::getppid();
    const auto controlled_not_open = this->controlled_shutdown.load(std::memory_order_acquire) &&
                                     status == shm::io_status::not_open &&
                                     (!direct_api_failure || this->running.load(std::memory_order_acquire));
    if (controlled_not_open || manager_parent_gone) {
        this->suppressed_shutdown_errors.fetch_add(1, std::memory_order_relaxed);
        EVLOG_info << fmt::format("SHM client shutdown signal: {} {} (expected during controlled stop)\n",
                                  shm::to_string(status), message);
        return true;
    }
    return false;
}

bool ShmFrameworkTransport::connect() {
    const auto result = this->shm_client->connect();
    if (result.status == shm::io_status::already_open) {
        this->controlled_shutdown.store(false, std::memory_order_release);
        this->running = true;
        return true;
    }
    if (result.status != shm::io_status::ok) {
        EVLOG_critical << fmt::format("Could not connect to SHM transport: {} {}", shm::to_string(result.status),
                                      result.message);
        return false;
    }
    this->controlled_shutdown.store(false, std::memory_order_release);
    this->running = true;
    return true;
}

void ShmFrameworkTransport::disconnect() {
    this->controlled_shutdown.store(true, std::memory_order_release);

    std::thread loop_thread;
    {
        auto loop = this->main_loop.handle();
        const auto has_loop = loop->thread.joinable() || loop->stopping;
        this->running = false;
        if (has_loop) {
            this->disconnect_event.notify();
        }
        if (loop->thread.joinable() && std::this_thread::get_id() != loop->thread_id) {
            loop->stopping = true;
            loop_thread = std::move(loop->thread);
        }
    }
    if (loop_thread.joinable()) {
        loop_thread.join();
        auto loop = this->main_loop.handle();
        loop->thread_id = std::thread::id{};
        loop->stopping = false;
    }
    this->drain_transport_queue();

    {
        auto topics = this->topics.handle();
        topics->subscribed_topics.clear();
        topics->handler_subscription_ref_counts.clear();
    }

    const auto result = this->shm_client->disconnect();
    if (result.status != shm::io_status::ok) {
        EVLOG_error << fmt::format("SHM disconnect failed: {} {}", shm::to_string(result.status), result.message);
    }
}

void ShmFrameworkTransport::publish(const std::string& topic, const nlohmann::json& json) {
    publish(topic, json, QOS::QOS2);
}

void ShmFrameworkTransport::publish(const std::string& topic, const nlohmann::json& json, QOS qos, bool retain) {
    publish(topic, json.dump(), qos, retain);
}

void ShmFrameworkTransport::publish(const std::string& topic, const std::string& data) {
    publish(topic, data, QOS::QOS0);
}

void ShmFrameworkTransport::publish(const std::string& topic, const std::string& data, QOS qos, bool retain) {
    publish_internal(topic, data, qos, retain, false);
}

void ShmFrameworkTransport::publish_internal(const std::string& topic, const std::string& data, QOS qos, bool retain,
                                             bool block_on_full_buffer) {
    if (topic.empty()) {
        return;
    }

    shm::publish_options options;
    options.full_buffer_behavior =
        block_on_full_buffer ? shm::publish_full_buffer_behavior::block : shm::publish_full_buffer_behavior::fail;
    options.retain = retain;
    options.clear_retained = retain && qos == QOS::QOS0 && data.empty();
    shm::io_result result;
    this->run_on_main_loop([&]() { result = this->shm_client->publish(topic, data, options); });
    if (result.status != shm::io_status::ok &&
        this->suppress_expected_shutdown_failure(result.status, result.message, true)) {
        return;
    }
    throw_on_shm_failure(result, "publish", topic);

    if (retain) {
        auto topics = this->topics.handle();
        if (options.clear_retained) {
            topics->retained_topics.erase(topic);
        } else {
            topics->retained_topics.insert(topic);
        }
    }

    EVLOG_verbose << fmt::format(
        "publishing to SHM topic: {} with payload: {} and qos: {} and retain: {} "
        "(qos accepted for API compatibility; retained payloads are manager-side local SHM state)",
        topic, data, static_cast<int>(qos), static_cast<int>(retain));
}

void ShmFrameworkTransport::subscribe(const std::string& topic) {
    subscribe(topic, QOS::QOS2);
}

void ShmFrameworkTransport::subscribe(const std::string& topic, QOS qos) {
    if (topic.empty()) {
        return;
    }

    shm::subscribe_options options;
    options.use_join_cursor = true;
    {
        auto topics = this->topics.handle();
        if (topics->subscribed_topics.find(topic) != topics->subscribed_topics.end()) {
            EVLOG_verbose << fmt::format("SHM topic {} is already subscribed", topic);
            return;
        }
        topics->subscribed_topics.insert(topic);
    }

    shm::io_result result;
    this->run_on_main_loop([&]() {
        result = this->shm_client->subscribe(
            topic,
            [this](std::string_view topic_view, std::string_view payload_view) {
                this->message_queue.enqueue(Message(std::string(topic_view), std::string(payload_view)));
            },
            [this, topic](const shm::topic::sequence_validation_result& sequence_result) {
                this->on_shm_sequence_anomaly(topic, sequence_result);
            },
            options);
    });
    try {
        if (result.status != shm::io_status::ok &&
            this->suppress_expected_shutdown_failure(result.status, result.message, true)) {
            auto topics = this->topics.handle();
            topics->subscribed_topics.erase(topic);
            return;
        }
        throw_on_shm_failure(result, "subscribe", topic);
    } catch (...) {
        auto topics = this->topics.handle();
        topics->subscribed_topics.erase(topic);
        throw;
    }

    EVLOG_verbose << fmt::format("subscribed to SHM topic: {} with qos: {}", topic, static_cast<int>(qos));
}

void ShmFrameworkTransport::unsubscribe(const std::string& topic) {
    if (topic.empty()) {
        return;
    }

    {
        auto topics = this->topics.handle();
        if (topics->subscribed_topics.find(topic) == topics->subscribed_topics.end()) {
            EVLOG_warning << fmt::format("Tried to unsubscribe from SHM topic {} but it was not subscribed", topic);
            return;
        }
        topics->subscribed_topics.erase(topic);
        topics->handler_subscription_ref_counts.erase(topic);
    }

    shm::io_result result;
    this->run_on_main_loop([&]() { result = this->shm_client->unsubscribe(topic); });
    try {
        if (result.status != shm::io_status::ok &&
            this->suppress_expected_shutdown_failure(result.status, result.message, true)) {
            return;
        }
        throw_on_shm_failure(result, "unsubscribe", topic);
    } catch (...) {
        auto topics = this->topics.handle();
        topics->subscribed_topics.insert(topic);
        throw;
    }

    EVLOG_verbose << fmt::format("Unsubscribed from SHM topic: {}", topic);
}

void ShmFrameworkTransport::clear_retained_topics() {
    std::vector<std::string> topics;
    {
        auto state = this->topics.handle();
        topics.assign(state->retained_topics.begin(), state->retained_topics.end());
    }

    for (const auto& retained_topic : topics) {
        this->publish(retained_topic, std::string(), QOS::QOS0, true);
        EVLOG_verbose << "Cleared SHM retained topic: " << retained_topic;
    }
}

nlohmann::json ShmFrameworkTransport::get(const MQTTRequest& request, std::size_t retries) {
    std::size_t attempt = 0;
    while (attempt <= retries) {
        try {
            return this->get_internal(request);
        } catch (const EverestTimeoutError&) {
            if (attempt < retries) {
                attempt += 1;
            } else {
                std::rethrow_exception(std::current_exception());
            }
        }
    }
    EVLOG_AND_THROW(EverestInternalError(
        fmt::format("Unknown error while waiting for SHM result of get({})", request.response_topic)));
}

nlohmann::json ShmFrameworkTransport::get(const std::string& topic, QOS qos, std::size_t retries) {
    const MQTTRequest request = {topic, qos};
    return this->get(request, retries);
}

const std::string& ShmFrameworkTransport::get_everest_prefix() const {
    return mqtt_everest_prefix;
}

const std::string& ShmFrameworkTransport::get_external_prefix() const {
    return mqtt_external_prefix;
}

std::shared_future<void> ShmFrameworkTransport::spawn_main_loop_thread() {
    auto loop = this->main_loop.handle();
    if (loop->thread.joinable() || loop->stopping) {
        return loop->future;
    }

    this->running = true;
    std::packaged_task<void(void)> task([this]() {
        {
            auto loop = this->main_loop.handle();
            loop->thread_id = std::this_thread::get_id();
        }
        try {
            if (!this->ev_handler.register_event_handler(
                    static_cast<everest::lib::io::event::fd_event_register_interface*>(this->shm_client.get()))) {
                EVLOG_AND_THROW(EverestInternalError("Could not register SHM client with framework event loop"));
            }
            if (!this->ev_handler.register_event_handler(&this->disconnect_event,
                                                         [this](const auto&) { this->running = false; })) {
                EVLOG_AND_THROW(
                    EverestInternalError("Could not register SHM disconnect event with framework event loop"));
            }
            if (!this->register_transport_queue_events()) {
                EVLOG_AND_THROW(
                    EverestInternalError("Could not register SHM callback queue with framework event loop"));
            }

            this->ev_handler.run(this->running);
            this->drain_transport_queue();
            this->unregister_transport_queue_events();
            this->ev_handler.unregister_event_handler(
                static_cast<everest::lib::io::event::fd_event_register_interface*>(this->shm_client.get()));
            this->ev_handler.unregister_event_handler(&this->disconnect_event);
        } catch (std::exception& e) {
            EVLOG_critical << fmt::format("Caught SHM mainloop std::exception: {}", e.what());
            exit(1);
        }
    });

    loop->future = task.get_future().share();
    loop->thread = std::thread(std::move(task));
    return loop->future;
}

std::shared_future<void> ShmFrameworkTransport::get_main_loop_future() {
    return this->main_loop.handle()->future;
}

std::vector<std::string>
ShmFrameworkTransport::concrete_topics_for_handler_filter(const std::string& topic_filter) const {
    if (topic_filter.find_first_of("+#") == std::string::npos) {
        return {topic_filter};
    }
    if (!shm::is_valid_topic_filter(topic_filter)) {
        EVLOG_AND_THROW(EverestInternalError(fmt::format("Invalid SHM topic filter '{}'", topic_filter)));
    }

    std::vector<std::string> concrete_topics;
    for (const auto& registered_topic : this->registered_topics) {
        if (shm::topic_filter_matches(topic_filter, registered_topic)) {
            concrete_topics.push_back(registered_topic);
        }
    }
    if (concrete_topics.empty()) {
        EVLOG_AND_THROW(EverestInternalError(
            fmt::format("SHM topic filter '{}' matched no registered exact topics", topic_filter)));
    }
    return concrete_topics;
}

std::vector<std::string>
ShmFrameworkTransport::register_concrete_handler_subscriptions(const std::vector<std::string>& concrete_topics,
                                                               QOS qos) {
    std::vector<std::string> counted_topics;
    try {
        for (const auto& concrete_topic : concrete_topics) {
            bool subscribe_required = false;
            bool counted = false;
            {
                auto topics = this->topics.handle();
                auto ref_count = topics->handler_subscription_ref_counts.find(concrete_topic);
                if (ref_count != topics->handler_subscription_ref_counts.end()) {
                    ref_count->second++;
                    counted = true;
                } else if (topics->subscribed_topics.find(concrete_topic) == topics->subscribed_topics.end()) {
                    topics->handler_subscription_ref_counts[concrete_topic] = 1U;
                    subscribe_required = true;
                    counted = true;
                }
            }

            if (counted) {
                counted_topics.push_back(concrete_topic);
            }
            if (subscribe_required) {
                this->subscribe(concrete_topic, qos);
            }
        }
    } catch (...) {
        this->unregister_concrete_handler_subscriptions(counted_topics);
        throw;
    }
    return counted_topics;
}

void ShmFrameworkTransport::unregister_concrete_handler_subscriptions(const std::vector<std::string>& concrete_topics) {
    for (const auto& concrete_topic : concrete_topics) {
        bool unsubscribe_required = false;
        {
            auto topics = this->topics.handle();
            auto ref_count = topics->handler_subscription_ref_counts.find(concrete_topic);
            if (ref_count == topics->handler_subscription_ref_counts.end()) {
                continue;
            }
            ref_count->second--;
            if (ref_count->second == 0U) {
                topics->handler_subscription_ref_counts.erase(ref_count);
                unsubscribe_required =
                    topics->subscribed_topics.find(concrete_topic) != topics->subscribed_topics.end();
            }
        }

        if (unsubscribe_required) {
            this->unsubscribe(concrete_topic);
        }
    }
}

void ShmFrameworkTransport::run_on_main_loop(std::function<void()> operation) {
    std::future<void> future;
    bool run_directly = false;
    {
        auto loop = this->main_loop.handle();
        if (std::this_thread::get_id() == loop->thread_id || !this->running.load() || !loop->thread.joinable()) {
            run_directly = true;
        } else {
            auto task = std::make_shared<std::packaged_task<void()>>(std::move(operation));
            future = task->get_future();
            this->ev_handler.add_action([task]() { (*task)(); });
        }
    }
    if (run_directly) {
        operation();
        return;
    }
    future.get();
}

nlohmann::json ShmFrameworkTransport::get_internal(const MQTTRequest& request) {
    if (request.response_topic.empty()) {
        EVLOG_AND_THROW(EverestInternalError("SHM get requires a non-empty response topic"));
    }

    auto response_topic_lock = this->response_topic_lock_for(request.response_topic);
    struct ResponseTopicLockGuard {
        ShmFrameworkTransport* adapter;
        const std::string& topic;
        const std::shared_ptr<everest::lib::util::monitor<ResponseTopicLockState>>& response_topic_lock;

        ~ResponseTopicLockGuard() {
            adapter->cleanup_response_topic_lock(topic, response_topic_lock);
        }
    } response_topic_lock_guard{this, request.response_topic, response_topic_lock};
    auto response_topic_handle = response_topic_lock->handle();

    const auto pending = std::make_shared<PendingGet>();
    struct PendingGetGuard {
        ShmFrameworkTransport* adapter;
        const std::string& topic;
        const std::shared_ptr<PendingGet>& pending;

        ~PendingGetGuard() {
            auto pending_gets = adapter->pending_gets.handle();
            auto it = pending_gets->pending_gets.find(topic);
            if (it != pending_gets->pending_gets.end() && it->second == pending) {
                pending_gets->pending_gets.erase(it);
            }
        }
    } pending_get_guard{this, request.response_topic, pending};

    bool subscribed_before_get = false;
    {
        auto topics = this->topics.handle();
        subscribed_before_get =
            topics->subscribed_topics.find(request.response_topic) != topics->subscribed_topics.end();
    }

    if (!request.request_topic.has_value()) {
        auto pending_gets = this->pending_gets.handle();
        pending_gets->pending_gets[request.response_topic] = pending;
    }

    if (!subscribed_before_get) {
        this->subscribe(request.response_topic, request.qos);
    }
    struct GetSubscriptionGuard {
        ShmFrameworkTransport* adapter;
        const std::string& topic;
        bool active;

        ~GetSubscriptionGuard() {
            if (!active) {
                return;
            }
            try {
                adapter->unsubscribe(topic);
            } catch (const std::exception& e) {
                EVLOG_error << fmt::format("SHM get cleanup unsubscribe failed for topic '{}': {}", topic, e.what());
            }
        }
    } get_subscription_guard{this, request.response_topic, !subscribed_before_get};

    if (request.request_topic.has_value()) {
        this->drain_stale_get_responses();

        {
            auto pending_gets = this->pending_gets.handle();
            pending_gets->pending_gets[request.response_topic] = pending;
        }

        json req_data = parse_request_data(request);
        MqttMessagePayload payload{MqttMessageType::GetConfig, req_data};
        const json payload_json = payload;
        this->publish_internal(request.request_topic.value(), payload_json.dump(), request.qos, false, true);
    }

    return this->wait_for_get_response(request, pending);
}

nlohmann::json ShmFrameworkTransport::wait_for_get_response(const MQTTRequest& request,
                                                            const std::shared_ptr<PendingGet>& pending) {
    const auto deadline = std::chrono::steady_clock::now() + request.timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        bool should_sync_directly = false;
        {
            auto loop = this->main_loop.handle();
            should_sync_directly = !loop->thread.joinable() || std::this_thread::get_id() == loop->thread_id;
        }

        {
            if (should_sync_directly) {
                (void)this->shm_client->sync();
                this->drain_transport_queue();
            } else {
                this->run_on_main_loop([&]() { (void)this->shm_client->sync(); });
            }

            auto pending_gets = this->pending_gets.handle();
            const auto wait_deadline =
                std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(5));
            if (pending_gets.wait_until(wait_deadline, [&]() { return pending->ready; })) {
                return pending->response;
            }
        }
    }

    {
        auto pending_gets = this->pending_gets.handle();
        if (pending->ready) {
            return pending->response;
        }
    }

    EVLOG_AND_THROW(
        EverestTimeoutError(fmt::format("Timeout while waiting for SHM result of get({})", request.response_topic)));
}

void ShmFrameworkTransport::drain_stale_get_responses() {
    // Pull already-available SHM messages before installing the waiter, so stale responses cannot satisfy the new
    // get().
    this->run_on_main_loop([&]() {
        (void)this->shm_client->sync();
        this->drain_transport_queue();
    });
}

void ShmFrameworkTransport::drain_transport_queue() {
    this->message_queue.drain([this](const Message& message) { this->on_shm_message(message); });
}

bool ShmFrameworkTransport::register_transport_queue_events() {
    return this->message_queue.register_events(this->ev_handler,
                                               [this](const Message& message) { this->on_shm_message(message); });
}

bool ShmFrameworkTransport::unregister_transport_queue_events() {
    return this->message_queue.unregister_events(this->ev_handler);
}

std::shared_ptr<everest::lib::util::monitor<ShmFrameworkTransport::ResponseTopicLockState>>
ShmFrameworkTransport::response_topic_lock_for(const std::string& topic) {
    auto locks = this->response_topic_locks.handle();
    auto& lock = locks->locks[topic];
    if (!lock) {
        lock = std::make_shared<everest::lib::util::monitor<ResponseTopicLockState>>();
    }
    return lock;
}

void ShmFrameworkTransport::cleanup_response_topic_lock(
    const std::string& topic,
    const std::shared_ptr<everest::lib::util::monitor<ResponseTopicLockState>>& response_topic_lock) {
    auto locks = this->response_topic_locks.handle();
    auto it = locks->locks.find(topic);
    // use_count is 2 when only this local shared_ptr and the map entry still reference the topic lock.
    if (it != locks->locks.end() && it->second == response_topic_lock && response_topic_lock.use_count() == 2) {
        locks->locks.erase(it);
    }
}

bool ShmFrameworkTransport::notify_pending_get(const Message& message, const nlohmann::json& payload) {
    std::shared_ptr<PendingGet> pending;
    {
        auto pending_gets = this->pending_gets.handle();
        auto it = pending_gets->pending_gets.find(message.topic);
        if (it == pending_gets->pending_gets.end()) {
            return false;
        }
        pending = it->second;
        const auto response = payload_for_get_response(payload);
        pending->response = response;
        pending->ready = true;
    }
    this->pending_gets.notify_all();
    return true;
}

void ShmFrameworkTransport::register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler, QOS qos) {
    const auto concrete_topics = this->concrete_topics_for_handler_filter(topic);
    const auto token = handler;
    std::vector<std::string> counted_topics;
    try {
        this->message_handler.register_handler(topic, handler);
        counted_topics = this->register_concrete_handler_subscriptions(concrete_topics, qos);
        auto handlers = this->handlers.handle();
        handlers->handler_tokens[topic].push_back(std::move(handler));
    } catch (...) {
        this->message_handler.unregister_handler(topic, token);
        try {
            this->unregister_concrete_handler_subscriptions(counted_topics);
        } catch (const std::exception& e) {
            EVLOG_error << fmt::format("SHM rollback unsubscribe failed for handler topic '{}': {}", topic, e.what());
        }
        throw;
    }
}

void ShmFrameworkTransport::unregister_handler(const std::string& topic, const Token& token) {
    EVLOG_verbose << fmt::format("Unregistering SHM handler {} for {}", fmt::ptr(token.get()), topic);

    this->message_handler.unregister_handler(topic, token);

    bool unsubscribe_required = false;
    {
        auto handlers = this->handlers.handle();
        auto tokens_for_topic = handlers->handler_tokens.find(topic);
        if (tokens_for_topic != handlers->handler_tokens.end()) {
            auto& tokens = tokens_for_topic->second;
            const auto old_size = tokens.size();
            tokens.erase(std::remove(tokens.begin(), tokens.end(), token), tokens.end());
            unsubscribe_required = old_size != tokens.size() && tokens.empty();
            if (tokens.empty()) {
                handlers->handler_tokens.erase(tokens_for_topic);
            }
        }
    }

    if (unsubscribe_required) {
        this->unregister_concrete_handler_subscriptions(this->concrete_topics_for_handler_filter(topic));
    }
}

void ShmFrameworkTransport::on_shm_message(const Message& message) {
    EVLOG_verbose << "Incoming SHM message. topic: " << message.topic << " payload: " << message.payload;

    const auto& topic = message.topic;
    const auto& payload = message.payload;

    try {
        json data;
        if (topic.find(this->mqtt_everest_prefix) == 0) {
            try {
                data = json::parse(payload);
            } catch (nlohmann::detail::parse_error&) {
                EVLOG_warning << fmt::format("Could not decode json for incoming SHM topic '{}': {}", topic, payload);
                return;
            }
        } else {
            data = json(payload);
        }

        if (!this->notify_pending_get(message, data)) {
            this->message_handler.add(ParsedMessage{topic, std::move(data)});
        }
    } catch (std::exception& e) {
        EVLOG_critical << fmt::format("Caught SHM on_message std::exception: {}", e.what());
        exit(1);
    }
}

void ShmFrameworkTransport::on_shm_sequence_anomaly(const std::string& topic,
                                                    const shm::topic::sequence_validation_result& result) {
    this->sequence_anomalies.fetch_add(1, std::memory_order_relaxed);
    EVLOG_warning << fmt::format(
        "SHM sequence anomaly on topic '{}': status={} expected_sequence={} actual_sequence={} slot_index={}; "
        "dropping payload, ACKing slot, and continuing without reconnect",
        topic, sequence_status_to_string(result.status), result.expected_sequence, result.actual_sequence,
        result.slot_idx);
}

} // namespace Everest
