// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/device_model_provider_calls.hpp>

#include <algorithm>
#include <utility>

namespace ocpp {
namespace v2 {

ProviderCalls::Call::Call(ProviderCalls& calls, std::string owner) : calls{&calls}, owner{std::move(owner)} {
}

ProviderCalls::Call::Call(Call&& other) noexcept :
    calls{other.calls}, owner{std::move(other.owner)}, thread{other.thread} {
    other.calls = nullptr;
}

ProviderCalls::Call::~Call() {
    if (this->calls == nullptr) {
        return;
    }
    {
        auto calls_in_progress = this->calls->calls_in_progress.handle();
        const auto it = calls_in_progress->find(this->owner);
        it->second.erase(it->second.find(this->thread));
        if (it->second.empty()) {
            calls_in_progress->erase(it);
        }
    }
    this->calls->calls_in_progress.notify_all();
}

ProviderCalls::Call ProviderCalls::begin(const std::string& owner) {
    (*this->calls_in_progress.handle())[owner].insert(std::this_thread::get_id());
    return Call{*this, owner};
}

void ProviderCalls::wait_idle(const std::string& owner) {
    const auto self = std::this_thread::get_id();
    auto calls_in_progress = this->calls_in_progress.handle();
    calls_in_progress.wait([&calls_in_progress, &owner, self]() {
        const auto it = calls_in_progress->find(owner);
        return it == calls_in_progress->end() or
               std::all_of(it->second.begin(), it->second.end(), [self](const auto& thread) { return thread == self; });
    });
}

} // namespace v2
} // namespace ocpp
