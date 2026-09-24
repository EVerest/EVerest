// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <utils/error.hpp>
#include <utils/exceptions.hpp>
#include <utils/message_handler_scaling_policy.hpp>
#include <utils/types.hpp>

#include <everest/logging.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>
#include <vector>

namespace Everest {

/// Attributes log records of the calling thread to a module for the lifetime of the object. Defined in the framework
/// library so that this header stays usable with test stubs of the logging header.
class ModuleLogScope {
public:
    explicit ModuleLogScope(const std::string& name);
    ~ModuleLogScope();
    ModuleLogScope(const ModuleLogScope&) = delete;
    ModuleLogScope& operator=(const ModuleLogScope&) = delete;

private:
    std::string m_previous;
};

/// In-process typed message bus between modules that live in one process.
///
/// Variables are delivered asynchronously on one shared thread pool, in order per subscription, as one shared
/// read-only object per publish. Commands run synchronously on the caller's thread. Requirements are resolved to
/// their fulfillment with the resolver the host supplies, so the generated code only knows its own module id and
/// requirement.
///
/// The header is instantiated separately inside every module shared object, so it must not carry static state:
/// the host creates exactly one LocalBus and hands it to every module through ModuleAdapter::local.
class LocalBus {
public:
    using Payload = std::shared_ptr<const void>;
    using Resolver =
        std::function<std::vector<Fulfillment>(std::string_view module_id, std::string_view requirement_id)>;
    template <class R, class... Args> using CmdFn = std::function<R(const Args&...)>;

    explicit LocalBus(Resolver resolver, std::size_t min_threads = 2, std::size_t max_threads = 32) :
        m_resolver(std::move(resolver)), m_pool(min_threads, max_threads, std::chrono::seconds(60)) {
        m_watchdog = std::thread([this] { run_watchdog(); });
    }
    LocalBus(const LocalBus&) = delete;
    LocalBus& operator=(const LocalBus&) = delete;
    ~LocalBus() {
        stop();
        {
            const std::lock_guard<std::mutex> lock(m_watchdog_mutex);
            m_watchdog_exit = true;
        }
        m_watchdog_cv.notify_all();
        if (m_watchdog.joinable()) {
            m_watchdog.join();
        }
    }

    /// Marks a module as living in this process. Call before that module or any of its consumers registers.
    /// \p log_name is what log records carry while the module's code runs; defaults to the module id.
    void register_local_module(const std::string& module_id, const std::string& log_name = "") {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_local_modules.insert(module_id);
        m_log_names[module_id] = log_name.empty() ? module_id : log_name;
    }

    bool is_local(const std::string& module_id) const {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_local_modules.count(module_id) != 0;
    }

    /// Drops pending deliveries and refuses new ones. Call after every module returned from shutdown() and before
    /// module objects are destroyed, because queued tasks hold callbacks into module code.
    void stop() {
        m_stopped = true;
    }

    std::optional<Fulfillment> resolve(const std::string& caller_module_id, const Requirement& req) const {
        const auto fulfillments = m_resolver(caller_module_id, req.id);
        if (req.index >= fulfillments.size()) {
            return std::nullopt;
        }
        return fulfillments.at(req.index);
    }

    /// Delivers one shared object to every subscriber. Never runs a callback on the publisher's thread.
    template <class T>
    void publish(const std::string& module_id, const std::string& impl_id, const std::string& var,
                 std::shared_ptr<const T> value) {
        std::vector<std::shared_ptr<Subscription>> subscribers;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_vars.find(key(module_id, impl_id, var));
            if (it == m_vars.end()) {
                return;
            }
            subscribers = it->second;
        }
        const Payload payload = std::move(value);
        for (const auto& subscription : subscribers) {
            assert(subscription->type_name == typeid(T).name());
            post(subscription, [fn = subscription->fn, payload] { fn(payload); });
        }
    }

    /// Returns false when the provider is not a local module; the caller then uses its transport path instead.
    template <class T>
    bool subscribe(const std::string& subscriber_module_id, const Requirement& req, const std::string& var,
                   std::function<void(const T&)> callback) {
        const auto fulfillment = resolve(subscriber_module_id, req);
        if (not fulfillment.has_value() or not is_local(fulfillment->module_id)) {
            return false;
        }
        auto subscription = std::make_shared<Subscription>();
        subscription->fn = [callback = std::move(callback)](const Payload& payload) {
            callback(*std::static_pointer_cast<const T>(payload));
        };
        subscription->type_name = typeid(T).name();
        const std::lock_guard<std::mutex> lock(m_mutex);
        subscription->log_name = log_name_unlocked(subscriber_module_id);
        subscription->key = key(fulfillment->module_id, fulfillment->implementation_id, var);
        m_vars[subscription->key].push_back(std::move(subscription));
        return true;
    }

    /// Registers a command handler. Exceptions escaping the handler surface to the caller the same way the
    /// transport path reports them: CmdError subclasses pass through, everything else becomes HandlerException.
    template <class R, class... Args>
    void provide(const std::string& module_id, const std::string& impl_id, const std::string& cmd,
                 CmdFn<R, Args...> handler) {
        const std::string identifier = module_id + "->" + impl_id + "." + cmd;
        std::string log_name;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            log_name = log_name_unlocked(module_id);
        }
        auto wrapped = std::make_shared<CmdFn<R, Args...>>(
            [this, handler = std::move(handler), identifier, log_name](const Args&... args) -> R {
                const ModuleLogScope log_scope(log_name);
                const InFlightCall in_flight(*this, identifier);
                try {
                    return handler(args...);
                } catch (const CmdError&) {
                    throw;
                } catch (const std::exception& e) {
                    EVLOG_error << "Exception during handling of: " << identifier << "(): " << e.what();
                    throw HandlerException(e.what());
                } catch (...) {
                    EVLOG_error << "Unknown exception during handling of: " << identifier << "()";
                    throw HandlerException("Unknown exception");
                }
            });
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_cmds[key(module_id, impl_id, cmd)] = CmdSlot{std::move(wrapped), typeid(CmdFn<R, Args...>).name()};
    }

    /// Returns nullptr when the provider is not local or has not registered the command yet.
    template <class R, class... Args>
    std::shared_ptr<const CmdFn<R, Args...>> find_cmd(const std::string& caller_module_id, const Requirement& req,
                                                      const std::string& cmd) const {
        const auto fulfillment = resolve(caller_module_id, req);
        if (not fulfillment.has_value() or not is_local(fulfillment->module_id)) {
            return nullptr;
        }
        const std::lock_guard<std::mutex> lock(m_mutex);
        const auto it = m_cmds.find(key(fulfillment->module_id, fulfillment->implementation_id, cmd));
        if (it == m_cmds.end()) {
            return nullptr;
        }
        assert(it->second.type_name == typeid(CmdFn<R, Args...>).name());
        return std::static_pointer_cast<const CmdFn<R, Args...>>(it->second.fn);
    }

    /// Delivers \p error of \p module_id / \p impl_id to its subscribers and to every global error subscriber.
    void publish_error(const std::string& module_id, const std::string& impl_id, const error::Error& error) {
        std::vector<std::shared_ptr<Subscription>> subscribers;
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            const auto it = m_vars.find(error_key(module_id, impl_id, error.type));
            if (it != m_vars.end()) {
                subscribers = it->second;
            }
            subscribers.insert(subscribers.end(), m_global_error_subscriptions.begin(),
                               m_global_error_subscriptions.end());
        }
        const Payload payload = std::make_shared<const error::Error>(error);
        for (const auto& subscription : subscribers) {
            post(subscription, [fn = subscription->fn, payload] { fn(payload); });
        }
    }

    /// Subscribes to raise and clear of \p error_type from \p module_id / \p impl_id.
    void subscribe_error(const std::string& subscriber_module_id, const std::string& module_id,
                         const std::string& impl_id, const std::string& error_type,
                         const error::ErrorCallback& raise_callback, const error::ErrorCallback& clear_callback) {
        auto subscription = make_error_subscription(subscriber_module_id, raise_callback, clear_callback);
        const std::lock_guard<std::mutex> lock(m_mutex);
        subscription->log_name = log_name_unlocked(subscriber_module_id);
        subscription->key = error_key(module_id, impl_id, error_type);
        m_vars[subscription->key].push_back(std::move(subscription));
    }

    /// Subscribes to raise and clear of every error in the process.
    void subscribe_all_errors(const std::string& subscriber_module_id, const error::ErrorCallback& raise_callback,
                              const error::ErrorCallback& clear_callback) {
        auto subscription = make_error_subscription(subscriber_module_id, raise_callback, clear_callback);
        const std::lock_guard<std::mutex> lock(m_mutex);
        subscription->log_name = log_name_unlocked(subscriber_module_id);
        subscription->key = "*/!error/*";
        m_global_error_subscriptions.push_back(std::move(subscription));
    }

    std::size_t published_count() const {
        return m_published;
    }

    /// One line per subscription: target, subscriber, queued tasks and whether a drain is running.
    std::vector<std::string> describe_subscriptions() const {
        std::vector<std::string> lines;
        const std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& subscription : all_subscriptions_unlocked()) {
            const std::lock_guard<std::mutex> sub_lock(subscription->mutex);
            lines.push_back(subscription->key + " -> " + subscription->log_name +
                            ": queued=" + std::to_string(subscription->queue.size()) +
                            (subscription->running ? " running" : " idle"));
        }
        lines.push_back("published=" + std::to_string(m_published.load()) + " delivered=" +
                        std::to_string(m_delivered.load()) + " draining=" + std::to_string(m_draining.load()));
        return lines;
    }

private:
    /// A subscription and its strand: tasks run one at a time in the order they were posted.
    struct Subscription {
        std::function<void(const Payload&)> fn;
        std::string type_name;
        std::string log_name;
        std::string key;
        std::mutex mutex;
        std::deque<std::function<void()>> queue;
        bool running{false};
        std::chrono::steady_clock::time_point task_started{};
        bool in_task{false};
        bool warned{false};
        bool backlog_warned{false};
    };

    /// Registers a direct command call for the watchdog while the handler runs.
    class InFlightCall {
    public:
        InFlightCall(LocalBus& bus, const std::string& identifier) : m_bus(bus), m_identifier(identifier) {
            {
                const std::lock_guard<std::mutex> lock(m_bus.m_calls_mutex);
                m_bus.m_calls.emplace(m_identifier, std::chrono::steady_clock::now());
            }
            m_bus.activity_started();
        }
        ~InFlightCall() {
            {
                const std::lock_guard<std::mutex> lock(m_bus.m_calls_mutex);
                const auto range = m_bus.m_calls.equal_range(m_identifier);
                if (range.first != range.second) {
                    m_bus.m_calls.erase(range.first);
                }
            }
            m_bus.m_active--;
        }

    private:
        LocalBus& m_bus;
        std::string m_identifier;
    };

    struct CmdSlot {
        std::shared_ptr<void> fn;
        std::string type_name;
    };

    static std::string key(const std::string& module_id, const std::string& impl_id, const std::string& name) {
        return module_id + '/' + impl_id + '/' + name;
    }

    static std::string error_key(const std::string& module_id, const std::string& impl_id,
                                 const std::string& error_type) {
        return module_id + '/' + impl_id + "/!error/" + error_type;
    }

    static std::shared_ptr<Subscription> make_error_subscription(const std::string& subscriber_module_id,
                                                                 const error::ErrorCallback& raise_callback,
                                                                 const error::ErrorCallback& clear_callback) {
        auto subscription = std::make_shared<Subscription>();
        subscription->fn = [raise_callback, clear_callback](const Payload& payload) {
            const auto& error = *std::static_pointer_cast<const error::Error>(payload);
            if (error.state == error::State::Active) {
                raise_callback(error);
            } else {
                clear_callback(error);
            }
        };
        subscription->type_name = typeid(error::Error).name();
        subscription->log_name = subscriber_module_id;
        return subscription;
    }

    std::vector<std::shared_ptr<Subscription>> all_subscriptions_unlocked() const {
        std::vector<std::shared_ptr<Subscription>> subscriptions;
        for (const auto& [key, subs] : m_vars) {
            subscriptions.insert(subscriptions.end(), subs.begin(), subs.end());
        }
        subscriptions.insert(subscriptions.end(), m_global_error_subscriptions.begin(),
                             m_global_error_subscriptions.end());
        return subscriptions;
    }

    std::string log_name_unlocked(const std::string& module_id) const {
        const auto it = m_log_names.find(module_id);
        return it == m_log_names.end() ? module_id : it->second;
    }

    void post(const std::shared_ptr<Subscription>& subscription, std::function<void()> task) {
        if (m_stopped) {
            return;
        }
        m_published++;
        std::size_t backlog = 0;
        {
            const std::lock_guard<std::mutex> lock(subscription->mutex);
            subscription->queue.push_back(std::move(task));
            if (subscription->queue.size() > QUEUE_WARNING_SIZE and not subscription->backlog_warned) {
                subscription->backlog_warned = true;
                backlog = subscription->queue.size();
            }
            if (subscription->running) {
                if (backlog > 0) {
                    EVLOG_warning << "LocalBus: subscriber " << subscription->log_name << " of " << subscription->key
                                  << " has " << backlog << " queued messages";
                }
                return;
            }
            subscription->running = true;
        }
        activity_started();
        m_pool.run([this, subscription] { drain(subscription); });
    }

    void drain(const std::shared_ptr<Subscription>& subscription) {
        while (true) {
            std::function<void()> task;
            {
                const std::lock_guard<std::mutex> lock(subscription->mutex);
                subscription->in_task = false;
                if (subscription->queue.empty() or m_stopped) {
                    subscription->running = false;
                    subscription->backlog_warned = false;
                    m_active--;
                    return;
                }
                task = std::move(subscription->queue.front());
                subscription->queue.pop_front();
                subscription->task_started = std::chrono::steady_clock::now();
                subscription->in_task = true;
                subscription->warned = false;
            }
            const ModuleLogScope log_scope(subscription->log_name);
            m_draining++;
            try {
                task();
                m_delivered++;
            } catch (const std::exception& e) {
                EVLOG_error << "Exception in variable subscriber callback: " << e.what();
            } catch (...) {
                EVLOG_error << "Unknown exception in variable subscriber callback";
            }
            m_draining--;
        }
    }

    /// Counts a draining strand or a running command; wakes the watchdog if it sleeps waiting for activity.
    void activity_started() {
        m_activity_count++;
        if (m_active++ == 0 and m_watchdog_idle) {
            const std::lock_guard<std::mutex> lock(m_watchdog_mutex);
            m_watchdog_cv.notify_all();
        }
    }

    /// Warns about subscriber callbacks and direct command calls that block. It scans once per
    /// STALL_WARNING_AFTER while the bus shows any activity, and sleeps without timeout once a whole period passed
    /// without any, so steady or bursty traffic costs at most one wakeup per period and an idle bus none.
    void run_watchdog() {
        std::unique_lock<std::mutex> lock(m_watchdog_mutex);
        while (true) {
            m_watchdog_idle = true;
            m_watchdog_cv.wait(lock, [this] { return m_watchdog_exit or m_active > 0; });
            m_watchdog_idle = false;
            while (true) {
                const auto seen = m_activity_count.load();
                if (m_watchdog_cv.wait_for(lock, STALL_WARNING_AFTER, [this] { return m_watchdog_exit; })) {
                    return;
                }
                lock.unlock();
                check_stalls();
                lock.lock();
                if (m_active == 0 and m_activity_count == seen) {
                    break;
                }
            }
        }
    }

    void check_stalls() {
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::shared_ptr<Subscription>> subscriptions;
        {
            const std::lock_guard<std::mutex> bus_lock(m_mutex);
            subscriptions = all_subscriptions_unlocked();
        }
        for (const auto& subscription : subscriptions) {
            const std::lock_guard<std::mutex> sub_lock(subscription->mutex);
            const auto running_for = std::chrono::duration_cast<std::chrono::seconds>(now - subscription->task_started);
            if (subscription->in_task and running_for >= STALL_WARNING_AFTER and not subscription->warned) {
                EVLOG_warning << "LocalBus: subscriber " << subscription->log_name << " of " << subscription->key
                              << " has been running one callback for " << running_for.count() << " s";
                subscription->warned = true;
            }
        }
        const std::lock_guard<std::mutex> calls_lock(m_calls_mutex);
        for (const auto& [identifier, started] : m_calls) {
            const auto running_for = std::chrono::duration_cast<std::chrono::seconds>(now - started);
            if (running_for >= STALL_WARNING_AFTER) {
                EVLOG_warning << "LocalBus: command " << identifier << "() has been running for " << running_for.count()
                              << " s";
            }
        }
    }

    static constexpr std::chrono::seconds STALL_WARNING_AFTER{5};
    static constexpr std::size_t QUEUE_WARNING_SIZE{100};

    mutable std::mutex m_mutex;
    std::map<std::string, std::vector<std::shared_ptr<Subscription>>> m_vars;
    std::vector<std::shared_ptr<Subscription>> m_global_error_subscriptions;
    std::map<std::string, CmdSlot> m_cmds;
    std::set<std::string> m_local_modules;
    std::map<std::string, std::string> m_log_names;
    Resolver m_resolver;
    std::atomic<bool> m_stopped{false};
    std::atomic<std::size_t> m_published{0};
    std::atomic<std::size_t> m_delivered{0};
    std::atomic<std::size_t> m_draining{0};
    std::atomic<std::size_t> m_active{0};
    std::atomic<std::size_t> m_activity_count{0};
    std::atomic<bool> m_watchdog_idle{false};
    std::mutex m_calls_mutex;
    std::multimap<std::string, std::chrono::steady_clock::time_point> m_calls;
    std::mutex m_watchdog_mutex;
    std::condition_variable m_watchdog_cv;
    bool m_watchdog_exit{false};
    std::thread m_watchdog;
    // Declared last so it is destroyed first, while everything a task may touch still exists.
    everest::lib::util::thread_pool_scaling<detail::MessageHandlerScalingPolicy> m_pool;
};

} // namespace Everest
