// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <string>

namespace Everest {

/// \brief Add the permitted Linux capabilities of this process to its inheritable and ambient sets, so that
///        child processes started via execve() keep them. Does nothing when running as root or without
///        permitted capabilities, e.g. when the module binary has no file capabilities.
///        Capabilities are per thread and only threads created afterwards inherit them, so call this before
///        any other thread is started.
/// \return Error description on failure
std::optional<std::string> raise_ambient_capabilities();

} // namespace Everest
