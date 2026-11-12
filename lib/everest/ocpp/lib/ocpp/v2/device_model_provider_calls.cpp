// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/device_model_provider_calls.hpp>

#include <algorithm>
#include <utility>

namespace ocpp {
namespace v2 {

ProviderCalls::Call::Call(ProviderCalls& calls, std::string owner, const VariableValueProvider* provider,
                          const std::uint64_t generation) :
    calls{&calls}, owner{std::move(owner)}, provider{provider}, generation{generation} {
}

ProviderCalls::Call::Call(Call&& other) noexcept :
    calls{other.calls},
    owner{std::move(other.owner)},
    provider{other.provider},
    generation{other.generation},
    thread{other.thread} {
    other.calls = nullptr;
}

ProviderCalls::Call::~Call() {
    if (this->calls == nullptr) {
        return;
    }
    {
        auto calls_in_progress = this->calls->calls_in_progress.handle();
        const auto it = calls_in_progress->find(this->owner);
        auto& calls_of_owner = it->second;
        calls_of_owner.erase(
            std::find_if(calls_of_owner.begin(), calls_of_owner.end(), [this](const CallInProgress& call) {
                return call.provider == this->provider and call.generation == this->generation and
                       call.thread == this->thread;
            }));
        if (calls_of_owner.empty()) {
            calls_in_progress->erase(it);
        }
    }
    this->calls->calls_in_progress.notify_all();
}

ProviderCalls::Call ProviderCalls::begin(const std::string& owner, const VariableValueProvider* provider,
                                         const std::uint64_t generation) {
    (*this->calls_in_progress.handle())[owner].push_back({provider, generation, std::this_thread::get_id()});
    return Call{*this, owner, provider, generation};
}

void ProviderCalls::wait_idle(const std::string& owner) {
    this->wait_idle_if(owner, [](const CallInProgress&) { return true; });
}

void ProviderCalls::wait_idle_replaced(const std::string& owner, const VariableValueProvider* provider,
                                       const std::uint64_t generation) {
    this->wait_idle_if(owner, [provider, generation](const CallInProgress& call) {
        return call.generation < generation and call.provider != provider;
    });
}

template <typename AwaitedCall> void ProviderCalls::wait_idle_if(const std::string& owner, AwaitedCall awaited) {
    const auto self = std::this_thread::get_id();
    auto calls_in_progress = this->calls_in_progress.handle();
    // A thread inside a provider call never waits: two such threads waiting for each other would never return
    const bool inside_a_call =
        std::any_of(calls_in_progress->begin(), calls_in_progress->end(), [self](const auto& calls_of_owner) {
            return std::any_of(calls_of_owner.second.begin(), calls_of_owner.second.end(),
                               [self](const CallInProgress& call) { return call.thread == self; });
        });
    if (inside_a_call) {
        return;
    }
    calls_in_progress.wait([&calls_in_progress, &owner, &awaited]() {
        const auto it = calls_in_progress->find(owner);
        return it == calls_in_progress->end() or std::none_of(it->second.begin(), it->second.end(), awaited);
    });
}

} // namespace v2
} // namespace ocpp
