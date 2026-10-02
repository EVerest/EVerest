// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <ocpp/v2/device_model_provider_calls.hpp>

namespace ocpp {
namespace v2 {

ProviderCalls::Call::Call(ProviderCalls& calls, std::string source, std::pair<Component, Variable> variable) :
    calls{&calls}, source{std::move(source)}, variable{std::move(variable)} {
}

ProviderCalls::Call::Call(Call&& other) noexcept :
    calls{other.calls},
    source{std::move(other.source)},
    variable{std::move(other.variable)},
    variable_locked{other.variable_locked} {
    other.calls = nullptr;
}

ProviderCalls::Call::~Call() {
    this->finish();
}

void ProviderCalls::Call::finish() {
    if (this->calls == nullptr) {
        return;
    }
    {
        auto state = this->calls->state.handle();
        if (this->variable_locked) {
            state->variables_in_write.erase(this->variable);
        }
        const auto calls_it = state->calls_in_progress.find(this->source);
        if (--calls_it->second == 0) {
            state->calls_in_progress.erase(calls_it);
        }
    }
    this->calls->state.notify_all();
    this->calls = nullptr;
}

void ProviderCalls::Call::lock_variable() {
    auto state = this->calls->state.handle();
    state.wait([&state, this]() { return state->variables_in_write.count(this->variable) == 0; });
    state->variables_in_write.insert(this->variable);
    this->variable_locked = true;
}

ProviderCalls::Call ProviderCalls::begin(const std::string& source, const Component& component,
                                         const Variable& variable) {
    ++this->state.handle()->calls_in_progress[source];
    return Call{*this, source, {component, variable}};
}

void ProviderCalls::wait_idle(const std::string& source) {
    auto state = this->state.handle();
    state.wait([&state, &source]() { return state->calls_in_progress.count(source) == 0; });
}

} // namespace v2
} // namespace ocpp
