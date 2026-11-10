// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/util/async/monitor.hpp>
#include <everest_api_types/ocpp_device_model/API.hpp>

#include <chrono>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace module {

/// \brief Splits a comma separated list of owner names, ignoring whitespace around names and empty entries
std::set<std::string> parse_owner_list(std::string const& list);

/// \brief Collects the registrations that API clients publish, and answers get_registrations from them
class RegistrationCollector {
public:
    using Registration = everest::lib::API::V1_0::types::ocpp_device_model::Registration;

    struct Answer {
        std::vector<Registration> registrations;
        /// the owner of each registration, in the same order; the module id for a registration that names none
        std::vector<std::string> owners;
        /// expected owners without a registration when the answer was given
        std::vector<std::string> missing_owners;
        /// registered owners that are not in a non-empty list of expected owners
        std::vector<std::string> unexpected_owners;
    };

    /// \param expected_owners comma separated owner names, see parse_owner_list
    /// \param expected_owners_timeout maximum wait for the expected owners
    /// \param registration_timeout time to collect when no owners are expected
    RegistrationCollector(std::string module_id, std::string const& expected_owners,
                          std::chrono::milliseconds expected_owners_timeout,
                          std::chrono::milliseconds registration_timeout);

    std::vector<std::string> expected_owners() const;

    /// \brief Records the latest registration of its owner; an empty components list removes the owner
    /// \returns true when collect() has answered and no collection was started since, meaning that the
    /// registration is passed on at once
    bool add(Registration const& registration);

    /// \brief Starts a collection: registrations added from here on are part of the answer of collect() and are
    /// not passed on. Called before clients are prompted, so that none of their answers is passed on as well.
    void start_collecting();

    /// \brief Waits until every expected owner registered or the deadline passed, or for the registration
    /// timeout when no owners are expected, and returns the registrations recorded by then
    Answer collect();

    /// \brief Ends a collect() in progress and every later one without waiting
    void cancel();

private:
    struct State {
        std::map<std::string, Registration> registrations;
        bool answered{false};
        bool cancelled{false};
    };

    std::string owner_of(Registration const& registration) const;
    bool all_expected_registered(State const& state) const;

    std::string m_module_id;
    std::set<std::string> m_expected_owners;
    std::chrono::milliseconds m_expected_owners_timeout;
    std::chrono::milliseconds m_registration_timeout;
    everest::lib::util::monitor<State> m_state;
};

} // namespace module
