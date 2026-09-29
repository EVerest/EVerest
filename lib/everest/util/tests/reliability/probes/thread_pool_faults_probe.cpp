// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include "probe_support.hpp"

#include <everest/util/async/thread_pool_scaling.hpp>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <memory>
#include <new>
#include <pthread.h>
#include <random>
#include <string>
#include <vector>

using namespace everest::lib::util;
using namespace std::chrono_literals;

namespace {

using Pool = thread_pool_scaling<LatencyScaling<1, 1>>;

thread_local int allocation_countdown = 0;
thread_local std::size_t failure_size = 0;
thread_local int deny_allocations = 0;
thread_local int producer_id = 0;
std::atomic<int> allocation_failures{0};
std::atomic<int> thread_start_failures{0};
std::atomic<int> waiting_producers{0};
std::atomic<bool> fail_thread_start{false};
std::atomic<bool> chaos_on{false};
std::atomic<unsigned> allocation_attempts{0};
std::atomic<unsigned> thread_attempts{0};
unsigned chaos_period = 31;

void* operator_new(std::size_t size) {
    const bool selected = allocation_countdown > 0 && --allocation_countdown == 0;
    const bool chaos_failure = chaos_on && ++allocation_attempts % chaos_period == 0;
    if (selected || (failure_size != 0 && failure_size == size) || deny_allocations > 0 || chaos_failure) {
        failure_size = 0;
        if (deny_allocations > 0) {
            --deny_allocations;
        }
        ++allocation_failures;
        throw std::bad_alloc();
    }
    if (void* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc();
}

template <typename Predicate> bool complete(Predicate predicate, std::chrono::milliseconds limit = 1000ms) {
    return test_probe::wait_until(predicate, limit);
}

int constructor_sweep(int ordinal) {
    if (ordinal < 1) {
        return 2;
    }
    const int before = test_probe::thread_count();
    bool threw = false;
    allocation_countdown = ordinal;
    try {
        Pool pool(4, 8, 1ms);
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    const bool injected = allocation_failures == 1;
    allocation_countdown = 0;
    return injected && threw && complete([&] { return test_probe::thread_count() == before; }) ? 0 : 1;
}

int submission_sweep(int ordinal, bool future) {
    if (ordinal < 1) {
        return 2;
    }
    std::atomic<int> done{0};
    std::array<unsigned char, 128> payload{};
    payload[0] = 42;
    bool threw = false;
    bool got_future = false;
    {
        Pool pool(1, 4, 1ms);
        allocation_countdown = ordinal;
        try {
            if (future) {
                auto result = pool([&] {
                    ++done;
                    return 42;
                });
                allocation_countdown = 0;
                if (result.get() != 42) {
                    return 1;
                }
                got_future = true;
            } else {
                pool.run([&, payload] {
                    if (payload[0] == 42) {
                        ++done;
                    }
                });
            }
        } catch (const std::bad_alloc&) {
            threw = true;
        }
        allocation_countdown = 0;
    }
    const bool injected = allocation_failures == 1;
    // A run() submission has fewer allocation sites than the future path. An ordinal beyond those sites is a
    // passing control: no exception is expected and the accepted task must still execute once.
    if (!injected) {
        return !threw && done == 1 && (!future || got_future) ? 0 : 1;
    }
    return threw && done == 0 ? 0 : 1;
}

int retirement(int failures) {
    if (failures < 1) {
        return 2;
    }
    const int before = test_probe::thread_count();
    std::atomic<int> done{0};
    {
        Pool pool(0, 4, 1ms);
        pool.run([&] {
            deny_allocations = failures;
            ++done;
        });
        if (!complete([&] { return done == 1; }) || !complete([&] { return allocation_failures == failures; }) ||
            !complete([&] { return test_probe::thread_count() == before + 1; })) {
            return 1;
        }
        if (pool([] { return 42; }).get() != 42) {
            return 1;
        }
    }
    return allocation_failures == failures && complete([&] { return test_probe::thread_count() == before; }) ? 0 : 1;
}

template <typename Policy> int cold_shutdown(int fault) {
    if (fault < 0 || fault > 2) {
        return 2;
    }
    std::atomic<int> ran{0};
    std::future<int> future;
    {
        thread_pool_scaling<Policy> pool(0, 2, 1ms);
        future = pool([&] {
            ++ran;
            return 42;
        });
        if (fault == 1) {
            fail_thread_start = true;
        }
        if (fault == 2) {
            allocation_countdown = 1;
        }
    }
    allocation_countdown = 0;
    bool broken_promise = false;
    try {
        if (future.get() != 42) {
            return 1;
        }
    } catch (const std::future_error&) {
        broken_promise = true;
    }
    const int expected_thread_failures = fault == 1 ? 1 : 0;
    const int expected_allocation_failures = fault == 2 ? 1 : 0;
    return thread_start_failures == expected_thread_failures && allocation_failures == expected_allocation_failures &&
                   ran == 1 && !broken_promise
               ? 0
               : 1;
}

int bounded_enqueue(bool inject) {
    std::atomic<int> warm_done{0};
    std::atomic<int> ran{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
    std::atomic<bool> first_done{false};
    std::atomic<bool> second_done{false};
    std::atomic<bool> caught{false};
    Pool pool(1, 1, 1s, 1);
    constexpr int block_elements = 512 / sizeof(TrackedAction);
    for (int index = 0; index < block_elements - 3; ++index) {
        pool.run([&] { ++warm_done; });
        if (!complete([&] { return warm_done == index + 1; })) {
            return 1;
        }
    }
    pool.run([&] {
        entered = true;
        while (!release) {
            std::this_thread::yield();
        }
    });
    if (!complete([&] { return entered.load(); })) {
        test_probe::fail("faults: bounded blocker did not enter");
    }
    pool.run([] {});
    std::thread first([&] {
        producer_id = 1;
        if (inject) {
            failure_size = block_elements * sizeof(TrackedAction);
        }
        try {
            pool.run([&] { ++ran; });
        } catch (const std::bad_alloc&) {
            caught = true;
        }
        failure_size = 0;
        first_done = true;
    });
    if (!complete([&] { return waiting_producers.load() & 2; })) {
        test_probe::fail("faults: first bounded producer did not block");
    }
    std::thread second([&] {
        producer_id = 2;
        pool.run([&] { ++ran; });
        second_done = true;
    });
    if (!complete([&] { return waiting_producers.load() & 4; })) {
        test_probe::fail("faults: second bounded producer did not block");
    }
    release = true;
    if (!complete([&] { return first_done.load(); })) {
        test_probe::fail("faults: first bounded producer did not return");
    }
    const bool autonomous = complete([&] { return second_done.load(); }, 300ms);
    if (!autonomous) {
        pool.run([] {});
    }
    if (!complete([&] { return second_done.load(); })) {
        test_probe::fail("faults: bounded recovery submission did not return");
    }
    first.join();
    second.join();
    const int expected_tasks = caught ? 1 : 2;
    const bool complete_without_poke =
        autonomous && ran == expected_tasks && allocation_failures == (inject ? 1 : 0) && caught == inject;
    return complete_without_poke ? 0 : 1;
}

int chaos(unsigned period, int capacity, int rounds, int tasks_per_producer, unsigned seed) {
    constexpr int producers = 8;
    if (period < 2 || capacity < 0 || rounds < 1 || tasks_per_producer < 1) {
        return 2;
    }
    chaos_period = period;
    const int before = test_probe::thread_count();
    int retried = 0;
    for (int round = 0; round < rounds; ++round) {
        std::vector<std::atomic<int>> seen(producers * tasks_per_producer);
        for (auto& value : seen) {
            value = 0;
        }
        std::atomic<bool> start{false};
        std::atomic<int> retries{0};
        std::atomic<int> active{0};
        std::atomic<int> peak{0};
        auto pool = std::make_unique<Pool>(round % 2, 8, std::chrono::milliseconds(round % 3), capacity);
        std::array<std::thread, producers> submitters;
        for (int producer = 0; producer < producers; ++producer) {
            submitters[producer] = std::thread([&, producer] {
                std::mt19937 local_random(seed + static_cast<unsigned>(round * 101 + producer));
                while (!start) {
                    std::this_thread::yield();
                }
                for (int task = 0; task < tasks_per_producer; ++task) {
                    const int id = producer * tasks_per_producer + task;
                    for (;;) {
                        try {
                            pool->run([&, id] {
                                const int active_workers = ++active;
                                int previous_peak = peak.load();
                                while (previous_peak < active_workers &&
                                       !peak.compare_exchange_weak(previous_peak, active_workers)) {
                                }
                                ++seen[id];
                                if (id % 73 == 0) {
                                    std::this_thread::yield();
                                }
                                std::this_thread::sleep_for(100us);
                                --active;
                            });
                            break;
                        } catch (const std::bad_alloc&) {
                            ++retries;
                            std::this_thread::yield();
                        }
                    }
                    if (local_random() % 100 == 0) {
                        std::this_thread::sleep_for(100us);
                    }
                }
            });
        }
        chaos_on = true;
        start = true;
        for (auto& submitter : submitters) {
            submitter.join();
        }
        pool.reset();
        chaos_on = false;
        if (active != 0 || peak > 8) {
            return 1;
        }
        for (auto& value : seen) {
            if (value != 1) {
                return 1;
            }
        }
        retried += retries;
    }
    return allocation_failures > 0 && thread_start_failures > 0 && retried > 0 &&
                   complete([&] { return test_probe::thread_count() == before; })
               ? 0
               : 1;
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
    if (fail_thread_start.exchange(false) || (chaos_on && ++thread_attempts % 5 == 0)) {
        ++thread_start_failures;
        return EAGAIN;
    }
    return real_create(thread, attributes, entry, context);
}

extern "C" int pthread_cond_wait(pthread_cond_t* condition, pthread_mutex_t* mutex) {
    static auto real_wait =
        reinterpret_cast<int (*)(pthread_cond_t*, pthread_mutex_t*)>(dlsym(RTLD_NEXT, "pthread_cond_wait"));
    if (producer_id != 0) {
        waiting_producers.fetch_or(1 << producer_id);
    }
    return real_wait(condition, mutex);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        return 2;
    }
    const std::string mode = argv[1];
    const int value = argc > 2 ? std::atoi(argv[2]) : 1;
    if (mode == "constructor") {
        return constructor_sweep(value);
    }
    if (mode == "submit") {
        return submission_sweep(value, false);
    }
    if (mode == "future") {
        return submission_sweep(value, true);
    }
    if (mode == "retirement") {
        return retirement(value);
    }
    if (mode == "cold-greedy") {
        return cold_shutdown<GreedyScaling>(value);
    }
    if (mode == "cold-latency") {
        return cold_shutdown<LatencyScaling<50, 5>>(value);
    }
    if (mode == "bounded-enqueue") {
        return bounded_enqueue(value != 0);
    }
    if (mode == "chaos") {
        const int capacity = argc > 3 ? std::atoi(argv[3]) : 0;
        const int rounds = argc > 4 ? std::atoi(argv[4]) : 2;
        const int tasks_per_producer = argc > 5 ? std::atoi(argv[5]) : 50;
        const unsigned seed = argc > 6 ? std::strtoul(argv[6], nullptr, 10) : 12345U;
        return chaos(static_cast<unsigned>(value), capacity, rounds, tasks_per_producer, seed);
    }
    return 2;
}
