// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <new>
#include <pthread.h>
#include <string>

using namespace everest::lib::util;
using namespace std::chrono_literals;

namespace {

std::atomic<int> thread_failures_left{0};
std::atomic<int> thread_failures{0};
std::atomic<int> allocation_failures{0};
thread_local int allocation_failures_left = 0;
thread_local int allocation_stride = 1;
thread_local int allocation_countdown = 1;

void* operator_new(std::size_t size) {
    if (allocation_failures_left > 0 && --allocation_countdown == 0) {
        --allocation_failures_left;
        allocation_countdown = allocation_stride;
        ++allocation_failures;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

} // namespace

void* operator new(std::size_t size) {
    return operator_new(size);
}

void* operator new[](std::size_t size) {
    return operator_new(size);
}

void operator delete(void* memory) noexcept {
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}

void operator delete[](void* memory) noexcept {
    std::free(memory);
}

void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*),
                              void* context) {
    static auto real_create = reinterpret_cast<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(
        dlsym(RTLD_NEXT, "pthread_create"));
    int left = thread_failures_left.load();
    while (left > 0 && !thread_failures_left.compare_exchange_weak(left, left - 1)) {
    }
    if (left > 0) {
        ++thread_failures;
        return EAGAIN;
    }
    return real_create(thread, attributes, entry, context);
}

namespace {

template <typename Policy> int run(int minimum, const std::string& fault, int failures) {
    constexpr int length = 8;
    if ((minimum != 0 && minimum != 1) || failures < 0) {
        return 2;
    }

    const int initial_threads = test_probe::thread_count();
    const auto destroying_thread = std::this_thread::get_id();
    std::array<std::promise<void>, length> signals;
    std::array<std::shared_future<void>, length> ready;
    std::array<std::atomic<int>, length> seen{};
    for (int index = 0; index < length; ++index) {
        ready[index] = signals[index].get_future().share();
        seen[index] = 0;
    }

    std::atomic<int> expired{0};
    std::atomic<int> inline_callbacks{0};
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    {
        thread_pool_scaling<Policy> pool(minimum, length, 1s);
        for (int index = 0; index < length; ++index) {
            pool.run([&, index] {
                const int active_workers = ++active;
                int previous_peak = peak.load();
                while (previous_peak < active_workers && !peak.compare_exchange_weak(previous_peak, active_workers)) {
                }
                if (std::this_thread::get_id() == destroying_thread) {
                    ++inline_callbacks;
                }
                if (index + 1 < length && ready[index + 1].wait_for(1s) != std::future_status::ready) {
                    ++expired;
                }
                ++seen[index];
                signals[index].set_value();
                --active;
            });
        }
        if (fault == "thread") {
            thread_failures_left = failures;
        } else if (fault == "node" || fault == "state") {
            allocation_stride = allocation_countdown = fault == "node" ? 1 : 2;
            allocation_failures_left = failures;
        } else if (fault != "control") {
            return 2;
        }
    }

    allocation_failures_left = 0;
    const auto stopped = test_probe::wait_until([&] { return test_probe::thread_count() == initial_threads; });
    bool exactly_once = true;
    for (auto& value : seen) {
        exactly_once = exactly_once && value == 1;
    }
    const int injected = thread_failures + allocation_failures;
    const int expected_failures = fault == "control" ? 0 : failures;
    return injected == expected_failures && exactly_once && expired == 0 && inline_callbacks == 0 && peak <= length &&
                   active == 0 && stopped
               ? 0
               : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        return 2;
    }

    const std::string policy = argv[1];
    const int minimum = std::atoi(argv[2]);
    const std::string fault = argv[3];
    const int failures = std::atoi(argv[4]);
    if (policy == "latency") {
        return run<LatencyScaling<50, 5>>(minimum, fault, failures);
    }
    if (policy == "fixed") {
        return run<FixedSizeScaling<100>>(minimum, fault, failures);
    }
    return 2;
}
