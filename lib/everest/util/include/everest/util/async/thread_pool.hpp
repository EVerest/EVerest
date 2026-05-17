// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/**
 * @file thread_pool.hpp
 * @brief Simple fixed-size thread pool implementation.
 */

#pragma once

#include <everest/util/queue/thread_safe_queue.hpp>
#include <functional>
#include <future>
#include <thread>

namespace everest::lib::util {

/**
 * @brief A thread safe fixed-size pool for task execution.
 * @details This pool maintains a constant number of worker threads. It provides two
 * interfaces for task submission: operator() for tasks requiring a return value
 * (via std::future) and run() for fire-and-forget tasks.
 */
class thread_pool {
public:
    /** @brief Type definition for the tasks held in the queue. */
    using action = std::function<void()>;

    /**
     * @brief Constructs the thread pool and spawns worker threads.
     * @param thread_count The number of worker threads to maintain.
     */
    thread_pool(unsigned int thread_count) {
        auto worker_loop = [this] {
            while (auto task = m_action_queue.wait_and_pop()) {
                // Task successful, execute it while handling exceptions
                try {
                    task.value()();
                } catch (...) {
                    // Keep the worker alive even if the task fails.
                    // Exceptions for operator() are handled in the promise wrapper.
                }
            }
        };
        for (std::size_t i = 0; i < thread_count; ++i) {
            m_threads.emplace_back(worker_loop);
        }
    }

    /**
     * @brief Destructor. Signals all threads to stop and joins them.
     * @details Unblocks any threads waiting on the queue before joining.
     */
    ~thread_pool() {
        m_action_queue.stop();
        for (auto& elem : m_threads) {
            if (elem.joinable()) {
                elem.join();
            }
        }
    }

    /**
     * @brief Submits a task to the pool and returns a future for its result.
     * @tparam F The type of the callable.
     * @tparam Args The types of the arguments to pass to the callable.
     * @param f The callable to execute.
     * @param args The arguments to pass to the callable.
     * @return A std::future that will eventually contain the result of the callable.
     */
    template <typename F, typename... Args>
    auto operator()(F&& f, Args&&... args) const -> std::future<std::invoke_result_t<F, Args...>> {
        using R = std::invoke_result_t<F, Args...>;

        auto prom = std::make_shared<std::promise<R>>();
        auto fut = prom->get_future();
        enqueue_task(std::forward<F>(f), prom, std::forward<Args>(args)...);

        return fut;
    }

    /**
     * @brief Submits a "fire-and-forget" task to the pool.
     * @details This method is highly efficient as it avoids the overhead of creating
     * std::promise and std::future objects. It returns immediately after the task
     * is added to the queue.
     * @tparam F The type of the callable.
     * @tparam Args The types of the arguments to pass to the callable.
     * @param f The callable to execute.
     * @param args The arguments to pass to the callable.
     */
    template <typename F, typename... Args> void run(F&& f, Args&&... args) const {
        m_action_queue.push(std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    }

private:
    /**
     * @brief Helper to set a promise value for non-void return types.
     */
    template <typename Fut, typename F> static void promise_set_value(std::promise<Fut>& prom, F& f) {
        prom.set_value(f());
    }

    /**
     * @brief Helper to set a promise value for void return types.
     */
    template <typename F> static void promise_set_value(std::promise<void>& prom, F& f) {
        f();
        prom.set_value();
    }

    /**
     * @brief Wraps a task with a promise and enqueues it.
     */
    template <typename F, typename... Args, typename R>
    void enqueue_task(F&& f, std::shared_ptr<std::promise<R>>& prom, Args&&... args) const {
        auto bound_f = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        m_action_queue.push([prom, task_f = std::move(bound_f)]() mutable {
            try {
                promise_set_value(*prom, task_f);
            } catch (...) {
                // Ensure promise is settled with an exception if task throws
                prom->set_exception(std::current_exception());
            }
        });
    }

    /** @brief Thread safe queue for incoming tasks. */
    mutable thread_safe_queue<action> m_action_queue;

    /** @brief Container for worker thread handles. */
    std::vector<std::thread> m_threads;
};

} // namespace everest::lib::util
