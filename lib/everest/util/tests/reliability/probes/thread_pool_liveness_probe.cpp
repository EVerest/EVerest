// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

#include <atomic>
#include <cerrno>
#include <dlfcn.h>
#include <pthread.h>
#include <string>

using namespace everest::lib::util;
using namespace std::chrono_literals;

namespace {

std::atomic<bool> fail_once{false};
std::atomic<int> injected_thread_start_failures{0};

template <typename Policy> int failed_growth(bool inject, bool expect_autonomous_recovery) {
    std::promise<void> entered;
    std::promise<void> leaf;
    const auto entered_future = entered.get_future();
    const auto leaf_future = leaf.get_future().share();
    std::atomic<int> done{0};
    thread_pool_scaling<Policy> pool(1, 2, 1s);
    pool.run([&] {
        entered.set_value();
        leaf_future.wait();
        ++done;
    });
    if (entered_future.wait_for(1s) != std::future_status::ready) {
        test_probe::fail("liveness: first worker did not enter");
    }
    fail_once = inject;
    pool.run([&] { ++done; });
    pool.run([&] {
        ++done;
        leaf.set_value();
    });
    const bool recovered_without_submission = test_probe::wait_until([&] { return done == 3; }, 300ms);
    if (!recovered_without_submission) {
        pool.run([] {});
        if (!test_probe::wait_until([&] { return done == 3; })) {
            test_probe::fail("liveness: recovery submission did not drain work");
        }
    }
    return injected_thread_start_failures == (inject ? 1 : 0) &&
                   recovered_without_submission == expect_autonomous_recovery
               ? 0
               : 1;
}

template <typename Policy> int failed_cold_growth(bool inject, bool expect_autonomous_recovery) {
    std::atomic<int> done{0};
    thread_pool_scaling<Policy> pool(0, 2, 1s);
    fail_once = inject;
    pool.run([&] { ++done; });
    pool.run([&] { ++done; });
    const bool recovered_without_submission = test_probe::wait_until([&] { return done == 2; }, 300ms);
    if (!recovered_without_submission) {
        pool.run([] {});
        if (!test_probe::wait_until([&] { return done == 2; })) {
            test_probe::fail("liveness: cold recovery submission did not drain work");
        }
    }
    return injected_thread_start_failures == (inject ? 1 : 0) &&
                   recovered_without_submission == expect_autonomous_recovery
               ? 0
               : 1;
}

[[noreturn]] void bounded_reentrant(int capacity, int maximum) {
    std::promise<void> release;
    const auto ready = release.get_future().share();
    std::atomic<int> entered{0};
    std::atomic<int> submitted{0};
    std::atomic<int> parents_done{0};
    std::atomic<int> children_done{0};
    thread_pool_scaling<LatencyScaling<5, 1>> pool(4, maximum, 1s, capacity);
    for (int index = 0; index < 4; ++index) {
        pool.run([&] {
            ++entered;
            ready.wait();
            for (int child = 0; child < 2; ++child) {
                pool.run([&] { ++children_done; });
                ++submitted;
            }
            ++parents_done;
        });
    }
    if (!test_probe::wait_until([&] { return entered == 4; })) {
        test_probe::fail("liveness: parent setup stalled");
    }
    release.set_value();
    const bool completed = test_probe::wait_until([&] { return parents_done == 4 && children_done == 8; }, 500ms);
    if (!completed && capacity == 1 && maximum == 4 && entered == 4 && submitted < 8) {
        test_probe::diagnostic("liveness: bounded reentrant queue limit reached");
        std::_Exit(3);
    }
    std::_Exit(completed ? 0 : 1);
}

} // namespace

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*),
                              void* context) {
    static auto original = reinterpret_cast<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(
        dlsym(RTLD_NEXT, "pthread_create"));
    if (fail_once.exchange(false)) {
        ++injected_thread_start_failures;
        return EAGAIN;
    }
    return original(thread, attributes, entry, context);
}

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "greedy-control") {
        return failed_growth<GreedyScaling>(false, true);
    }
    if (mode == "greedy-failure") {
        return failed_growth<GreedyScaling>(true, false);
    }
    if (mode == "fixed-control") {
        return failed_growth<FixedSizeScaling<2>>(false, true);
    }
    if (mode == "fixed-failure") {
        return failed_growth<FixedSizeScaling<2>>(true, false);
    }
    if (mode == "latency-failure") {
        return failed_growth<LatencyScaling<5, 1>>(true, true);
    }
    if (mode == "cold-greedy-control") {
        return failed_cold_growth<GreedyScaling>(false, true);
    }
    if (mode == "cold-greedy-failure") {
        return failed_cold_growth<GreedyScaling>(true, false);
    }
    if (mode == "cold-latency-failure") {
        return failed_cold_growth<LatencyScaling<5, 1>>(true, true);
    }
    if (mode == "bounded-stall") {
        bounded_reentrant(1, 4);
    }
    if (mode == "unbounded-control") {
        bounded_reentrant(0, 4);
    }
    if (mode == "larger-queue-control") {
        bounded_reentrant(8, 4);
    }
    if (mode == "spare-worker-control") {
        bounded_reentrant(1, 8);
    }
    return 2;
}
