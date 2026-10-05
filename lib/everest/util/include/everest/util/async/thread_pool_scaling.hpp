// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "everest/util/async/monitor.hpp"
#include "everest/util/queue/thread_safe_bounded_queue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <list>
#include <optional>
#include <stdexcept>
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
        if (queue_size == 0 or not oldest_arrival.has_value()) {
            return false;
        }
        const auto wait = std::chrono::steady_clock::now() - oldest_arrival.value();
        return wait >= std::chrono::milliseconds(ThresholdMs);
    }
    /**
     * @brief Earliest time at which the task that arrived at \p oldest_arrival reaches the threshold.
     */
    static std::chrono::steady_clock::time_point next_check(std::chrono::steady_clock::time_point oldest_arrival) {
        return oldest_arrival + std::chrono::milliseconds(ThresholdMs);
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
// |  Latency     | wait >= ThresholdMs      | Balances resources with SLA.  Scales   |
// |              |                          | only if tasks sit too long in queue.   |
// +--------------+--------------------------+----------------------------------------+
// | Conservative | queue_size > (workers*2) | Prioritizes stability. Scales only     |
// |              |                          | when tasks significantly outnumber     |
// |              |                          | current worker capacity.               |
// +--------------+--------------------------+----------------------------------------+
// |  FixedSize   | queue_size >= Limit      | Rigid and predictable. Grows only      |
// |              |                          | when a specific depth limit is hit.    |
// +--------------+--------------------------+----------------------------------------+

// --- Conditions reported to the exception policy ---

/**
 * @brief Reported to the ExceptionPolicy for every failed attempt to start a worker the pool needs.
 * @details The pool keeps retrying whatever the policy does, unless the policy throws.
 */
struct worker_start_error : std::runtime_error {
    std::exception_ptr cause;              ///< What std::thread or the worker registry threw.
    std::size_t attempt;                   ///< Consecutive failed attempts at this call site, this one included.
    std::chrono::milliseconds failing_for; ///< Time since the first failed attempt of this run.

    worker_start_error(std::exception_ptr cause, std::size_t attempt, std::chrono::milliseconds failing_for) :
        std::runtime_error("thread_pool_scaling: could not start a worker"),
        cause(std::move(cause)),
        attempt(attempt),
        failing_for(failing_for) {
    }
};

/**
 * @brief Reported to the ExceptionPolicy by a submission that finds a task queued for longer than the stall
 * threshold while the pool is at its thread limit and no worker is coming.
 * @details The pool cannot resolve this itself: every worker is busy, typically blocked on a task deeper in the queue
 * than the limit allows. The report repeats with every submission while the condition holds.
 */
struct thread_limit_stall : std::runtime_error {
    std::chrono::milliseconds waited; ///< Age of the oldest queued task.
    std::size_t thread_limit;         ///< The pool's maximum worker count.

    thread_limit_stall(std::chrono::milliseconds waited, std::size_t thread_limit) :
        std::runtime_error("thread_pool_scaling: a task waits at the thread limit"),
        waited(waited),
        thread_limit(thread_limit) {
    }
};

// --- Exception Handling Policies ---

/**
 * @brief Exception policy: silently swallow exceptions (fire-and-forget semantics).
 */
struct SuppressExceptions {
    static void handle_exception([[maybe_unused]] std::exception_ptr) noexcept {
    }
};

/**
 * @brief Exception policy: rethrow. A task's exception leaves the worker thread and terminates the process; a
 * worker_start_error or thread_limit_stall leaves the submission, or terminates the process from the supervisor or
 * the destructor.
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
 *   task. Every failed attempt is reported to the ExceptionPolicy as a worker_start_error, without any pool lock
 *   held, and retried unless the policy throws: a policy with a supervisor retries after its tick, a policy
 *   without one grows only on submission, so the submission itself retries with a delay growing from 1 ms to
 *   64 ms until the start succeeds or a worker has become free for the task; the submitter blocks for that time.
 *   Destruction starts workers whatever the policy. A policy that throws ends the submission with that
 *   exception, or terminates the process from the supervisor or the destructor.
 * - A submission that finds the oldest queued task older than stall_threshold while the pool is at its thread
 *   limit with no worker coming reports a thread_limit_stall to the ExceptionPolicy. The pool cannot resolve
 *   that itself; it typically means more tasks block on each other than the limit allows.
 * - The pool's own bookkeeping after a task and during destruction does not allocate, except for starting a
 *   worker, where a failure is handled as above, and for building a report, where a failure drops the report;
 *   a std::bad_alloc from a task is handled by the ExceptionPolicy like any other exception.
 * - With a queue_limit, a submission blocks while the queue is full. Tasks must then not submit to their own
 *   pool: once every worker blocks in such a submission nobody pops, and the pool deadlocks.
 * * @tparam ScalingPolicy A policy class implementing should_grow(size_t, size_t, std::optional<time_point>) and
 * a constexpr supervisor_tick, optionally next_check(time_point); see the policy notes above.
 * * @tparam ExceptionPolicy A policy class implementing a static handle_exception(std::exception_ptr), called
 * from the worker with a task's exception, and from submission, supervisor or destructor with a
 * worker_start_error or thread_limit_stall; see the condition notes above.
 */
template <typename ScalingPolicy = LatencyScaling<10>, typename ExceptionPolicy = SuppressExceptions>
class thread_pool_scaling {
public:
    using action = std::function<void()>;

    static constexpr std::chrono::seconds default_idle_timeout{60};   ///< Surplus workers retire after this.
    static constexpr std::size_t unbounded_queue = 0;                 ///< Queue limit meaning no limit.
    static constexpr std::chrono::seconds default_stall_threshold{1}; ///< See thread_limit_stall.

    /**
     * @brief Constructs the scalable thread pool.
     * @param[in] min Minimum worker threads to keep alive.
     * @param[in] max Maximum allowed worker threads. Must be at least 1 and not below \p min, otherwise an accepted
     * task could never get a worker; the constructor throws std::invalid_argument for such a value.
     * @param[in] idle_timeout Idle duration before a surplus worker retires. Values below 1 ms are raised to 1 ms,
     * since a surplus worker only counts as waiting for a task while it blocks at the queue.
     * @param[in] queue_limit Maximum tasks allowed in the queue; \ref unbounded_queue for no limit.
     * @param[in] stall_threshold Age of the oldest queued task at which a submission reports a thread_limit_stall.
     *
     * The supervisor tick (if any) is carried by the ScalingPolicy itself; see
     * @ref LatencyScaling for an example.
     */
    thread_pool_scaling(std::size_t min, std::size_t max, std::chrono::milliseconds idle_timeout = default_idle_timeout,
                        std::size_t queue_limit = unbounded_queue,
                        std::chrono::milliseconds stall_threshold = default_stall_threshold) :
        m_min_threads(min),
        m_max_threads(max),
        m_idle_timeout(std::max(min_idle_timeout, idle_timeout)),
        m_stall_threshold(stall_threshold),
        m_action_queue(queue_limit) {
        if (max == 0 or max < min) {
            throw std::invalid_argument("thread_pool_scaling: max must be at least 1 and not below min");
        }

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

    using handle = monitor_handle<RegistryData, std::mutex>;                      ///< Alias for monitor access.
    using queue_state = typename thread_safe_bounded_queue<TrackedAction>::state; ///< Snapshot of the task queue.

    /**
     * @brief Outcome of one attempt to start a worker for a queued task.
     */
    struct start_attempt {
        bool attempted = false;     ///< A worker was needed and the thread limit allowed one.
        std::exception_ptr failure; ///< What the start threw, if it failed.

        bool started() const {
            return attempted and not failure;
        }
    };

    /**
     * @brief What a submission decided on one snapshot under the registry lock.
     */
    struct growth_decision {
        start_attempt start;                                  ///< The worker start, if the policy wanted one.
        std::optional<std::chrono::milliseconds> stalled_for; ///< Age of the oldest task, if stalled at the limit.
        bool wake_supervisor = false;                         ///< The task may wait and the supervisor sleeps.
    };

    /**
     * @brief Counts a run of consecutive failed worker starts at one call site. A round that does not fail ends the
     * run, so that an isolated failure much later starts a new one.
     */
    struct start_failures {
        std::size_t attempts = 0;
        std::chrono::steady_clock::time_point first{};

        void record() {
            if (attempts++ == 0) {
                first = std::chrono::steady_clock::now();
            }
        }

        std::chrono::milliseconds failing_for() const {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - first);
        }

        void reset() {
            attempts = 0;
        }
    };

    void spawn_worker_internal(handle& reg_h) {
        auto it = reg_h->workers.emplace(reg_h->workers.end());
        m_worker_count = reg_h->workers.size();

        m_starting_workers++;
        try {
            *it = std::thread([this, it] { run_worker(it); });
        } catch (...) {
            m_starting_workers--;
            reg_h->workers.erase(it);
            m_worker_count = reg_h->workers.size();
            throw;
        }
    }

    void run_worker(std::list<std::thread>::iterator it) {
        m_starting_workers--;
        while (true) {
            if (auto task = next_task()) {
                execute(task.value());
                join_retired_workers();
            } else if (idle_worker_leaves(it)) {
                return;
            }
        }
    }

    /**
     * @brief Supervisor loop. While tasks are queued it re-evaluates the scaling policy so that
     * time-based policies (e.g. LatencyScaling) scale up when tasks sit in the queue
     * without any new submission to trigger a check. It sleeps while the queue is empty and, for policies
     * with next_check(), until the oldest task could exceed the threshold. It exits on shutdown; the destroying
     * thread supervises the drain from then on. A failed start is reported at the top of the next round, with
     * the lock released; a round that starts nothing, because the task got a worker or none is needed, ends a
     * run of consecutive failures, as does the empty queue that puts the supervisor to sleep.
     */
    void run_supervisor(std::chrono::milliseconds tick) {
        auto not_before = std::chrono::steady_clock::time_point::min();
        start_failures failures;
        std::exception_ptr start_failure;
        while (true) {
            if (start_failure) {
                report_start_failure(failures, start_failure);
                start_failure = nullptr;
            } else {
                failures.reset();
            }
            auto reg_h = m_reg.handle();
            if (not wait_for_queued_task(reg_h, failures)) {
                return;
            }
            const auto first_arrival = m_action_queue.oldest_arrival();
            if (not first_arrival.has_value()) {
                not_before = std::chrono::steady_clock::time_point::min();
                continue;
            }
            if (not wait_until_unless_shutdown(reg_h, std::max(check_time(first_arrival.value(), tick), not_before))) {
                return;
            }
            const auto queue = m_action_queue.snapshot();
            not_before = std::chrono::steady_clock::time_point::min();
            if (queue.size == 0) {
                continue;
            }
            if (at_thread_limit(reg_h)) {
                if (not wait_below_thread_limit(reg_h)) {
                    return;
                }
                continue;
            }
            if (has_unclaimed_task(queue) and policy_wants_growth(reg_h, queue)) {
                start_failure = try_spawn_worker(reg_h);
            }
            not_before = std::chrono::steady_clock::now() + tick;
        }
    }

    /**
     * @brief Pushes a task and grows the pool for it if needed.
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

        start_failures failures;
        auto retry_delay = start_retry_delay;
        while (true) {
            const auto decision = grow_if_needed(failures);
            if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
                if (decision.wake_supervisor) {
                    m_reg.notify_all();
                }
                return;
            }
            if (not decision.start.failure) {
                return;
            }
            std::this_thread::sleep_for(retry_delay);
            retry_delay = std::min(retry_delay * 2, start_retry_delay_max);
        }
    }

    void stop_and_join() {
        announce_shutdown();
        join_supervisor();
        drain_queue();
        join_workers();
        join_retired_workers();
    }

    /**
     * @brief Waits for the next task. A surplus worker waits at most the idle timeout, a worker within the minimum
     * count without limit; both return empty once the queue is stopped and empty.
     */
    std::optional<TrackedAction> next_task() {
        const bool retirable = m_worker_count.load() > m_min_threads;
        return retirable ? m_action_queue.try_pop(m_idle_timeout) : m_action_queue.wait_and_pop();
    }

    static void execute(TrackedAction& task) {
        try {
            task.func();
        } catch (...) {
            ExceptionPolicy::handle_exception(std::current_exception());
        }
    }

    void join_retired_workers() {
        std::vector<std::thread> retired;
        {
            auto reg_h = m_reg.handle();
            retired.swap(reg_h->zombies);
        }
        join_all(retired);
    }

    template <typename Threads> static void join_all(Threads& threads) {
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    /**
     * @brief Decides for a worker that got no task whether it leaves. Under shutdown it leaves once the queue is
     * empty. Otherwise a surplus worker with an empty queue retires: its thread moves to the zombies for a later
     * join and the supervisor is woken in case it waits at the thread limit; a worker that cannot retire for lack
     * of memory stays.
     */
    bool idle_worker_leaves(std::list<std::thread>::iterator it) {
        auto reg_h = m_reg.handle();
        if (reg_h->shutdown) {
            return m_action_queue.size() == 0;
        }
        if (reg_h->workers.size() <= m_min_threads or m_action_queue.size() > 0) {
            return false;
        }
        try {
            reg_h->zombies.push_back(std::move(*it));
        } catch (const std::bad_alloc&) {
            return false;
        }
        reg_h->workers.erase(it);
        m_worker_count = reg_h->workers.size();
        if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
            m_reg.notify_all();
        }
        return true;
    }

    static void report_start_failure(start_failures& failures, std::exception_ptr cause) {
        failures.record();
        report<worker_start_error>(std::move(cause), failures.attempts, failures.failing_for());
    }

    /**
     * @brief Hands a condition to the ExceptionPolicy. Called without any pool lock held. Building the condition
     * allocates; if that fails the report is dropped, since it must not affect the task.
     */
    template <typename Condition, typename... Args> static void report(Args&&... args) {
        std::exception_ptr condition;
        try {
            condition = std::make_exception_ptr(Condition(std::forward<Args>(args)...));
        } catch (const std::bad_alloc&) {
            return;
        }
        ExceptionPolicy::handle_exception(std::move(condition));
    }

    /**
     * @brief Sleeps while the queue is empty, marked idle so that a submission wakes it. False on shutdown.
     * The queue observation that puts the supervisor to sleep also ends a run of failed starts: with nothing to
     * grow for, a later failure is a new run. Read in the predicate so that a pop between two reads cannot
     * leave the run open.
     */
    bool wait_for_queued_task(handle& reg_h, start_failures& failures) {
        reg_h->supervisor_idle = true;
        reg_h.wait([&]() {
            const bool ready = reg_h->shutdown or m_action_queue.size() > 0;
            if (not ready) {
                failures.reset();
            }
            return ready;
        });
        reg_h->supervisor_idle = false;
        return not reg_h->shutdown;
    }

    /**
     * @brief When a task that arrived at \p arrival should be checked: the policy's next_check(), or one tick from
     * now for a policy without one.
     */
    static std::chrono::steady_clock::time_point check_time(std::chrono::steady_clock::time_point arrival,
                                                            std::chrono::milliseconds tick) {
        if constexpr (has_next_check<ScalingPolicy>::value) {
            return ScalingPolicy::next_check(arrival);
        } else {
            return std::chrono::steady_clock::now() + tick;
        }
    }

    bool wait_until_unless_shutdown(handle& reg_h, std::chrono::steady_clock::time_point deadline) {
        return not reg_h.wait_until(deadline, [&]() { return reg_h->shutdown; });
    }

    bool at_thread_limit(handle& reg_h) const {
        return reg_h->workers.size() >= m_max_threads;
    }

    /**
     * @brief Sleeps while the pool is at its thread limit, woken by a retiring worker. False on shutdown.
     */
    bool wait_below_thread_limit(handle& reg_h) {
        reg_h.wait([&]() { return reg_h->shutdown or not at_thread_limit(reg_h); });
        return not reg_h->shutdown;
    }

    /**
     * @brief Whether a queued task has no worker waiting at the queue or on its way there.
     */
    bool has_unclaimed_task(const queue_state& queue) const {
        return queue.size > queue.waiting_consumers + m_starting_workers.load();
    }

    bool policy_wants_growth(handle& reg_h, const queue_state& queue) const {
        return ScalingPolicy::should_grow(reg_h->workers.size(), queue.size, queue.oldest_arrival);
    }

    std::exception_ptr try_spawn_worker(handle& reg_h) {
        try {
            spawn_worker_internal(reg_h);
        } catch (const std::system_error&) {
            return std::current_exception();
        } catch (const std::bad_alloc&) {
            return std::current_exception();
        }
        return nullptr;
    }

    /**
     * @brief Starts a worker for the submitted task if it may wait and the policy agrees, or notes that it is
     * stalled at the thread limit; then reports a failed start or the stall with the lock released.
     */
    growth_decision grow_if_needed(start_failures& failures) {
        growth_decision decision;
        {
            auto reg_h = m_reg.handle();
            const auto queue = m_action_queue.snapshot();
            const bool task_may_wait = has_unclaimed_task(queue);
            if (task_may_wait and can_grow(reg_h) and policy_wants_growth(reg_h, queue)) {
                decision.start.attempted = true;
                decision.start.failure = try_spawn_worker(reg_h);
            } else if (task_may_wait and at_thread_limit(reg_h) and stalled(queue)) {
                decision.stalled_for = age(queue);
            }
            decision.wake_supervisor = task_may_wait and reg_h->supervisor_idle;
        }
        if (decision.stalled_for) {
            report<thread_limit_stall>(decision.stalled_for.value(), m_max_threads);
        }
        if (decision.start.failure) {
            report_start_failure(failures, decision.start.failure);
        }
        return decision;
    }

    bool can_grow(handle& reg_h) const {
        return not reg_h->shutdown and not at_thread_limit(reg_h);
    }

    bool stalled(const queue_state& queue) const {
        return queue.oldest_arrival.has_value() and age(queue) >= m_stall_threshold;
    }

    /**
     * @brief Age of the oldest queued task. Only for a non-empty snapshot.
     */
    static std::chrono::milliseconds age(const queue_state& queue) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                     queue.oldest_arrival.value());
    }

    /**
     * @brief Closes the queue and sets the shutdown flag in one critical section, so that a worker returning from
     * the closed queue sees the flag at once, then wakes the supervisor and anyone waiting at the thread limit.
     */
    void announce_shutdown() {
        {
            auto reg_h = m_reg.handle();
            m_action_queue.stop();
            reg_h->shutdown = true;
        }
        m_reg.notify_all();
    }

    void join_supervisor() {
        if constexpr (ScalingPolicy::supervisor_tick.has_value()) {
            if (m_supervisor.joinable()) {
                m_supervisor.join();
            }
        }
    }

    /**
     * @brief Runs every accepted task: while the queue is not empty, starts a worker for a task nobody is coming
     * for, retrying a failed start without a deadline.
     */
    void drain_queue() {
        start_failures failures;
        while (m_action_queue.size() > 0) {
            const auto start = start_drain_worker();
            if (start.failure) {
                report_start_failure(failures, start.failure);
            } else {
                failures.reset();
            }
            std::this_thread::sleep_for(start.started() ? drain_poll_after_start : drain_poll);
        }
    }

    start_attempt start_drain_worker() {
        start_attempt start;
        auto reg_h = m_reg.handle();
        const auto queue = m_action_queue.snapshot();
        if (has_unclaimed_task(queue) and not at_thread_limit(reg_h)) {
            start.attempted = true;
            start.failure = try_spawn_worker(reg_h);
        }
        return start;
    }

    /**
     * @brief Joins every registered worker. Only after the drain, which may still add workers.
     */
    void join_workers() {
        std::list<std::thread> workers;
        {
            auto reg_h = m_reg.handle();
            workers = std::move(reg_h->workers);
            reg_h->workers.clear();
            m_worker_count = 0;
        }
        join_all(workers);
    }

    static constexpr std::chrono::milliseconds min_idle_timeout{1};       ///< Shorter would be a non-blocking pop.
    static constexpr std::chrono::milliseconds start_retry_delay{1};      ///< First delay after a failed worker start.
    static constexpr std::chrono::milliseconds start_retry_delay_max{64}; ///< Bound of the doubling retry delay.
    static constexpr std::chrono::milliseconds drain_poll{1};             ///< Destructor drain wait between checks.
    static constexpr std::chrono::milliseconds drain_poll_after_start{5}; ///< Destructor drain wait after a start.

    const std::size_t m_min_threads;                   ///< Minimum persistent thread count.
    const std::size_t m_max_threads;                   ///< Maximum allowed thread count.
    const std::chrono::milliseconds m_idle_timeout;    ///< Surplus thread idle timeout.
    const std::chrono::milliseconds m_stall_threshold; ///< Queued task age that a submission reports at the limit.

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
