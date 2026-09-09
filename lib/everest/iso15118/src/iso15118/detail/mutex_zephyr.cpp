// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Zephyr implementation of iso15118::detail::Mutex, built directly on Zephyr's own struct
// k_mutex - bypassing libstdc++'s std::mutex (and its gthreads requirement) entirely. Built
// instead of mutex_libstdcxx.cpp when ISO15118_TARGET_ZEPHYR is ON (see src/iso15118/CMakeLists.txt
// and detail/mutex.hpp).
#include <iso15118/detail/mutex.hpp>

#include <zephyr/kernel.h>

namespace iso15118::detail {

struct Mutex::Impl {
    struct k_mutex mtx;

    Impl() {
        k_mutex_init(&mtx);
    }
};

Mutex::Mutex() : impl(std::make_unique<Impl>()) {
}

Mutex::~Mutex() = default;

void Mutex::lock() {
    // K_FOREVER: this library assumes locking a Mutex always succeeds (eventually); there is no
    // caller that wants a timeout or a try-lock.
    k_mutex_lock(&impl->mtx, K_FOREVER);
}

void Mutex::unlock() {
    k_mutex_unlock(&impl->mtx);
}

} // namespace iso15118::detail
