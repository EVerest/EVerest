// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string_view>

namespace everest::project_info {

std::string_view name() noexcept;

std::string_view description() noexcept;

std::string_view version() noexcept;

std::string_view git_commit() noexcept;

} // namespace everest::project_info
