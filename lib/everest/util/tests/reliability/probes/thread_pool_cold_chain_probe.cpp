// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

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

std::atomic<bool> fail_next_thread_start{false};
std::atomic<int> thread_start_failures{0};
std::atomic<int> allocation_failures{0};
thread_local int allocation_countdown = 0;

void* operator_new(std::size_t size) {
    if (allocation_countdown > 0 && --allocation_countdown == 0) {
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
    if (fail_next_thread_start.exchange(false)) {
        ++thread_start_failures;
        return EAGAIN;
    }
    return real_create(thread, attributes, entry, context);
}

namespace {

enum class Fault {
    control,
    thread,
    node,
    state
};

Fault parse_fault(const std::string& value) {
    if (value == "control") {
        return Fault::control;
    }
    if (value == "thread") {
        return Fault::thread;
    }
    if (value == "node") {
        return Fault::node;
    }
    if (value == "state") {
        return Fault::state;
    }
    test_probe::fail("cold-chain: invalid fault");
}

int run(Fault fault, int minimum) {
    if (minimum < 0 || minimum > 1) {
        return 2;
    }

    const int initial_threads = test_probe::thread_count();
    std::promise<void> release;
    const auto released = release.get_future().share();
    std::atomic<int> done{0};
    std::atomic<int> timed_out{0};
    std::atomic<int> inline_callbacks{0};
    const auto destroying_thread = std::this_thread::get_id();

    {
        thread_pool_scaling<LatencyScaling<50, 5>> pool(minimum, 2, 1s);
        pool.run([&] {
            if (std::this_thread::get_id() == destroying_thread) {
                ++inline_callbacks;
            }
            if (released.wait_for(300ms) != std::future_status::ready) {
                ++timed_out;
            }
            ++done;
        });
        pool.run([&] {
            release.set_value();
            ++done;
        });

        switch (fault) {
        case Fault::thread:
            fail_next_thread_start = true;
            break;
        case Fault::node:
            allocation_countdown = 1;
            break;
        case Fault::state:
            allocation_countdown = 2;
            break;
        case Fault::control:
            break;
        }
    }

    allocation_countdown = 0;
    const int expected_thread_failures = fault == Fault::thread ? 1 : 0;
    const int expected_allocation_failures = fault == Fault::node || fault == Fault::state ? 1 : 0;
    return thread_start_failures == expected_thread_failures && allocation_failures == expected_allocation_failures &&
                   done == 2 && timed_out == 0 && inline_callbacks == 0 && test_probe::thread_count() == initial_threads
               ? 0
               : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return 2;
    }
    return run(parse_fault(argv[1]), std::atoi(argv[2]));
}
