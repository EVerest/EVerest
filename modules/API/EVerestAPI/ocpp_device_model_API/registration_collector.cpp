// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "registration_collector.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace module {

namespace {

std::string trim(std::string const& text) {
    const auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
    const auto begin = std::find_if_not(text.begin(), text.end(), is_space);
    const auto end = std::find_if_not(text.rbegin(), std::string::const_reverse_iterator(begin), is_space).base();
    return std::string(begin, end);
}

} // namespace

std::set<std::string> parse_owner_list(std::string const& list) {
    std::set<std::string> owners;
    std::istringstream stream(list);
    std::string entry;
    while (std::getline(stream, entry, ',')) {
        auto owner = trim(entry);
        if (not owner.empty()) {
            owners.insert(std::move(owner));
        }
    }
    return owners;
}

RegistrationCollector::RegistrationCollector(std::string module_id, std::string const& expected_owners,
                                             std::chrono::milliseconds expected_owners_timeout,
                                             std::chrono::milliseconds registration_timeout) :
    m_module_id(std::move(module_id)),
    m_expected_owners(parse_owner_list(expected_owners)),
    m_expected_owners_timeout(expected_owners_timeout),
    m_registration_timeout(registration_timeout) {
}

std::vector<std::string> RegistrationCollector::expected_owners() const {
    return {m_expected_owners.begin(), m_expected_owners.end()};
}

bool RegistrationCollector::add(Registration const& registration) {
    bool answered = false;
    {
        auto state = m_state.handle();
        const auto owner = owner_of(registration);
        if (registration.components.empty()) {
            state->registrations.erase(owner);
        } else {
            state->registrations.insert_or_assign(owner, registration);
        }
        answered = state->answered;
    }
    m_state.notify_all();
    return answered;
}

void RegistrationCollector::start_collecting() {
    m_state.handle()->answered = false;
}

RegistrationCollector::Answer RegistrationCollector::collect() {
    auto state = m_state.handle();
    if (m_expected_owners.empty()) {
        const auto deadline = std::chrono::steady_clock::now() + m_registration_timeout;
        state.wait_until(deadline, [&state] { return state->cancelled; });
    } else {
        const auto deadline = std::chrono::steady_clock::now() + m_expected_owners_timeout;
        state.wait_until(deadline, [this, &state] { return state->cancelled or all_expected_registered(*state); });
    }

    Answer answer;
    for (auto const& [owner, registration] : state->registrations) {
        answer.registrations.push_back(registration);
        answer.owners.push_back(owner);
        if (not m_expected_owners.empty() and m_expected_owners.count(owner) == 0) {
            answer.unexpected_owners.push_back(owner);
        }
    }
    for (auto const& owner : m_expected_owners) {
        if (state->registrations.count(owner) == 0) {
            answer.missing_owners.push_back(owner);
        }
    }
    state->answered = true;
    return answer;
}

void RegistrationCollector::cancel() {
    m_state.handle()->cancelled = true;
    m_state.notify_all();
}

std::string RegistrationCollector::owner_of(Registration const& registration) const {
    return registration.owner.value_or(m_module_id);
}

bool RegistrationCollector::all_expected_registered(State const& state) const {
    return std::all_of(m_expected_owners.begin(), m_expected_owners.end(),
                       [&state](auto const& owner) { return state.registrations.count(owner) != 0; });
}

} // namespace module
