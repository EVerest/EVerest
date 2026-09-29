// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#include <utils/message_handler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <vector>

namespace {

int forced_processor_count = 0;

} // namespace

extern "C" int get_nprocs() {
    return forced_processor_count;
}

int main() {
    using namespace std::chrono_literals;

    const auto floor = Everest::THREAD_POOL_SCALING_MAX_THREAD_COUNT_FLOOR;
    for (const int processor_count : {0, 1, static_cast<int>(floor + 2)}) {
        forced_processor_count = processor_count;
        const auto maximum = Everest::thread_pool_scaling_max_thread_count();
        if (maximum != std::max<std::size_t>(floor, processor_count)) {
            return 1;
        }

        const auto chain_length = std::min<std::size_t>(maximum, 8);
        std::atomic<unsigned> expired{0};
        std::atomic<unsigned> done{0};
        for (int round = 0; round < 10; ++round) {
            std::vector<std::promise<void>> signals(chain_length);
            std::vector<std::shared_future<void>> ready;
            ready.reserve(chain_length);
            for (auto& signal : signals) {
                ready.push_back(signal.get_future().share());
            }

            // The configured policy may intentionally leave this small queue unchanged. A short fixed latency
            // policy isolates capacity progress while the generated settings are checked separately.
            everest::lib::util::thread_pool_scaling<everest::lib::util::LatencyScaling<1, 1>> pool(
                Everest::THREAD_POOL_SCALING_MIN_THREAD_COUNT, maximum, 1ms);
            for (std::size_t index = 0; index < chain_length; ++index) {
                pool.run([&, index] {
                    if (index + 1 < chain_length && ready[index + 1].wait_for(1500ms) != std::future_status::ready) {
                        ++expired;
                    }
                    signals[index].set_value();
                    ++done;
                });
            }
            if (ready.front().wait_for(1500ms) != std::future_status::ready) {
                ++expired;
            }
        }
        if (expired != 0 || done != chain_length * 10) {
            return 1;
        }
    }
    return 0;
}
