// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

using namespace everest::lib::util;
using namespace std::chrono_literals;

namespace {

thread_local int allocations_until_failure = 0;
std::atomic<int> allocation_failures{0};
std::atomic<bool> enable_growth_failure{false};
std::atomic<int> failure_ordinal{1};
std::thread::id main_thread;

void* operator_new(std::size_t size) {
    if (allocations_until_failure > 0 && --allocations_until_failure == 0) {
        ++allocation_failures;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

struct ProbeLatency : LatencyScaling<5, 1> {
    static bool should_grow(std::size_t workers, std::size_t queued,
                            std::optional<std::chrono::steady_clock::time_point> oldest) {
        const bool grow = LatencyScaling<5, 1>::should_grow(workers, queued, oldest);
        if (grow && std::this_thread::get_id() != main_thread && enable_growth_failure.exchange(false)) {
            allocations_until_failure = failure_ordinal.load();
        }
        return grow;
    }
};

int supervisor_allocation(bool inject, int ordinal) {
    if (ordinal < 1) {
        return 2;
    }
    std::promise<void> entered;
    std::promise<void> release;
    const auto started = entered.get_future();
    const auto ready = release.get_future().share();
    std::atomic<int> done{0};
    {
        thread_pool_scaling<ProbeLatency> pool(1, 2, 1s);
        pool.run([&] {
            entered.set_value();
            ready.wait();
        });
        if (started.wait_for(1s) != std::future_status::ready) {
            test_probe::fail("allocation: supervisor setup did not start");
        }
        failure_ordinal = ordinal;
        enable_growth_failure = inject;
        pool.run([&] { ++done; });
        const bool completed = test_probe::wait_until([&] { return done == 1; }, 300ms);
        release.set_value();
        if (!completed) {
            test_probe::fail("allocation: supervisor did not recover");
        }
    }
    return done == 1 && allocation_failures == (inject ? 1 : 0) ? 0 : 1;
}

int worker_allocation(bool inject, int ordinal) {
    if (ordinal < 1) {
        return 2;
    }
    std::atomic<int> done{0};
    {
        thread_pool_scaling<LatencyScaling<5, 1>> pool(0, 2, 1ms);
        pool.run([&] {
            ++done;
            if (inject) {
                allocations_until_failure = ordinal;
            }
        });
        if (!test_probe::wait_until([&] { return done == 1; }, 300ms)) {
            return 1;
        }
        std::this_thread::sleep_for(100ms);
    }
    return done == 1 && allocation_failures == (inject ? 1 : 0) ? 0 : 1;
}

int destructor_allocation(bool inject) {
    std::atomic<int> done{0};
    auto pool = std::make_unique<thread_pool_scaling<LatencyScaling<50, 1>>>(0, 2, 1s);
    pool->run([&] { ++done; });
    if (inject) {
        allocations_until_failure = 1;
    }
    pool.reset();
    return done == 1 && allocation_failures == (inject ? 1 : 0) ? 0 : 1;
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

int main(int argc, char** argv) {
    main_thread = std::this_thread::get_id();
    if (argc < 2 || argc > 3) {
        return 2;
    }
    const std::string mode = argv[1];
    const int ordinal = argc == 3 ? std::atoi(argv[2]) : 1;
    if (mode == "supervisor-control") {
        return supervisor_allocation(false, ordinal);
    }
    if (mode == "supervisor-failure") {
        return supervisor_allocation(true, ordinal);
    }
    if (mode == "worker-control") {
        return worker_allocation(false, ordinal);
    }
    if (mode == "worker-failure") {
        return worker_allocation(true, ordinal);
    }
    if (mode == "destructor-control") {
        return destructor_allocation(false);
    }
    if (mode == "destructor-failure") {
        return destructor_allocation(true);
    }
    return 2;
}
