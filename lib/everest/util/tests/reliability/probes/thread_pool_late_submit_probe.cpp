// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include <everest/util/async/thread_pool_scaling.hpp>

#include <atomic>
#include <cstdlib>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>
#include <string>
#include <unistd.h>

namespace {

std::atomic<int> paused{0};
std::atomic<int> children_done{0};
std::atomic<bool> collected{false};
std::atomic<bool> release_blockers{false};
std::atomic<pthread_mutex_t*> queue_mutex{nullptr};
thread_local int lock_number = 0;
thread_local bool destroying = false;
int joins_to_release = 1;
int worker_limit = 2;
int minimum = 1;
int parent_count = 1;
bool queued_at_shutdown = false;

} // namespace

extern "C" int pthread_mutex_lock(pthread_mutex_t* mutex) {
    static auto real_lock = reinterpret_cast<int (*)(pthread_mutex_t*)>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
    if (lock_number == 1) {
        queue_mutex = mutex;
    }
    if (lock_number != 0 && ++lock_number == 4) {
        lock_number = 0;
        ++paused;
        while (!collected.load()) {
            std::this_thread::yield();
        }
    }
    return real_lock(mutex);
}

extern "C" int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    static auto real_unlock = reinterpret_cast<int (*)(pthread_mutex_t*)>(dlsym(RTLD_NEXT, "pthread_mutex_unlock"));
    const int result = real_unlock(mutex);
    if (destroying && queued_at_shutdown && mutex == queue_mutex.load()) {
        release_blockers = true;
    }
    return result;
}

extern "C" int pthread_join(pthread_t thread, void** result) {
    static auto real_join = reinterpret_cast<int (*)(pthread_t, void**)>(dlsym(RTLD_NEXT, "pthread_join"));
    if (destroying && --joins_to_release == 0) {
        collected = true;
    }
    return real_join(thread, result);
}

namespace {

template <typename Policy> int probe() {
    using namespace std::chrono_literals;
    joins_to_release = Policy::supervisor_tick.has_value() ? 2 : 1;
    std::atomic<bool> arm{false};
    std::atomic<int> parents_ready{0};
    std::atomic<int> blockers_ready{0};
    using Pool = everest::lib::util::thread_pool_scaling<Policy>;
    auto pool = std::make_unique<Pool>(minimum, worker_limit, 60s);
    Pool* raw_pool = pool.get();
    for (int index = 0; index < parent_count; ++index) {
        pool->run([&] {
            ++parents_ready;
            while (!arm.load()) {
                std::this_thread::sleep_for(100us);
            }
            lock_number = 1;
            raw_pool->run([] { ++children_done; });
        });
    }
    for (int index = parent_count; index < worker_limit; ++index) {
        pool->run([&] {
            ++blockers_ready;
            while (!release_blockers.load()) {
                std::this_thread::sleep_for(100us);
            }
        });
    }
    while (parents_ready.load() != parent_count || blockers_ready.load() != worker_limit - parent_count) {
        std::this_thread::sleep_for(100us);
    }
    arm = true;
    while (paused.load() != parent_count) {
        std::this_thread::sleep_for(100us);
    }
    if (!queued_at_shutdown) {
        release_blockers = true;
        while (children_done.load() != parent_count) {
            std::this_thread::sleep_for(100us);
        }
    }
    std::this_thread::sleep_for(60ms);
    destroying = true;
    pool.reset();
    return children_done.load() == parent_count ? 0 : 4;
}

} // namespace

int main(int argc, char** argv) {
    std::set_terminate([] {
        const char message[] = "accepted submission resumed after workers were collected\n";
        (void)!write(STDERR_FILENO, message, sizeof(message) - 1);
        void* stack[32];
        backtrace_symbols_fd(stack, backtrace(stack, 32), STDERR_FILENO);
        std::_Exit(86);
    });
    if (argc != 6) {
        return 2;
    }
    worker_limit = std::atoi(argv[2]);
    minimum = std::atoi(argv[3]);
    parent_count = std::atoi(argv[4]);
    queued_at_shutdown = std::string(argv[5]) == "queued";
    if ((std::string(argv[1]) != "latency" && std::string(argv[1]) != "fixed") || worker_limit < 2 ||
        (minimum != 0 && minimum != 1) || parent_count < 1 || parent_count >= worker_limit ||
        (std::string(argv[5]) != "queued" && std::string(argv[5]) != "completed")) {
        return 2;
    }
    if (std::string(argv[1]) == "latency") {
        return probe<everest::lib::util::LatencyScaling<50, 5>>();
    }
    return probe<everest::lib::util::FixedSizeScaling<1>>();
}
