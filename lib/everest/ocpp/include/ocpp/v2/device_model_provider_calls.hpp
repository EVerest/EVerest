// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include <everest/util/async/monitor.hpp>

namespace ocpp {
namespace v2 {

/// \brief Counts the calls to the providers of runtime owners, so that an owner can wait until the calls to its
/// provider returned
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
        Call(ProviderCalls& calls, std::string owner);

        ProviderCalls* calls;
        std::string owner;
        std::thread::id thread{std::this_thread::get_id()};
    };

    /// \brief Counts a call to the provider of \p owner. Does not block.
    Call begin(const std::string& owner);

    /// \brief Waits until the calls to the provider of \p owner that are in progress on other threads returned. A call
    /// on the calling thread is the one the provider makes this call from, which returns only after it.
    void wait_idle(const std::string& owner);

private:
    everest::lib::util::monitor<std::map<std::string, std::multiset<std::thread::id>>> calls_in_progress;
};

} // namespace v2
} // namespace ocpp
