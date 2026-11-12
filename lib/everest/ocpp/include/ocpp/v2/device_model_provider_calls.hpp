// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <everest/util/async/monitor.hpp>

namespace ocpp {
namespace v2 {

class VariableValueProvider;

/// \brief Counts the calls to the providers of runtime owners by provider and by the generation of the registration
/// they were made for, so that an owner can wait until the calls to its provider returned
class ProviderCalls {
public:
    /// \brief A call to the provider of one owner, counted until it is destroyed
    class Call {
    public:
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        Call(Call&& other) noexcept;
        Call& operator=(Call&&) = delete;
        ~Call();

    private:
        friend class ProviderCalls;
        Call(ProviderCalls& calls, std::string owner, const VariableValueProvider* provider, std::uint64_t generation);

        ProviderCalls* calls;
        std::string owner;
        const VariableValueProvider* provider;
        std::uint64_t generation;
        std::thread::id thread{std::this_thread::get_id()};
    };

    /// \brief Counts a call to \p provider, which \p owner registered with \p generation. Does not block.
    Call begin(const std::string& owner, const VariableValueProvider* provider, std::uint64_t generation);

    /// \brief Waits until the calls to the providers of \p owner in progress returned. Called from inside a call to
    /// any provider, it returns at once: two such calls waiting for each other would never return.
    void wait_idle(const std::string& owner);

    /// \brief Like wait_idle, for the calls that the registration of \p provider with \p generation replaced only:
    /// those made for an older registration to another provider. No such call begins after that registration.
    void wait_idle_replaced(const std::string& owner, const VariableValueProvider* provider, std::uint64_t generation);

private:
    struct CallInProgress {
        const VariableValueProvider* provider;
        std::uint64_t generation;
        std::thread::id thread;
    };
    template <typename AwaitedCall> void wait_idle_if(const std::string& owner, AwaitedCall awaited);

    everest::lib::util::monitor<std::map<std::string, std::vector<CallInProgress>>> calls_in_progress;
};

} // namespace v2
} // namespace ocpp
