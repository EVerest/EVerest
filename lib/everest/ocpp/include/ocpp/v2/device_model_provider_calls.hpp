// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <set>
#include <string>
#include <utility>

#include <everest/util/async/monitor.hpp>
#include <ocpp/v2/comparators.hpp>
#include <ocpp/v2/ocpp_types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Tracks the calls to the providers of runtime sources: writes to one variable are serialised, and a source can
/// wait until the calls to its provider returned
class ProviderCalls {
public:
    /// \brief A call to the provider of one source, counted until it is destroyed
    class Call {
    public:
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        Call(Call&& other) noexcept;
        Call& operator=(Call&&) = delete;
        ~Call();

        /// \brief Waits until no other call writes to the variable and reserves it for this call
        void lock_variable();

        /// \brief Ends the call early: releases the variable and stops counting the call
        void finish();

    private:
        friend class ProviderCalls;
        Call(ProviderCalls& calls, std::string source, std::pair<Component, Variable> variable);

        ProviderCalls* calls;
        std::string source;
        std::pair<Component, Variable> variable;
        bool variable_locked{false};
    };

    /// \brief Counts a call to the provider of \p source. Does not block.
    Call begin(const std::string& source, const Component& component, const Variable& variable);

    /// \brief Waits until the calls to the provider of \p source that are in progress returned
    void wait_idle(const std::string& source);

private:
    struct State {
        std::set<std::pair<Component, Variable>> variables_in_write;
        std::map<std::string, int> calls_in_progress;
    };
    everest::lib::util::monitor<State> state;
};

} // namespace v2
} // namespace ocpp
