// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

#include <atomic>
#include <cerrno>
#include <dlfcn.h>
#include <memory>
#include <pthread.h>
#include <string>
#include <system_error>
#include <vector>

using namespace everest::lib::util;
using namespace std::chrono_literals;

namespace {

using Pool = thread_pool_scaling<LatencyScaling<0, 1>>;

std::atomic<int> thread_start_calls{0};
std::atomic<int> fail_at_call{0};
std::atomic<int> failures_left{0};
std::atomic<int> failures_observed{0};

struct SwitchableScaling {
    static constexpr std::optional<std::chrono::milliseconds> supervisor_tick = std::nullopt;
    static inline std::atomic<bool> enabled{false};

    static bool should_grow(std::size_t, std::size_t queued, std::optional<std::chrono::steady_clock::time_point>) {
        return enabled && queued > 0;
    }
};

int constructor_failures(int rounds) {
    if (rounds < 1) {
        return 2;
    }
    const int idle_threads = test_probe::thread_count();
    const int failures_before = failures_observed;
    for (int round = 0; round < rounds; ++round) {
        for (int position = 1; position <= 5; ++position) {
            fail_at_call = thread_start_calls.load() + position;
            bool threw = false;
            try {
                Pool pool(4, 8, 1ms);
            } catch (const std::system_error&) {
                threw = true;
            }
            fail_at_call = 0;
            if (!threw || !test_probe::wait_until([&] { return test_probe::thread_count() == idle_threads; })) {
                return 1;
            }
        }
    }
    return failures_observed - failures_before == rounds * 5 ? 0 : 1;
}

int resource_supervisor() {
    std::promise<void> release;
    std::promise<void> entered;
    const auto ready = release.get_future().share();
    const auto started = entered.get_future();
    std::atomic<int> done{0};
    const int failures_before = failures_observed;
    {
        Pool pool(1, 4, 1s);
        pool.run([&] {
            entered.set_value();
            ready.wait();
        });
        if (started.wait_for(1s) != std::future_status::ready) {
            test_probe::fail("stress-fault: supervisor setup did not start");
        }
        fail_at_call = thread_start_calls.load() + 1;
        pool.run([&] { ++done; });
        const bool injected = test_probe::wait_until([&] { return failures_observed == failures_before + 1; }, 300ms);
        release.set_value();
        if (!injected) {
            test_probe::fail("stress-fault: supervisor start failure did not fire");
        }
    }
    return done == 1 && failures_observed == failures_before + 1 ? 0 : 1;
}

int resource_submit() {
    SwitchableScaling::enabled = false;
    std::promise<void> release;
    std::promise<void> entered;
    const auto ready = release.get_future().share();
    const auto started = entered.get_future();
    std::atomic<int> done{0};
    const int failures_before = failures_observed;
    {
        thread_pool_scaling<SwitchableScaling> pool(1, 2, 1s);
        pool.run([&] {
            entered.set_value();
            ready.wait();
        });
        if (started.wait_for(1s) != std::future_status::ready) {
            test_probe::fail("stress-fault: submit setup did not start");
        }
        SwitchableScaling::enabled = true;
        fail_at_call = thread_start_calls.load() + 1;
        bool threw = false;
        try {
            pool.run([&] { ++done; });
        } catch (const std::system_error&) {
            threw = true;
        }
        fail_at_call = 0;
        pool.run([&] { ++done; });
        const bool recovered = test_probe::wait_until([&] { return done == 2; }, 300ms);
        release.set_value();
        if (threw || !recovered) {
            test_probe::fail("stress-fault: failed submission did not recover");
        }
    }
    return failures_observed == failures_before + 1 ? 0 : 1;
}

int cold_drain_retry(int rounds) {
    if (rounds < 1) {
        return 2;
    }
    for (int round = 0; round < rounds; ++round) {
        std::atomic<int> done{0};
        const int failures_before = failures_observed;
        {
            thread_pool_scaling<LatencyScaling<20, 1>> pool(0, 4, 1ms);
            failures_left = 3;
            for (int index = 0; index < 8; ++index) {
                pool.run([&] { ++done; });
            }
        }
        if (done != 8 || failures_observed != failures_before + 3) {
            return 1;
        }
    }
    return 0;
}

int drain_chain_failures(int rounds, int capacity) {
    if (rounds < 1 || capacity < 0) {
        return 2;
    }
    for (int round = 0; round < rounds; ++round) {
        std::vector<std::promise<void>> signals(8);
        std::vector<std::shared_future<void>> ready;
        ready.reserve(signals.size());
        for (auto& signal : signals) {
            ready.push_back(signal.get_future().share());
        }
        std::atomic<int> done{0};
        std::promise<void> entered;
        const auto started = entered.get_future();
        const int failures_before = failures_observed;
        {
            Pool pool(1, 8, std::chrono::milliseconds(round % 3), capacity);
            pool.run([&] {
                entered.set_value();
                if (ready[1].wait_for(1s) != std::future_status::ready) {
                    test_probe::fail("stress-fault: drain-chain root stranded");
                }
                ++done;
                signals[0].set_value();
            });
            if (started.wait_for(1s) != std::future_status::ready) {
                test_probe::fail("stress-fault: drain-chain root did not start");
            }
            failures_left = 5;
            for (int index = 1; index < 8; ++index) {
                pool.run([&, index] {
                    if (index != 7 && ready[index + 1].wait_for(1s) != std::future_status::ready) {
                        test_probe::fail("stress-fault: drain-chain child stranded");
                    }
                    ++done;
                    signals[index].set_value();
                });
            }
        }
        if (done != 8 || failures_observed != failures_before + 5) {
            return 1;
        }
    }
    return 0;
}

} // namespace

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*),
                              void* context) {
    static auto real_create = reinterpret_cast<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(
        dlsym(RTLD_NEXT, "pthread_create"));
    const int call = ++thread_start_calls;
    int left = failures_left.load();
    while (left > 0 && !failures_left.compare_exchange_weak(left, left - 1)) {
    }
    if (fail_at_call.load() == call || left > 0) {
        ++failures_observed;
        return EAGAIN;
    }
    return real_create(thread, attributes, entry, context);
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 4) {
        return 2;
    }
    const std::string mode = argv[1];
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 1;
    const int capacity = argc > 3 ? std::atoi(argv[3]) : 0;
    if (mode == "constructor-failures") {
        return constructor_failures(rounds);
    }
    if (mode == "resource-supervisor") {
        return argc == 2 ? resource_supervisor() : 2;
    }
    if (mode == "resource-submit") {
        return argc == 2 ? resource_submit() : 2;
    }
    if (mode == "cold-drain-retry") {
        return cold_drain_retry(rounds);
    }
    if (mode == "drain-chain-failures") {
        return drain_chain_failures(rounds, capacity);
    }
    return 2;
}
