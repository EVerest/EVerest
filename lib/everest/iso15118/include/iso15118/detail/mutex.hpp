// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>

namespace iso15118::detail {

/**
 * \brief A basic, non-recursive mutex - the platform-independent primitive this library uses
 * instead of std::mutex.
 *
 * std::mutex itself is not available on every C++ standard library configuration this library
 * targets: on at least one Zephyr/GNU libstdc++ configuration, std::mutex (and
 * std::condition_variable) are compiled out entirely because the toolchain's gthreads glue isn't
 * wired up, even though std::lock_guard/std::unique_lock (which only need a type with
 * lock()/unlock(), not std::mutex specifically) remain available. Routing this library's own
 * locking through Mutex instead of std::mutex sidesteps that gap.
 *
 * Implemented once per platform, selected by CMake (see src/iso15118/CMakeLists.txt,
 * ISO15118_TARGET_ZEPHYR):
 *  - mutex_libstdcxx.cpp: wraps std::mutex (host/POSIX builds, or any other build with a
 *    fully-featured libstdc++/libc++).
 *  - mutex_zephyr.cpp: wraps Zephyr's struct k_mutex directly, bypassing libstdc++ entirely.
 * A port to another platform only needs to add one more such .cpp file implementing this same
 * interface (and a CMake option to select it, analogous to ISO15118_TARGET_ZEPHYR).
 *
 * Satisfies C++'s BasicLockable requirement, so it can be used directly with std::lock_guard and
 * std::unique_lock.
 */
class Mutex {
public:
    Mutex();
    ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    void lock();
    void unlock();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace iso15118::detail
