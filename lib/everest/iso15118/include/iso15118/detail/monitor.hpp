// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <mutex>
#include <utility>

#include <iso15118/detail/mutex.hpp>

namespace iso15118::detail {

/**
 * \brief RAII guard providing locked access to the value held by a Monitor.
 *
 * Movable, not copyable: exactly one MonitorHandle at a time can be holding the lock on a given
 * Monitor. The lock is released when the handle is destroyed.
 */
template <class T> class MonitorHandle {
public:
    MonitorHandle(T& obj, Mutex& mtx) : obj_(obj), lock_(mtx) {
    }

    MonitorHandle(const MonitorHandle&) = delete;
    MonitorHandle& operator=(const MonitorHandle&) = delete;
    MonitorHandle(MonitorHandle&&) = default;
    MonitorHandle& operator=(MonitorHandle&&) = default;
    ~MonitorHandle() = default;

    T& operator*() {
        return obj_;
    }
    T* operator->() {
        return &obj_;
    }

private:
    T& obj_;
    std::unique_lock<Mutex> lock_;
};

/**
 * \brief Bundles a value of type T with a Mutex protecting it; a lightweight, non-condition-
 * variable-capable stand-in for everest::lib::util::monitor<T> (see include/iso15118/detail/mutex.hpp
 * for why this library doesn't use std::mutex-based utilities directly). Access is only possible
 * through the RAII handle returned by handle().
 */
template <class T> class Monitor {
public:
    Monitor() = default;
    explicit Monitor(T&& obj) : obj_(std::move(obj)) {
    }
    template <class... Args> explicit Monitor(Args&&... args) : obj_(std::forward<Args>(args)...) {
    }

    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;
    Monitor(Monitor&&) = delete;
    Monitor& operator=(Monitor&&) = delete;

    MonitorHandle<T> handle() {
        return MonitorHandle<T>(obj_, mtx_);
    }

private:
    T obj_{};
    Mutex mtx_;
};

} // namespace iso15118::detail
