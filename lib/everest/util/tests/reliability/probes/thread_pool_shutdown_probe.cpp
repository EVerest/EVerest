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

std::atomic<bool> fail_next_thread_start{false};
std::atomic<int> injected_thread_start_failures{0};

} // namespace

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes, void* (*entry)(void*),
                              void* context) {
    static auto real_create = reinterpret_cast<int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*)>(
        dlsym(RTLD_NEXT, "pthread_create"));
    if (fail_next_thread_start.exchange(false)) {
        ++injected_thread_start_failures;
        return EAGAIN;
    }
    return real_create(thread, attributes, entry, context);
}

namespace {

int failed_chain(bool inject) {
    std::promise<void> entered;
    std::promise<void> b_done;
    std::promise<void> c_done;
    auto started = entered.get_future();
    const auto b_ready = b_done.get_future().share();
    const auto c_ready = c_done.get_future().share();
    std::atomic<int> completed{0};
    std::atomic<int> timed_out{0};
    std::atomic<int> inline_callbacks{0};
    const auto destroying_thread = std::this_thread::get_id();

    {
        thread_pool_scaling<LatencyScaling<50, 5>> pool(1, 3, 1s);
        pool.run([&] {
            entered.set_value();
            if (b_ready.wait_for(1s) != std::future_status::ready) {
                ++timed_out;
            }
            ++completed;
        });
        if (started.wait_for(1s) != std::future_status::ready) {
            test_probe::fail("shutdown: dependency root did not start");
        }
        pool.run([&] {
            if (std::this_thread::get_id() == destroying_thread) {
                ++inline_callbacks;
            }
            if (c_ready.wait_for(300ms) != std::future_status::ready) {
                ++timed_out;
            }
            b_done.set_value();
            ++completed;
        });
        pool.run([&] {
            c_done.set_value();
            ++completed;
        });
        fail_next_thread_start = inject;
    }

    return injected_thread_start_failures == (inject ? 1 : 0) && timed_out == 0 && completed == 3 &&
                   inline_callbacks == 0
               ? 0
               : 1;
}

template <typename Policy> int serial_shutdown(bool destroy_with_backlog) {
    std::promise<void> entered;
    const auto started = entered.get_future();
    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> inline_callbacks{0};
    std::atomic<int> done{0};
    const auto destroying_thread = std::this_thread::get_id();

    {
        thread_pool_scaling<Policy> pool(1, 1, 1s);
        const auto record = [&] {
            const int active_callbacks = ++active;
            int previous_peak = peak.load();
            while (previous_peak < active_callbacks && !peak.compare_exchange_weak(previous_peak, active_callbacks)) {
            }
            if (std::this_thread::get_id() == destroying_thread) {
                ++inline_callbacks;
            }
        };
        pool.run([&] {
            record();
            entered.set_value();
            std::this_thread::sleep_for(200ms);
            --active;
            ++done;
        });
        if (started.wait_for(1s) != std::future_status::ready) {
            return 1;
        }
        pool.run([&] {
            record();
            --active;
            ++done;
        });
        if (!destroy_with_backlog && !test_probe::wait_until([&] { return done == 2; }, 1s)) {
            return 1;
        }
    }

    return peak == 1 && done == 2 && inline_callbacks == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    const std::string mode = argv[1];
    if (mode == "chain-control") {
        return failed_chain(false);
    }
    if (mode == "chain-failure") {
        return failed_chain(true);
    }
    if (mode == "serial-latency") {
        return serial_shutdown<LatencyScaling<50, 5>>(true);
    }
    if (mode == "serial-greedy") {
        return serial_shutdown<GreedyScaling>(true);
    }
    if (mode == "serial-control") {
        return serial_shutdown<LatencyScaling<50, 5>>(false);
    }
    return 2;
}
