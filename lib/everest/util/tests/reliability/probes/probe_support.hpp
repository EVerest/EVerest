// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Contributors to EVerest

#pragma once

#include <chrono>
#include <cstdlib>
#include <dirent.h>
#include <thread>
#include <unistd.h>

namespace everest::lib::util::test_probe {

inline void diagnostic(const char* message) {
    (void)!write(STDERR_FILENO, message, __builtin_strlen(message));
    (void)!write(STDERR_FILENO, "\n", 1);
}

[[noreturn]] inline void fail(const char* message) {
    diagnostic(message);
    std::_Exit(1);
}

template <typename Predicate>
bool wait_until(Predicate predicate, std::chrono::milliseconds limit = std::chrono::milliseconds(1000)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return true;
}

inline int thread_count() {
    DIR* directory = opendir("/proc/self/task");
    if (directory == nullptr) {
        fail("probe: cannot count threads");
    }

    int count = 0;
    while (auto* entry = readdir(directory)) {
        if (entry->d_name[0] != '.') {
            ++count;
        }
    }
    closedir(directory);
    return count;
}

} // namespace everest::lib::util::test_probe
