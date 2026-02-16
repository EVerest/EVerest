// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "everest/util/async/monitor.hpp"
#include "everest/util/queue/thread_safe_bounded_queue.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <list>
#include <thread>
#include <vector>

namespace everest::lib::util {

/**
 * @brief A task wrapper that tracks when the task was enqueued.
 */
struct TrackedAction {
    std::function<void()> func;                    ///< The actual work to perform.
    std::chrono::steady_clock::time_point arrival; ///< Timestamp of enqueueing.

    /**
     * @brief Constructs a tracked action with the current timestamp.
     * @param[in] f The functional object to be executed.
     */
    explicit TrackedAction(std::function<void()> f) : func(std::move(f)), arrival(std::chrono::steady_clock::now()) {
    }
};

// --- Scaling Policies ---

/**
 * @brief Greedy scaling policy: grows whenever there is any backlog.
 */
struct GreedyScaling {
    /**
     * @brief Decides to grow if there is any backlog.
     * @param current_workers Number of threads currently in the registry.
     * @param queue_size Number of tasks waiting in the queue.
     * @return true if we should spawn a new thread.
     */
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            std::optional<std::chrono::steady_clock::time_point> oldest_task) {
        // If queue_size > 1, it means even if a worker is currently
        // popping, there is at least one other task that will be stuck waiting.
        return queue_size > 1;
    }
};

/**
 * @brief Conservative scaling policy: grows only when backlog is significant.
 */
struct ConservativeScaling {
    static bool should_grow(std::size_t current_workers, std::size_t queue_size,
                            [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> oldest_task) {
        return queue_size > (current_workers * 2);
    }
};

/**
 * @brief Fixed size scaling policy: grows after a specific queue depth limit is reached.
 * @tparam Limit The queue size threshold.
 */
template <std::size_t Limit> struct FixedSizeScaling {
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        return queue_size >= Limit;
    }
};

/**
 * @brief Latency-based scaling policy: grows if the oldest task has waited too long.
 * @tparam MaxWaitMs Maximum tolerable wait time in milliseconds.
 */
template <std::size_t ThresholdMs = 10> struct LatencyScaling {
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        if (queue_size <= 1 or not oldest_arrival.has_value()) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_arrival.value());
        return wait.count() > static_cast<long long>(ThresholdMs);
    }
};

// --- Thread Pool ---

// +--------------+--------------------------+----------------------------------------+
// |    POLICY    |   GROWTH TRIGGER LOGIC   |           CHARACTER / INTENT           |
// +--------------+--------------------------+----------------------------------------+
// |  Greedy      | queue_size > 1           | Minimizes latency at all costs. Scales |
// |              |                          | the moment a backlog is detected.      |
// +--------------+--------------------------+----------------------------------------+
// |  Latency     | wait > ThresholdMs       | Balances resources with SLA.  Scales   |
// |              |                          | only if tasks sit too long in queue.   |
// +--------------+--------------------------+----------------------------------------+
// | Conservative | queue_size > (workers*2) | Prioritizes stability. Scales only     |
// |              |                          | when tasks significantly outnumber     |
// |              |                          | current worker capacity.               |
// +--------------+--------------------------+----------------------------------------+
// |  FixedSize   | queue_size >= Limit      | Rigid and predictable. Grows only      |
// |              |                          | when a specific depth limit is hit.    |
// +--------------+--------------------------+----------------------------------------+

/**
 * @brief A thread pool that dynamically scales its worker count based on a policy.
 * * @details This pool maintains a minimum number of threads and expands up to a maximum
 * when the ScalingPolicy (e.g., LatencyScaling or GreedyScaling) signals that growth
 * is necessary. Idle surplus threads are automatically retired after a specified timeout.
 * * @tparam ScalingPolicy A policy class implementing should_grow(size_t, size_t, std::optional<time_point>).
 */
template <typename ScalingPolicy = LatencyScaling<10>> class thread_pool_scaling {
public:
    using action = std::function<void()>;

    /**
     * @brief Constructs the scalable thread pool.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param[in] min Minimum worker threads to keep alive.
     * @param[in] max Maximum allowed worker threads.
     * @param[in] timeout Idle duration before a surplus worker retires. Defaults to 60s.
     * @param[in] queue_limit Maximum tasks allowed in the queue. Defaults to 0 (unbounded).
     */
    template <class Rep, class Period>
    thread_pool_scaling(std::size_t min, std::size_t max,
                        std::chrono::duration<Rep, Period> timeout = std::chrono::seconds(60),
                        std::size_t queue_limit = 0) :
        m_min_threads(min),
        m_max_threads(max),
        m_idle_timeout(std::chrono::duration_cast<std::chrono::milliseconds>(timeout)),
        m_action_queue(queue_limit) {

        auto reg_h = m_reg.handle();
        for (std::size_t i = 0; i < m_min_threads; ++i) {
            spawn_worker_internal(reg_h);
        }
    }

    /**
     * @brief Destructor. Signals shutdown and joins all active worker threads.
     */
    ~thread_pool_scaling() {
        // 1. Signal shutdown and stop queue
        {
            auto reg_h = m_reg.handle();
            reg_h->shutdown = true;
        }
        m_action_queue.stop();

        // 2. Steal the active workers list
        std::list<std::thread> workers_to_join;
        {
            auto reg_h = m_reg.handle();
            workers_to_join = std::move(reg_h->workers);
        }

        // 3. Join everything in our stolen list
        for (auto& t : workers_to_join) {
            if (t.joinable()) {
                t.join();
            }
        }

        // 4. Join and clear any zombies that retired before the move
        auto reg_h = m_reg.handle();
        reap_zombies_internal(reg_h);
    }

    /**
     * @brief Submits a "fire-and-forget" task for execution.
     * @details Optimized path that avoids promise/future overhead.
     * @param[in] f The task to execute.
     * @param[in] args Arguments to pass to the task.
     */
    template <typename F, typename... Args> void run(F&& f, Args&&... args) {
        submit_to_queue(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

    /**
     * @brief Submits a task and returns a future for the result.
     * @return A std::future containing the result of the task.
     */
    template <typename F, typename... Args>
    auto operator()(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;
        auto prom = std::make_shared<std::promise<R>>();
        auto fut = prom->get_future();

        submit_to_queue([prom, bound_f = std::bind(std::forward<F>(f), std::forward<Args>(args)...)]() mutable {
            try {
                if constexpr (std::is_void_v<R>) {
                    bound_f();
                    prom->set_value();
                } else {
                    prom->set_value(bound_f());
                }
            } catch (...) {
                prom->set_exception(std::current_exception());
            }
        });
        return fut;
    }

private:
    /**
     * @brief Data structure representing the internal state of worker management.
     */
    struct RegistryData {
        std::list<std::thread> workers;  ///< List of active worker threads.
        std::deque<std::thread> zombies; ///< Threads that have exited but not yet been joined.
        bool shutdown = false;           ///< Global shutdown flag.
    };

    using handle = monitor_handle<RegistryData, std::mutex>; ///< Alias for monitor access.

    /**
     * @brief Internal helper to push tasks and trigger the scaling heuristic.
     * @param[in] func The functional object to enqueue.
     */
    void submit_to_queue(action&& func) {
        std::size_t size_after_push = m_action_queue.push(TrackedAction(std::move(func)));
        auto oldest_arrival = m_action_queue.oldest_arrival();

        if (size_after_push > 1) {
            auto reg_h = m_reg.handle();
            if (reg_h->workers.size() < m_max_threads &&
                ScalingPolicy::should_grow(reg_h->workers.size(), size_after_push, oldest_arrival)) {
                spawn_worker_internal(reg_h);
            }
        }
    }

    /**
     * @brief Spawns a new worker thread.
     * @param[in] reg_h Handle to the monitor-protected registry data.
     */
    void spawn_worker_internal(handle& reg_h) {
        reg_h->workers.emplace_back();
        auto it = std::prev(reg_h->workers.end());

        *it = std::thread([this, it]() {
            while (true) {
                auto task_opt = m_action_queue.try_pop(m_idle_timeout);

                if (task_opt) {
                    task_opt->func();
                    auto reg_h = m_reg.handle();
                    if (!reg_h->zombies.empty()) {
                        reap_zombies_internal(reg_h);
                    }
                } else {
                    auto reg_h = m_reg.handle();

                    // 1. THE CRITICAL CHECK:
                    // If shutdown is true, the destructor has already moved (or is moving)
                    // the 'workers' list. We must NOT touch 'it' or the 'workers' list.
                    if (reg_h->shutdown) {
                        return;
                    }

                    // 2. VOLUNTARY RETIREMENT:
                    // This only executes if we are NOT shutting down.
                    // Since we are holding the monitor lock and shutdown is false,
                    // we know 'it' is still valid in reg_h->workers.
                    if (reg_h->workers.size() > m_min_threads) {
                        reg_h->zombies.push_back(std::move(*it));
                        reg_h->workers.erase(it);
                        return;
                    }
                }
            }
        });
    }

    /**
     * @brief Joins and clears retired threads.
     * @param[in] reg_h Handle to the monitor-protected registry data.
     */
    void reap_zombies_internal(handle& reg_h) {
        while (!reg_h->zombies.empty()) {
            if (reg_h->zombies.front().joinable()) {
                reg_h->zombies.front().join();
            }
            reg_h->zombies.pop_front();
        }
    }

    const std::size_t m_min_threads;                ///< Minimum persistent thread count.
    const std::size_t m_max_threads;                ///< Maximum allowed thread count.
    const std::chrono::milliseconds m_idle_timeout; ///< Surplus thread idle timeout.

    thread_safe_bounded_queue<TrackedAction> m_action_queue; ///< Task queue.
    monitor<RegistryData> m_reg;                             ///< Worker registry.
};

} // namespace everest::lib::util
