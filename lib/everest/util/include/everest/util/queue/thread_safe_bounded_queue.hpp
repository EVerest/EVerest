// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include "simple_queue.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace everest::lib::util {

/**
 * A thread safe bounded queue implemented on top of \ref queue::simple_queue. <br>
 * The common resource \ref simple_queue is guarded by a mutex in every member function.
 * A caller blocking on \p push or \p emplace will be unblocked when space becomes available via \p pop.
 * A caller blocking on \p pop or \p try_pop will be unblocked when new data is made available via \p push or \p
 * emplace.
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
     * @brief What \ref emplace_tracked reports about the queue at the moment of the push.
     */
    struct push_result {
        size_type size;              ///< Size of the queue after the push. 0 if the queue is stopped.
        size_type waiting_consumers; ///< Consumers blocked in \p pop, \p wait_and_pop or a \p try_pop with timeout.
    };

    /**
     * @brief What \ref snapshot reports, all read under one lock hold.
     */
    struct state {
        size_type size;                                                      ///< Current number of elements.
        size_type waiting_consumers;                                         ///< Consumers blocked inside a pop.
        std::optional<std::chrono::steady_clock::time_point> oldest_arrival; ///< Arrival of the front element.
    };

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
    size_type push(value_type const& value) {
        return emplace(value);
    }

    /**
     * @brief Push new data into the queue
     * @details Blocks the caller if the queue has reached its \p max_size.
     * @param[in] value data
     * @return The size of the queue after push. Returns 0 if the queue is stopped.
     */
    size_type push(value_type&& value) {
        return emplace(std::move(value));
    }

    /**
     * @brief Construct a new element in-place at the end of the queue.
     * @details Blocks the caller if the queue has reached its \p max_size.
     * @param[in] args Arguments forwarded to construct the data element.
     * @return The size of the queue after emplace. Returns 0 if the queue is stopped.
     */
    template <class... Args> size_type emplace(Args&&... args) {
        return emplace_tracked(std::forward<Args>(args)...).size;
    }

    /**
     * @brief Construct a new element in-place at the end of the queue and report the consumers waiting for it.
     * @details Blocks the caller if the queue has reached its \p max_size. Both values of the result are taken
     * under the queue lock at the moment of the push: every counted consumer is inside a blocking pop and takes at
     * most one element before it returns, so if \p size exceeds \p waiting_consumers at least one element is not
     * claimed by a waiting consumer.
     * @param[in] args Arguments forwarded to construct the data element.
     * @return The size of the queue after the push (0 if the queue is stopped) and the number of waiting consumers.
     */
    template <class... Args> [[nodiscard]] push_result emplace_tracked(Args&&... args) {
        std::unique_lock lock(m_mtx);
        if (m_max_size > 0) {
            m_cv_producer.wait(lock, [this]() { return m_queue.size() < m_max_size || m_stop; });
        }

        if (m_stop) {
            return {0, m_waiting_consumers};
        }

        try {
            m_queue.emplace(std::forward<Args>(args)...);
        } catch (...) {
            m_cv_producer.notify_one();
            throw;
        }
        const push_result result{m_queue.size(), m_waiting_consumers};
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
    std::optional<std::chrono::steady_clock::time_point> oldest_arrival() const {
        std::lock_guard lock(m_mtx);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        return m_queue.front().arrival;
    }

    /**
     * @brief Safely returns the current number of elements in the queue.
     */
    size_type size() const {
        std::lock_guard lock(m_mtx);
        return m_queue.size();
    }

    /**
     * @brief Safely returns the number of consumers currently blocked inside a pop.
     * @details A snapshot; to decide whether a pushed element has a consumer waiting for it use the count that
     * \ref emplace_tracked reports together with the push.
     */
    size_type waiting_consumers() const {
        std::lock_guard lock(m_mtx);
        return m_waiting_consumers;
    }

    /**
     * @brief Safely returns size, waiting consumers and the oldest arrival as one consistent snapshot.
     */
    state snapshot() const {
        std::lock_guard lock(m_mtx);
        state result{m_queue.size(), m_waiting_consumers, std::nullopt};
        if (not m_queue.empty()) {
            result.oldest_arrival = m_queue.front().arrival;
        }
        return result;
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
            ++m_waiting_consumers;
            m_cv_consumer.wait(lock, wait_predicate);
            --m_waiting_consumers;
        } else if (timeout_ms > 0) {
            ++m_waiting_consumers;
            (void)m_cv_consumer.wait_for(lock, std::chrono::milliseconds(timeout_ms), wait_predicate);
            --m_waiting_consumers;
        }

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
    mutable std::mutex m_mtx;              ///< Mutex guarding access to the queue and state.
    std::condition_variable m_cv_consumer; ///< Condition variable for consumers waiting for data.
    std::condition_variable m_cv_producer; ///< Condition variable for producers waiting for space.
    bool m_stop{false};                    ///< Flag indicating the queue is shutting down.
    size_type m_waiting_consumers{0};      ///< Consumers currently inside a blocking pop. Guarded by m_mtx.
};
} // namespace everest::lib::util
