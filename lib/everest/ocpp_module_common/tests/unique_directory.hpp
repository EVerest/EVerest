// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace test_support {

/// \brief Creates an empty directory below the temporary directory whose name starts with \p prefix, so that tests of
/// several checkouts or test binaries on one host do not share files.
inline std::filesystem::path create_unique_directory(const std::string& prefix) {
    std::string pattern = (std::filesystem::temp_directory_path() / (prefix + "_XXXXXX")).string();
    if (mkdtemp(pattern.data()) == nullptr) {
        throw std::runtime_error("Could not create a temporary directory from " + pattern);
    }
    return pattern;
}

} // namespace test_support
