// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "everest/util/async/monitor.hpp"
#include "everest/util/queue/thread_safe_bounded_queue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <list>
#include <optional>
#include <system_error>
#include <thread>
#include <type_traits>
#include <variant>
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

// A policy advertises whether it needs a background supervisor via a single
// constexpr: `supervisor_tick`. `std::nullopt` means no supervisor: should_grow()
// is evaluated on every submission only. A value means "re-evaluate should_grow()
// for queued tasks without new submissions, at most every <tick> ms". The
// supervisor sleeps while the queue is empty, is woken only for a task that
// queues behind busy workers, and sleeps at the thread limit until a worker
// retires. A policy may additionally provide
//   static std::chrono::steady_clock::time_point next_check(std::chrono::steady_clock::time_point oldest_arrival);
// so that the supervisor sleeps until that time instead of waking every tick.
// The pool relies on:
//   - next_check() is no later than the first time should_grow() can return true
//     for a queue whose oldest task arrived at oldest_arrival (an early value is
//     harmless: the supervisor then retries every tick),
//   - next_check() is non-decreasing in oldest_arrival, so that a task submitted
//     while the supervisor sleeps towards a deadline never falls due earlier.

/**
 * @brief Conservative scaling policy: grows only when backlog is significant.
 */
struct ConservativeScaling {
    static constexpr std::optional<std::chrono::milliseconds> supervisor_tick = std::nullopt;
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
    static constexpr std::optional<std::chrono::milliseconds> supervisor_tick = std::nullopt;
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        return queue_size >= Limit;
    }
};

/**
 * @brief Greedy scaling policy: grows whenever a submitted task would have to wait for a worker.
 * @details Needs no supervisor. The pool asks should_grow() only when the submitted task has no worker waiting
 * or starting for it, so the queue never holds a task that nobody will take unless the pool is at its maximum;
 * the pool size follows the number of simultaneously outstanding tasks.
 */
struct GreedyScaling {
    static constexpr std::optional<std::chrono::milliseconds> supervisor_tick = std::nullopt;
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            [[maybe_unused]] std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        return queue_size > 0;
    }
};

/**
 * @brief Latency-based scaling policy: grows if the oldest task has waited too long.
 * @tparam ThresholdMs Maximum tolerable wait time in milliseconds.
 * @tparam TickMs Back-off, in milliseconds, before the supervisor re-evaluates a queued task it just grew for or
 * decided not to grow for. With next_check() this is not a cadence: an idle pool, or a task that an idle worker
 * takes, causes no supervisor wakeup at all.
 */
template <std::size_t ThresholdMs = 10, std::size_t TickMs = 5> struct LatencyScaling {
    static constexpr std::optional<std::chrono::milliseconds> supervisor_tick = std::chrono::milliseconds(TickMs);
    static bool should_grow([[maybe_unused]] std::size_t current_workers, std::size_t queue_size,
                            std::optional<std::chrono::steady_clock::time_point> oldest_arrival) {
        if (queue_size < 1 or not oldest_arrival.has_value()) {
            return false;
        }
        const auto now = std::chrono::steady_clock::now();
        const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(now - oldest_arrival.value());
        return wait.count() > static_cast<long long>(ThresholdMs);
    }
    /**
     * @brief Earliest time at which the task that arrived at \p oldest_arrival exceeds the threshold.
     */
    static std::chrono::steady_clock::time_point next_check(std::chrono::steady_clock::time_point oldest_arrival) {
        return oldest_arrival + std::chrono::milliseconds(ThresholdMs + 1);
    }
};

/**
 * @brief Detects whether a scaling policy provides next_check().
 */
template <typename Policy, typename = void> struct has_next_check : std::false_type {};
template <typename Policy>
struct has_next_check<Policy,
                      std::void_t<decltype(Policy::next_check(std::declval<std::chrono::steady_clock::time_point>()))>>
    : std::true_type {};

// --- Thread Pool ---

// +--------------+--------------------------+----------------------------------------+
// |    POLICY    |   GROWTH TRIGGER LOGIC   |           CHARACTER / INTENT           |
// +--------------+--------------------------+----------------------------------------+
// |  Greedy      | task has no free worker  | Never lets a task wait for a busy      |
// |              |                          | worker. Pool follows the number of     |
// |              |                          | outstanding tasks, up to the limit.    |
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

// --- Exception Handling Policies ---

/**
 * @brief Exception policy: silently swallow exceptions (fire-and-forget semantics).
 */
struct SuppressExceptions {
    static void handle_exception([[maybe_unused]] std::exception_ptr) noexcept {
    }
};

/**
 * @brief Exception policy: rethrow from the worker thread, terminating the process if uncaught.
 */
struct RethrowExceptions {
    [[noreturn]] static void handle_exception(std::exception_ptr eptr) {
        std::rethrow_exception(eptr);
    }
};

/**
 * @brief A thread pool that dynamically scales its worker count based on a policy.
 * * @details This pool maintains a minimum number of threads and expands up to a maximum
 * when the ScalingPolicy (e.g., LatencyScaling or GreedyScaling) signals that growth
 * is necessary. Idle surplus threads are automatically retired after a specified timeout.
 *
 * Failure behaviour:
 * - should_grow() is asked on a submission only if the submitted task may wait, that is, if the queue is now
 *   longer than the number of workers waiting at it or about to; a task an idle worker takes at once never grows
 *   the pool, whatever the policy.
 * - A task is accepted once submit returns. Destruction runs every accepted task on pool workers within the
 *   concurrency limit, retrying failed worker starts without a deadline (see the destructor). A task submitted
 *   from within a task after destruction began is rejected, which a future observes as broken_promise.
 * - Failure to start a worker (std::system_error or std::bad_alloc from std::thread or the registry) never loses a
 *   task and never escapes to a task or a submitter. A policy with a supervisor retries after its tick. A policy
 *   without one grows only on submission, so the submission itself retries a failed start with a delay growing
 *   from 1 ms to 64 ms until the start succeeds or a worker has become free for the task; the submitter blocks
 *   for that time. Destruction starts workers whatever the policy.
 * - The pool's own bookkeeping after a task and during destruction does not allocate, except for starting a
 *   worker, where a failure is handled as above; a std::bad_alloc from a task is handled by the ExceptionPolicy
 *   like any other exception.
 * - With a queue_limit, a submission blocks while the queue is full. Tasks must then not submit to their own
 *   pool: once every worker blocks in such a submission nobody pops, and the pool deadlocks.
 * * @tparam ScalingPolicy A policy class implementing should_grow(size_t, size_t, std::optional<time_point>) and
 * a constexpr supervisor_tick, optionally next_check(time_point); see the policy notes above.
 * * @tparam ExceptionPolicy A policy class implementing a static handle_exception() called inside the catch block.
 */
template <typename ScalingPolicy = LatencyScaling<10>, typename ExceptionPolicy = SuppressExceptions>
class thread_pool_scaling {
public:
    using action = std::function<void()>;

    /**
     * @brief Constructs the scalable thread pool.
     * @tparam Rep The representation type of the duration.
     * @tparam Period The period type of the duration.
     * @param[in] min Minimum worker threads to keep alive.
     * @param[in] max Maximum allowed worker threads. A value below \p min is raised to \p min.
     * @param[in] timeout Idle duration before a surplus worker retires. Defaults to 60s.
     * @param[in] queue_limit Maximum tasks allowed in the queue. Defaults to 0 (unbounded).
     *
     * The supervisor tick (if any) is carried by the ScalingPolicy itself; see
     * @ref LatencyScaling for an example.
     */
    template <class Rep, class Period>
    thread_pool_scaling(std::size_t min, std::size_t max,
                        std::chrono::duration<Rep, Period> timeout = std::chrono::seconds(60),
                        std::size_t queue_limit = 0) :
        m_min_threads(min),
        m_max_threads(std::max(min, max)),
        m_idle_timeout(std::chrono::duration_cast<std::chrono::milliseconds>(timeout)),
        m_action_queue(queue_limit) {

        try {
            {
                auto reg_h = m_reg.handle();
                for (std::size_t i = 0; i < m_min_threads; ++i) {
                    spawn_worker_internal(reg_h);
                }
            }
            if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
                m_supervisor = std::thread([this] { run_supervisor(*ScalingPolicy::supervisor_tick); });
            }
        } catch (...) {
            stop_and_join();
            throw;
        }
    }

    /**
     * @brief Destructor. Rejects new tasks, runs the tasks already accepted, then joins all threads.
     * @details Only pool workers run the accepted tasks, so the concurrency limit holds during destruction. The
     * destroying thread supervises the drain for every policy: a queued task that no worker waits for gets a
     * worker of its own, up to the maximum, with a failed start retried until it succeeds. Destruction has no
     * deadline: it waits as long as a worker cannot be started or a dependency needs more workers than the
     * maximum.
     */
    ~thread_pool_scaling() {
        stop_and_join();
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
        std::list<std::thread> workers;   ///< List of active worker threads.
        std::vector<std::thread> zombies; ///< Threads that have exited but not yet been joined.
        bool shutdown = false;            ///< Global shutdown flag.
        bool supervisor_idle = false;     ///< Supervisor sleeps until a task is queued.
    };

    using handle = monitor_handle<RegistryData, std::mutex>; ///< Alias for monitor access.

    /**
     * @brief Shutdown sequence shared by the destructor and a failing constructor.
     */
    void stop_and_join() {
        {
            auto reg_h = m_reg.handle();
            m_action_queue.stop();
            reg_h->shutdown = true;
        }
        m_reg.notify_all();

        if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
            if (m_supervisor.joinable()) {
                m_supervisor.join();
            }
        }

        while (m_action_queue.size() > 0) {
            bool spawned = false;
            {
                auto reg_h = m_reg.handle();
                const auto queue = m_action_queue.snapshot();
                if (queue.size > 0 and queue.waiting_consumers + m_starting_workers.load() == 0 and
                    reg_h->workers.size() < m_max_threads) {
                    try {
                        spawn_worker_internal(reg_h);
                        spawned = true;
                    } catch (const std::system_error&) {
                    } catch (const std::bad_alloc&) {
                    }
                }
            }
            std::this_thread::sleep_for(spawned ? std::chrono::milliseconds(5) : std::chrono::milliseconds(1));
        }

        std::list<std::thread> workers_to_join;
        {
            auto reg_h = m_reg.handle();
            workers_to_join = std::move(reg_h->workers);
            reg_h->workers.clear();
            m_worker_count = 0;
        }

        for (auto& t : workers_to_join) {
            if (t.joinable()) {
                t.join();
            }
        }

        std::vector<std::thread> zombies_to_join;
        {
            auto reg_h = m_reg.handle();
            zombies_to_join.swap(reg_h->zombies);
        }
        for (auto& t : zombies_to_join) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    /**
     * @brief Internal helper to push tasks and trigger the scaling heuristic.
     * @details The push reports whether a consumer blocked at that moment takes the task; if so nothing else is
     * needed. Otherwise the growth and wakeup decisions are taken under the registry lock on one consistent
     * snapshot of the queue together with the starting workers, which only change under that lock or by a
     * worker about to register at the queue, so that a task popped in the meantime does not grow the pool and
     * a worker started for another task is not mistaken for one coming for this task. For a policy without a
     * supervisor a failed worker start is retried here with a growing delay, since nothing else would, until
     * the start succeeds or the task no longer needs a worker; each retry decides on a fresh snapshot.
     * @param[in] func The functional object to enqueue.
     */
    void submit_to_queue(action&& func) {
        const auto pushed = m_action_queue.emplace_tracked(TrackedAction(std::move(func)));
        if (pushed.size == 0 or pushed.size <= pushed.waiting_consumers) {
            return;
        }

        std::chrono::milliseconds retry_delay{1};
        while (true) {
            bool start_failed = false;
            bool wake_supervisor = false;
            {
                auto reg_h = m_reg.handle();
                const auto queue = m_action_queue.snapshot();
                const bool task_may_wait = queue.size > queue.waiting_consumers + m_starting_workers.load();
                if (task_may_wait and not reg_h->shutdown and reg_h->workers.size() < m_max_threads and
                    ScalingPolicy::should_grow(reg_h->workers.size(), queue.size, queue.oldest_arrival)) {
                    try {
                        spawn_worker_internal(reg_h);
                    } catch (const std::system_error&) {
                        start_failed = true;
                    } catch (const std::bad_alloc&) {
                        start_failed = true;
                    }
                }
                wake_supervisor = task_may_wait and reg_h->supervisor_idle;
            }
            if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
                if (wake_supervisor) {
                    m_reg.notify_all();
                }
            }
            if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
                return;
            }
            if (not start_failed) {
                return;
            }
            std::this_thread::sleep_for(retry_delay);
            retry_delay = std::min(retry_delay * 2, std::chrono::milliseconds(64));
        }
    }

    /**
     * @brief Spawns a new worker thread.
     * @param[in] reg_h Handle to the monitor-protected registry data.
     */
    void spawn_worker_internal(handle& reg_h) {
        reg_h->workers.emplace_back();
        m_worker_count = reg_h->workers.size();
        auto it = std::prev(reg_h->workers.end());

        m_starting_workers++;
        try {
            *it = start_worker(it);
        } catch (...) {
            m_starting_workers--;
            reg_h->workers.erase(it);
            m_worker_count = reg_h->workers.size();
            throw;
        }
    }

    /**
     * @brief Starts the thread of the worker registered at \p it. Throws std::system_error if no thread can be
     * started.
     */
    std::thread start_worker(std::list<std::thread>::iterator it) {
        return std::thread([this, it]() {
            m_starting_workers--;
            while (true) {
                const bool retirable = m_worker_count.load() > m_min_threads;
                auto task_opt = retirable ? m_action_queue.try_pop(m_idle_timeout) : m_action_queue.wait_and_pop();
                if (task_opt) {
                    try {
                        task_opt->func();
                    } catch (...) {
                        ExceptionPolicy::handle_exception(std::current_exception());
                    }
                    std::vector<std::thread> zombies_to_join;
                    {
                        auto reg_h = m_reg.handle();
                        if (!reg_h->zombies.empty()) {
                            zombies_to_join.swap(reg_h->zombies);
                        }
                    }
                    for (auto& t : zombies_to_join) {
                        if (t.joinable()) {
                            t.join();
                        }
                    }
                } else {
                    auto reg_h = m_reg.handle();

                    if (reg_h->shutdown) {
                        if (m_action_queue.size() > 0) {
                            continue;
                        }
                        return;
                    }

                    if (reg_h->workers.size() > m_min_threads and m_action_queue.size() == 0) {
                        try {
                            reg_h->zombies.push_back(std::move(*it));
                        } catch (const std::bad_alloc&) {
                            continue;
                        }
                        reg_h->workers.erase(it);
                        m_worker_count = reg_h->workers.size();
                        if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
                            m_reg.notify_all();
                        }
                        return;
                    }
                }
            }
        });
    }

    /**
     * @brief Supervisor loop. While tasks are queued it re-evaluates the scaling policy so that
     * time-based policies (e.g. LatencyScaling) scale up when tasks sit in the queue
     * without any new submission to trigger a check. It sleeps while the queue is empty and, for policies
     * with next_check(), until the oldest task could exceed the threshold. It exits on shutdown; the destroying
     * thread supervises the drain from then on.
     */
    void run_supervisor(std::chrono::milliseconds tick) {
        auto not_before = std::chrono::steady_clock::time_point::min();
        while (true) {
            auto reg_h = m_reg.handle();
            reg_h->supervisor_idle = true;
            reg_h.wait([&]() { return reg_h->shutdown or m_action_queue.size() > 0; });
            reg_h->supervisor_idle = false;
            if (reg_h->shutdown) {
                return;
            }
            const auto first_arrival = m_action_queue.oldest_arrival();
            if (not first_arrival.has_value()) {
                continue;
            }

            auto deadline = std::chrono::steady_clock::now() + tick;
            if constexpr (has_next_check<ScalingPolicy>::value) {
                deadline = ScalingPolicy::next_check(first_arrival.value());
            }
            deadline = std::max(deadline, not_before);
            if (reg_h.wait_until(deadline, [&]() { return reg_h->shutdown; })) {
                return;
            }

            const std::size_t queue_size = m_action_queue.size();
            not_before = std::chrono::steady_clock::time_point::min();
            if (queue_size == 0) {
                continue;
            }
            const auto oldest_arrival = m_action_queue.oldest_arrival();
            if (reg_h->workers.size() >= m_max_threads) {
                reg_h.wait([&]() { return reg_h->shutdown or reg_h->workers.size() < m_max_threads; });
                if (reg_h->shutdown) {
                    return;
                }
            } else if (ScalingPolicy::should_grow(reg_h->workers.size(), queue_size, oldest_arrival)) {
                try {
                    spawn_worker_internal(reg_h);
                } catch (const std::system_error&) {
                } catch (const std::bad_alloc&) {
                }
                not_before = std::chrono::steady_clock::now() + tick;
            } else {
                not_before = std::chrono::steady_clock::now() + tick;
            }
        }
    }

    const std::size_t m_min_threads;                ///< Minimum persistent thread count.
    const std::size_t m_max_threads;                ///< Maximum allowed thread count.
    const std::chrono::milliseconds m_idle_timeout; ///< Surplus thread idle timeout.

    thread_safe_bounded_queue<TrackedAction> m_action_queue; ///< Task queue.
    std::atomic<std::size_t> m_worker_count{0};     ///< Size of the worker registry, readable without its lock.
    std::atomic<std::size_t> m_starting_workers{0}; ///< Workers started but not yet waiting at the queue.
    monitor<RegistryData> m_reg;                    ///< Worker registry.
    /// Background scaling supervisor. Only materialized as a real `std::thread`
    /// for policies whose `supervisor_tick` has a value; otherwise collapses to
    /// a `std::monostate` so non-supervisor pools don't carry a dead thread handle.
    std::conditional_t<ScalingPolicy::supervisor_tick.has_value(), std::thread, std::monostate> m_supervisor;
};

} // namespace everest::lib::util
