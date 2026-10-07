// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "everest/util/misc/bind.hpp"
#include "everest/util/misc/container.hpp"
#include <utils/message_handler.hpp>

#include <everest/logging.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace Everest {

namespace {
bool check_topic_matches(std::string_view full_topic, std::string_view wildcard_topic) {
    // Verbatim match
    if (full_topic == wildcard_topic) {
        return true;
    }

    std::size_t full_topic_pos = 0;
    std::size_t wildcard_topic_pos = 0;

    while (wildcard_topic_pos < wildcard_topic.size()) {
        // Always match on the first multi-level wildcard found
        if (wildcard_topic[wildcard_topic_pos] == '#') {
            return true;
        }

        std::size_t wildcard_topic_next = wildcard_topic.find('/', wildcard_topic_pos);
        std::size_t wildcard_topic_substr_len = (wildcard_topic_next == std::string_view::npos)
                                                    ? std::string_view::npos
                                                    : wildcard_topic_next - wildcard_topic_pos;
        std::string_view wildcard_topic_substr = wildcard_topic.substr(wildcard_topic_pos, wildcard_topic_substr_len);

        std::size_t full_topic_next = full_topic.find('/', full_topic_pos);
        std::size_t full_topic_substr_len =
            (full_topic_next == std::string_view::npos) ? std::string_view::npos : full_topic_next - full_topic_pos;
        std::string_view full_topic_substr = full_topic.substr(full_topic_pos, full_topic_substr_len);

        if (wildcard_topic_substr != "+" && wildcard_topic_substr != full_topic_substr) {
            return false;
        }

        if (wildcard_topic_next == std::string_view::npos) {
            return full_topic_next == std::string_view::npos;
        }

        if (full_topic_next == std::string_view::npos) {
            return wildcard_topic.substr(wildcard_topic_next + 1) == "#";
        }

        wildcard_topic_pos = wildcard_topic_next + 1;
        full_topic_pos = full_topic_next + 1;
    }

    return full_topic_pos >= full_topic.size();
}

// Pure function: collects all handlers whose registered topic (with MQTT wildcard support)
// matches the incoming topic. Kept outside MessageHandler to signal it has no dependency
// on class state; callers must hold the handler map lock before passing data.
std::vector<MessageHandler::SharedTypedHandler>
copy_shared_handler_wildcard(std::map<std::string, std::vector<MessageHandler::SharedTypedHandler>> const& data,
                             std::string const& topic) {
    std::vector<MessageHandler::SharedTypedHandler> handler_copy;
    for (const auto& [wildcard_topic, handlers_vec] : data) {
        if (check_topic_matches(topic, wildcard_topic)) {
            handler_copy.insert(handler_copy.end(), handlers_vec.begin(), handlers_vec.end());
        }
    }
    return handler_copy;
}

std::vector<MessageHandler::SharedTypedHandler> copy_shared_handler(MessageHandler::MultiHandlerMap const& data,
                                                                    std::string const& topic) {
    std::vector<MessageHandler::SharedTypedHandler> handler_copy;
    auto const* ptr = everest::lib::util::find_ptr(data, topic);
    if (ptr != nullptr) {
        handler_copy = ptr->second;
    }
    return handler_copy;
}

MessageHandler::SharedTypedHandler copy_shared_handler(MessageHandler::SingleHandlerMap const& data,
                                                       std::string const& topic) {
    MessageHandler::SharedTypedHandler handler_copy;
    auto const* ptr = everest::lib::util::find_ptr(data, topic);
    if (ptr != nullptr) {
        handler_copy = ptr->second;
    }
    return handler_copy;
}

void warn_on_high_queue_size(everest::lib::util::simple_queue<ParsedMessage> const& queue, std::string const& topic) {
    if (queue.size() >= MAX_PENDING_MESSAGES_PER_TOPIC) {
        EVLOG_warning << "Pending message queue for topic '" << topic << "' has reached the limit ("
                      << MAX_PENDING_MESSAGES_PER_TOPIC << "). Handler may be stuck or too slow.";
    }
}

template <typename Key>
void erase_handler(std::map<Key, MessageHandler::SharedTypedHandler>& data, const Key& key,
                   const MessageHandler::SharedTypedHandler& handler) {
    const auto it = data.find(key);
    if (it != data.end() && it->second == handler) {
        data.erase(it);
    }
}

void erase_handler(MessageHandler::MultiHandlerMap& data, const std::string& topic,
                   const MessageHandler::SharedTypedHandler& handler) {
    const auto it = data.find(topic);
    if (it == data.end()) {
        return;
    }
    auto& topic_handlers = it->second;
    topic_handlers.erase(std::remove(topic_handlers.begin(), topic_handlers.end(), handler), topic_handlers.end());
    if (topic_handlers.empty()) {
        data.erase(it);
    }
}

// A missing or non-string msg_type denotes an external MQTT message; an unknown msg_type yields std::nullopt.
std::optional<MqttMessageType> get_msg_type(const json& payload) {
    const auto msg_type_it = payload.find("msg_type");
    if (msg_type_it == payload.end() || !msg_type_it->is_string()) {
        return MqttMessageType::ExternalMQTT;
    }
    try {
        return string_to_mqtt_message_type(msg_type_it->get_ref<const std::string&>());
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}

} // namespace

using everest::lib::util::bind_obj;

namespace {
constexpr std::size_t START_FAILURE_LOG_EVERY = 100;
constexpr std::chrono::seconds STALL_LOG_INTERVAL{1};

std::string describe(const std::exception_ptr& cause) {
    try {
        std::rethrow_exception(cause);
    } catch (const std::exception& e) {
        return e.what();
    } catch (...) {
        return "unknown error";
    }
}
} // namespace

void MessageHandlerExceptionPolicy::handle_exception(std::exception_ptr eptr) {
    try {
        std::rethrow_exception(eptr);
    } catch (const everest::lib::util::worker_start_error& error) {
        const bool give_up = error.failing_for >= THREAD_POOL_SCALING_START_FAILURE_TOLERANCE;
        if (give_up or error.attempt == 1 or error.attempt % START_FAILURE_LOG_EVERY == 0) {
            EVLOG_warning << "Could not start a message handler thread (attempt " << error.attempt << ", failing for "
                          << error.failing_for.count() << " ms): " << describe(error.cause);
        }
        if (give_up) {
            EVLOG_error << "Starting a message handler thread kept failing for "
                        << std::chrono::duration_cast<std::chrono::seconds>(error.failing_for).count()
                        << " s, giving up";
            throw;
        }
    } catch (const everest::lib::util::thread_limit_stall& stall) {
        static std::mutex mutex;
        static std::chrono::steady_clock::time_point last_log;
        const auto now = std::chrono::steady_clock::now();
        std::lock_guard lock(mutex);
        if (now - last_log < STALL_LOG_INTERVAL) {
            return;
        }
        last_log = now;
        EVLOG_warning << "A message has waited " << stall.waited.count() << " ms for a handler thread with all "
                      << stall.thread_limit
                      << " busy. Handlers that wait for each other need "
                         "EVEREST_FRAMEWORK_THREAD_POOL_SCALING_MAX_THREAD_COUNT above their number";
    }
}

MessageHandler::MessageHandler() {
    operation_thread_pool = std::make_unique<ThreadPool>(
        THREAD_POOL_SCALING_MIN_THREAD_COUNT, THREAD_POOL_SCALING_MAX_THREAD_COUNT, THREAD_POOL_SCALING_IDLE_TIMEOUT,
        ThreadPool::unbounded_queue, THREAD_POOL_SCALING_STALL_THRESHOLD);
    operation_dispatcher_thread = std::thread([this] { run_operation_dispatcher(); });
    result_worker_thread = std::thread([this] { run_result_message_worker(); });
    external_mqtt_worker_thread = std::thread([this] { run_external_mqtt_worker(); });
}

MessageHandler::~MessageHandler() {
    stop();
}

void MessageHandler::add(const ParsedMessage& message) {
    // Once stopped (during teardown) no further messages must be dispatched: doing so could
    // enqueue work or spawn the ready thread after the worker threads have been joined and
    // while the objects the handlers reference are being destroyed.
    if (!this->running) {
        return;
    }

    EVLOG_verbose << "Adding message to queue: " << message.topic << " with data: " << message.data;

    const auto msg_type = get_msg_type(message.data);
    if (!msg_type.has_value()) {
        EVLOG_warning << "Ignoring message with unknown msg_type " << message.data.at("msg_type") << " on topic '"
                      << message.topic << "'";
        return;
    }

    if (msg_type == MqttMessageType::CmdResult || msg_type == MqttMessageType::ConfigurationResponse) {
        EVLOG_verbose << "Pushing cmd_result message to queue: " << message.data;
        result_message_queue.push(message);
    } else if (msg_type == MqttMessageType::GlobalReady) {
        const auto data_it = message.data.find("data");
        if (data_it == message.data.end()) {
            EVLOG_warning << "Ignoring GlobalReady message without data on topic '" << message.topic << "'";
            return;
        }
        const auto topic_copy = message.topic;
        const auto& data_copy = *data_it;

        // Steal the previous ready thread under the monitor lock, then join it outside.
        // Using steal-then-join avoids holding the lock during join(), which could block
        // other add() calls for an arbitrary duration while the handler runs.
        std::thread old_ready;
        {
            auto handle = ready.handle();
            old_ready = std::move(*handle);
            *handle = std::thread([this, topic_copy, data_copy] {
                SharedTypedHandler action;
                {
                    auto handle = handlers.handle();
                    action = handle->global_ready;
                }
                if (action) {
                    (*action->handler)(topic_copy, data_copy);
                }
            });
        } // release ready monitor lock before joining
        if (old_ready.joinable()) {
            old_ready.join();
        }
    } else if (msg_type == MqttMessageType::ExternalMQTT) {
        external_mqtt_message_queue.push(message);
    } else {
        operation_message_queue.push(message);
    }
}

void MessageHandler::stop() {
    if (!running.exchange(false)) {
        return; // Already stopped
    }

    operation_message_queue.stop();
    result_message_queue.stop();
    external_mqtt_message_queue.stop();

    // Join the dispatcher first: it must not be able to call schedule_operation_message()
    // (which dereferences operation_thread_pool) after the pool is destroyed.
    if (operation_dispatcher_thread.joinable()) {
        operation_dispatcher_thread.join();
    }
    // The thread_pool destructor handles stopping and joining its workers.
    operation_thread_pool.reset();
    if (result_worker_thread.joinable()) {
        result_worker_thread.join();
    }
    if (external_mqtt_worker_thread.joinable()) {
        external_mqtt_worker_thread.join();
    }
    std::thread ready_to_join;
    {
        auto handle = ready.handle();
        ready_to_join = std::move(*handle);
    }
    if (ready_to_join.joinable()) {
        ready_to_join.join();
    }
}

void MessageHandler::run_operation_dispatcher() {
    while (auto message = operation_message_queue.wait_and_pop()) {
        dispatch_operation_message(std::move(message.value()));
    }

    EVLOG_debug << "Operation dispatcher thread stopped";
}

void MessageHandler::dispatch_operation_message(ParsedMessage&& message) {
    {
        auto handle = operations.handle();
        if (everest::lib::util::exists(handle->in_flight, message.topic)) {
            auto& pending_queue = handle->pending_messages[message.topic];
            warn_on_high_queue_size(pending_queue, message.topic);
            pending_queue.push(std::move(message));
            return;
        }
        handle->in_flight.insert(message.topic);
    }

    schedule_operation_message(std::move(message));
}

void MessageHandler::schedule_operation_message(ParsedMessage&& message) {
    auto handle_operation_message_ftor = bind_obj(&MessageHandler::handle_operation_message, this);
    auto on_operation_message_done_ftor = bind_obj(&MessageHandler::on_operation_message_done, this);
    auto operation = [handle = std::move(handle_operation_message_ftor),
                      done = std::move(on_operation_message_done_ftor), message = std::move(message)]() {
        try {
            handle(message.topic, message.data);
        } catch (...) {
            done(message.topic);
            throw;
        }
        done(message.topic);
    };

    if (operation_thread_pool) {
        operation_thread_pool->run(std::move(operation));
    }
}

void MessageHandler::on_operation_message_done(const std::string& topic) {
    std::optional<ParsedMessage> next_message;
    {
        auto handle = operations.handle();
        if (!running) {
            // Shutting down: stop scheduling and release the in-flight slot.
            handle->in_flight.erase(topic);
            return;
        }

        auto opt_pending_it = everest::lib::util::find_optional(handle->pending_messages, topic);
        if (not opt_pending_it.has_value()) {
            handle->in_flight.erase(topic);
            return;
        }
        auto& pending_it = opt_pending_it.value();
        auto& pending_messages = pending_it->second;
        next_message = pending_messages.pop();
        if (pending_messages.empty()) {
            handle->pending_messages.erase(pending_it);
        }
    }

    if (!next_message.has_value()) {
        EVLOG_error << "Internal error: pending_messages queue for topic '" << topic
                    << "' was empty when expected to contain a message";
        auto handle = operations.handle();
        handle->in_flight.erase(topic);
        return;
    }
    schedule_operation_message(std::move(*next_message));
}

void MessageHandler::run_result_message_worker() {
    while (auto message = result_message_queue.wait_and_pop()) {
        handle_result_message(message->topic, message->data);
    }
    EVLOG_debug << "Cmd result worker thread stopped";
}

void MessageHandler::run_external_mqtt_worker() {
    while (auto message = external_mqtt_message_queue.wait_and_pop()) {
        handle_external_mqtt_message(message->topic, message->data);
    }
    EVLOG_debug << "External MQTT worker thread stopped";
}

void MessageHandler::handle_operation_message(const std::string& topic, const json& payload) {
    const auto msg_type = get_msg_type(payload);
    if (!msg_type.has_value()) {
        return;
    }

    auto data_it = payload.find("data");
    const json& data = (data_it != payload.end()) ? *data_it : payload;

    switch (msg_type.value()) {
    case MqttMessageType::Var:
        handle_var_message(topic, data);
        break;
    case MqttMessageType::Cmd:
        handle_cmd_message(topic, data);
        break;
    case MqttMessageType::ExternalMQTT:
        handle_external_mqtt_message(topic, data);
        break;
    case MqttMessageType::RaiseError:
    case MqttMessageType::ClearError:
        handle_error_message(topic, data);
        break;
    case MqttMessageType::ConfigurationRequest:
        handle_get_config_message(topic, data);
        break;
    case MqttMessageType::ModuleReady:
        handle_module_ready_message(topic, data);
        break;
    default:
        break;
    }
}

void MessageHandler::handle_result_message(const std::string& topic, const json& payload) {
    const auto msg_type = get_msg_type(payload);

    if (msg_type == MqttMessageType::CmdResult) {
        handle_cmd_result(topic, payload);
    } else if (msg_type == MqttMessageType::ConfigurationResponse) {
        handle_get_config_response(topic, payload);
    } else {
        EVLOG_warning << "Received invalid cmd_result message: " << payload;
    }
}

void MessageHandler::register_handler(const std::string& topic, std::shared_ptr<TypedHandler> handler) {
    switch (handler->type) {
    case HandlerType::Call: {
        auto lock = handlers.handle();
        lock->cmd[topic] = handler;
        break;
    }
    case HandlerType::Result: {
        auto lock = responses.handle();
        lock->cmd[handler->id] = handler;
        break;
    }
    case HandlerType::SubscribeVar: {
        auto lock = handlers.handle();
        lock->var[topic].push_back(handler);
        break;
    }
    case HandlerType::SubscribeError: {
        auto lock = handlers.handle();
        lock->error[topic].push_back(handler);
        break;
    }
    case HandlerType::ExternalMQTT: {
        auto lock = handlers.handle();
        lock->external_var[topic].push_back(handler);
        break;
    }
    case HandlerType::ConfigurationRequest: {
        auto lock = handlers.handle();
        lock->configuration_request[topic] = handler;
        break;
    }
    case HandlerType::ConfigurationResponse: {
        auto lock = responses.handle();
        lock->config[topic] = handler;
        break;
    }
    case HandlerType::ModuleReady: {
        auto lock = handlers.handle();
        lock->module_ready[topic] = handler;
        break;
    }
    case HandlerType::GlobalReady: {
        auto lock = handlers.handle();
        lock->global_ready = handler;
        break;
    }
    default:
        EVLOG_warning << "Unknown handler type for topic: " << topic;
        break;
    }
}

void MessageHandler::unregister_handler(const std::string& topic, const std::shared_ptr<TypedHandler>& handler) {
    if (!handler) {
        return;
    }
    switch (handler->type) {
    case HandlerType::Call: {
        auto lock = handlers.handle();
        erase_handler(lock->cmd, topic, handler);
        break;
    }
    case HandlerType::Result: {
        auto lock = responses.handle();
        erase_handler(lock->cmd, handler->id, handler);
        break;
    }
    case HandlerType::SubscribeVar: {
        auto lock = handlers.handle();
        erase_handler(lock->var, topic, handler);
        break;
    }
    case HandlerType::SubscribeError: {
        auto lock = handlers.handle();
        erase_handler(lock->error, topic, handler);
        break;
    }
    case HandlerType::ExternalMQTT: {
        auto lock = handlers.handle();
        erase_handler(lock->external_var, topic, handler);
        break;
    }
    case HandlerType::ConfigurationRequest: {
        auto lock = handlers.handle();
        erase_handler(lock->configuration_request, topic, handler);
        break;
    }
    case HandlerType::ConfigurationResponse: {
        auto lock = responses.handle();
        erase_handler(lock->config, topic, handler);
        break;
    }
    case HandlerType::ModuleReady: {
        auto lock = handlers.handle();
        erase_handler(lock->module_ready, topic, handler);
        break;
    }
    case HandlerType::GlobalReady: {
        auto lock = handlers.handle();
        if (lock->global_ready == handler) {
            lock->global_ready.reset();
        }
        break;
    }
    default:
        break;
    }
}

// Private message handler methods
void MessageHandler::handle_var_message(const std::string& topic, const json& data) {
    const auto data_it = data.find("data");
    if (data_it == data.end()) {
        EVLOG_warning << "Ignoring var message without data on topic '" << topic << "'";
        return;
    }

    std::vector<SharedTypedHandler> handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler(handle->var, topic);
    }

    for (const auto& handler : handler_copy) {
        (*handler->handler)(topic, *data_it);
    }
}

void MessageHandler::handle_cmd_message(const std::string& topic, const json& data) {
    SharedTypedHandler handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler(handle->cmd, topic);
    }

    if (handler_copy) {
        (*handler_copy->handler)(topic, data);
    }
}

void MessageHandler::handle_external_mqtt_message(const std::string& topic, const json& data) {
    std::vector<SharedTypedHandler> handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler_wildcard(handle->external_var, topic);
    }

    for (const auto& handler : handler_copy) {
        (*handler->handler)(topic, data);
    }
}

void MessageHandler::handle_error_message(const std::string& topic, const json& data) {
    std::vector<SharedTypedHandler> handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler_wildcard(handle->error, topic);
    }

    for (const auto& handler : handler_copy) {
        (*handler->handler)(topic, data);
    }
}

void MessageHandler::handle_get_config_message(const std::string& topic, const json& data) {
    SharedTypedHandler handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler(handle->configuration_request, topic);
    }

    if (handler_copy) {
        (*handler_copy->handler)(topic, data);
    }
}

void MessageHandler::handle_module_ready_message(const std::string& topic, const json& data) {
    SharedTypedHandler handler_copy;
    {
        auto handle = handlers.handle();
        handler_copy = copy_shared_handler(handle->module_ready, topic);
    }

    if (handler_copy) {
        (*handler_copy->handler)(topic, data);
    }
}

void MessageHandler::handle_cmd_result(const std::string& topic, const json& payload) {
    static const json::json_pointer data_ptr{"/data/data"};
    static const json::json_pointer id_ptr{"/data/data/id"};
    if (!payload.contains(id_ptr) || !payload.at(id_ptr).is_string()) {
        EVLOG_warning << "Ignoring cmd result without id on topic '" << topic << "'";
        return;
    }
    const auto& data = payload.at(data_ptr);
    const auto& id = payload.at(id_ptr).get_ref<const std::string&>();

    std::shared_ptr<TypedHandler> handler_copy;
    {
        auto handle = responses.handle();
        auto it = handle->cmd.find(id);
        if (it != handle->cmd.end()) {
            handler_copy = it->second;
            handle->cmd.erase(it);
        }
    }

    if (handler_copy) {
        (*handler_copy->handler)(topic, data);
    }
}

void MessageHandler::handle_get_config_response(const std::string& topic, const json& payload) {
    const auto data_it = payload.find("data");
    if (data_it == payload.end()) {
        EVLOG_warning << "Ignoring configuration response without data on topic '" << topic << "'";
        return;
    }

    // Each request waits for exactly one response, so the handler is consumed by the first response on its topic.
    std::shared_ptr<TypedHandler> handler_copy;
    {
        auto handle = responses.handle();
        const auto it = handle->config.find(topic);
        if (it != handle->config.end()) {
            handler_copy = it->second;
            handle->config.erase(it);
        }
    }
    if (!handler_copy) {
        EVLOG_debug << "Ignoring configuration response without pending request on topic '" << topic << "'";
        return;
    }
    (*handler_copy->handler)(topic, *data_it);
}

} // namespace Everest
