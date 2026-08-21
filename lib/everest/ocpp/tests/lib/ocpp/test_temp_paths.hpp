// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#pragma once

#include <atomic>
#include <filesystem>
#include <string>
#include <string_view>
#include <unistd.h>

namespace libocpp_test {

/// \brief a temporary path no other process can be using
/// \param[in] stem descriptive part of the name, so a leftover file says which test made it
/// \param[in] extension appended unchanged, include the dot
/// \return <tmp>/<stem>_<pid>_<n><extension>
///
/// Test databases and message log directories under a fixed path are shared by every
/// libocpp_unit_tests process on the machine. Two runs at once (several build trees, or
/// concurrent jobs on one CI host) then delete and re-create each other's databases, which
/// surfaces as unrelated migration and query failures.
inline std::filesystem::path unique_temp_path(std::string_view stem, std::string_view extension = {}) {
    static std::atomic<unsigned> sequence{0};
    return std::filesystem::temp_directory_path() /
           (std::string{stem} + "_" + std::to_string(::getpid()) + "_" +
            std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + std::string{extension});
}

/// \brief unique_temp_path() plus the directory itself
/// \note the caller owns removal, TearDown is the usual place
inline std::filesystem::path unique_temp_directory(std::string_view stem) {
    const auto path = unique_temp_path(stem);
    std::filesystem::create_directories(path);
    return path;
}

} // namespace libocpp_test
