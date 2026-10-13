// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/project_info.hpp>

#include <generated/version_information.hpp>

namespace everest::project_info {

std::string_view name() noexcept {
    return PROJECT_NAME;
}

std::string_view version() noexcept {
    return PROJECT_VERSION;
}

std::string_view git_commit() noexcept {
    return GIT_VERSION;
}

} // namespace everest::project_info
