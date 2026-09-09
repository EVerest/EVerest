// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// libstdc++ (std::mutex-backed) implementation of iso15118::detail::Mutex. Built instead of
// mutex_zephyr.cpp when ISO15118_TARGET_ZEPHYR is OFF (see src/iso15118/CMakeLists.txt and
// detail/mutex.hpp).
#include <iso15118/detail/mutex.hpp>

#include <mutex>

namespace iso15118::detail {

struct Mutex::Impl {
    std::mutex mtx;
};

Mutex::Mutex() : impl(std::make_unique<Impl>()) {
}

Mutex::~Mutex() = default;

void Mutex::lock() {
    impl->mtx.lock();
}

void Mutex::unlock() {
    impl->mtx.unlock();
}

} // namespace iso15118::detail
