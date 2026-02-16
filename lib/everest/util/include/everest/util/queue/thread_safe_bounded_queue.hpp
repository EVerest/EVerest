// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "simple_queue.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace everest::lib::util {

/**
 * A thread safe bounded queue implemented on top of \ref queue::simple_queue. <br>
 * The common resource \ref simple_queue is guarded by a mutex in every member function.
 * A caller blocking on \p push will be unblocked when space becomes available via \p pop.
 * A caller blocking on \p pop or \p try_pop will be unblocked when new data is made available via \p push.
 * @tparam T Datatype held by the queue
 */
template <class T> class thread_safe_bounded_queue {
public:
    /**
     * @var value_type
     * @brief Inherited type definition.
     */
    using value_type = typename simple_queue<T>::value_type;

    /**
     * @var size_type
     * @brief Inherited size type definition.
     */
    using size_type = typename simple_queue<T>::size_type;

    /**
     * @brief Constructor for the bounded queue.
     * @param[in] max_size The maximum number of elements allowed in the queue.
     * A value of 0 indicates an unbounded queue.
     */
    explicit thread_safe_bounded_queue(size_type max_size = 0) : m_max_size(max_size) {
    }

    /**
     * @brief Push new data into the queue
     * @details Blocks the caller if the queue has reached its \p max_size.
     * @param[in] value data
     * @return The size of the queue after push. Returns 0 if the queue is stopped.
     */
    size_type push(const value_type& value) {
        std::unique_lock lock(m_mtx);
        if (m_max_size > 0) {
            m_cv_producer.wait(lock, [this]() { return m_queue.size() < m_max_size || m_stop; });
        }

        if (m_stop) {
            return 0;
        }

        m_queue.push(value);
        auto result = m_queue.size();
        lock.unlock();
        m_cv_consumer.notify_one();
        return result;
    }

    /**
     * @brief Push new data into the queue
     * @details Blocks the caller if the queue has reached its \p max_size.
     * @param[in] value data
     * @return The size of the queue after push. Returns 0 if the queue is stopped.
     */
    size_type push(value_type&& value) {
        std::unique_lock lock(m_mtx);
        if (m_max_size > 0) {
            m_cv_producer.wait(lock, [this]() { return m_queue.size() < m_max_size || m_stop; });
        }

        if (m_stop) {
            return 0;
        }

        m_queue.push(std::forward<value_type>(value));
        auto result = m_queue.size();
        lock.unlock();
        m_cv_consumer.notify_one();
        return result;
    }

    /**
     * @brief Try to get an element from the queue.
     * @details Returns immediately.
     * @return An element from the queue, if one is available. \p std::nullopt otherwise
     */
    std::optional<value_type> try_pop() {
        return pop_impl(0);
    }

    /**
     * @brief Try to get an element from the queue.
     * @details Returns as soon as data is availble or after timeout.
     * @param[in] timeout as <a href="https://en.cppreference.com/w/cpp/chrono/duration">std::chrono::duration</a>.
     * Smallest unit acceptable is milliseconds.
     * @return An element from the queue, if one is available. \p std::nullopt otherwise
     */
    template <class Rep, class Period> std::optional<value_type> try_pop(std::chrono::duration<Rep, Period> timeout) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(timeout);
        return pop_impl(ms.count());
    }

    /**
     * @brief Get an element from the queue
     * @details Only returns, when data is available. Implicitly throws on stop().
     * @return An element from the queue.
     */
    value_type pop() {
        return pop_impl(-1).value();
    }

    /**
     * @brief Get an element from the queue
     * @details Only returns, when data is available, or the queue is stopped.
     * @return An element from the queue. Empty optional if stopped.
     */
    std::optional<value_type> wait_and_pop() {
        return pop_impl(-1);
    }

    /**
     * @brief Signals that no more items will be pushed and unblocks all waiting consumers and producers.
     * @details Remaining items in the queue can still be popped until it is empty.
     */
    void stop() {
        std::unique_lock lock(m_mtx);
        m_stop = true;
        lock.unlock();
        m_cv_consumer.notify_all();
        m_cv_producer.notify_all();
    }

    /**
     * @brief Safely returns the arrival time of the oldest task.
     * @return std::optional containing the time_point of the oldest task,
     * or std::nullopt if the queue is empty.
     */
    std::optional<std::chrono::steady_clock::time_point> oldest_arrival() {
        std::lock_guard lock(m_mtx);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        return m_queue.front().arrival;
    }

private:
    /**
     * @brief Internal implementation of the pop logic.
     * @param[in] timeout_ms Timeout in milliseconds. -1 for infinite wait, 0 for immediate return.
     * @return An optional containing the popped value or std::nullopt.
     */
    std::optional<value_type> pop_impl(int timeout_ms) {
        std::unique_lock lock(m_mtx);
        auto wait_predicate = [this]() { return not m_queue.empty() or m_stop; };

        if (timeout_ms < 0) {
            m_cv_consumer.wait(lock, wait_predicate);
        } else if (timeout_ms > 0) {
            (void)m_cv_consumer.wait_for(lock, std::chrono::milliseconds(timeout_ms), wait_predicate);
        }

        // if the queue is still empty, we return a nullopt. Note that this would be implicitly
        // handled by simple_queue::pop, but it is added here to be more explcit
        if (m_queue.empty()) {
            return std::nullopt;
        }

        auto result = m_queue.pop();
        lock.unlock();
        m_cv_producer.notify_one();
        return result;
    }

    simple_queue<T> m_queue;               ///< The underlying non-thread-safe container.
    const size_type m_max_size;            ///< Maximum capacity of the queue.
    std::mutex m_mtx;                      ///< Mutex guarding access to the queue and state.
    std::condition_variable m_cv_consumer; ///< Condition variable for consumers waiting for data.
    std::condition_variable m_cv_producer; ///< Condition variable for producers waiting for space.
    bool m_stop{false};                    ///< Flag indicating the queue is shutting down.
};
} // namespace everest::lib::util
