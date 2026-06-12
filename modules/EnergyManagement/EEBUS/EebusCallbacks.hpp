// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Callback bundle handed to EEBUS use case handlers

#pragma once

#include <functional>

#include <generated/types/energy.hpp>

namespace module {

/// \brief Callbacks a use case handler invokes to publish results into EVerest.
struct EebusCallbacks {
    /// \brief check that every callback in the bundle is set
    /// \returns true when all callbacks are callable
    [[nodiscard]] bool all_callbacks_valid() const;

    /// \brief publishes newly calculated external limits to the energy sink
    std::function<void(const types::energy::ExternalLimits& new_limits)> update_limits_callback;
};

} // namespace module
