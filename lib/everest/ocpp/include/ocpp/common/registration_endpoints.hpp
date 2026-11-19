// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <string>

#include <everest/util/async/monitor.hpp>
#include <ocpp/common/types.hpp>
#include <ocpp/common/websocket/websocket_uri.hpp>

namespace ocpp {

/// \brief The websocket URL that \p ocpp_csms_url stands for, without the \p identity the connection appends, so that
/// two spellings of one CSMS URL compare equal. A URL without a scheme is taken as `ws://`: the security profile picks
/// its scheme when connecting, and a change of security profile does not change the CSMS. A URL that cannot be parsed
/// is returned as written
inline std::string normalised_csms_url(const std::string& ocpp_csms_url, const std::string& identity) {
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    // The security profile only has to match the scheme for parse_and_validate() to accept the URL
    const bool secure = lower(ocpp_csms_url).rfind("wss://", 0) == 0;
    const auto security_profile = secure ? security::SecurityProfile::TLS_WITH_BASIC_AUTHENTICATION
                                         : security::SecurityProfile::UNSECURED_TRANSPORT_WITH_BASIC_AUTHENTICATION;
    try {
        auto uri = Uri::parse_and_validate(ocpp_csms_url, identity, security_profile);
        return ev_uri(secure, lower(uri.get_hostname()), uri.get_port(), uri.get_path()).str();
    } catch (const std::invalid_argument&) {
        return ocpp_csms_url;
    }
}

/// \brief A network connection profile as far as registration is concerned: a different configuration slot, or a
/// different CSMS behind the same slot, may not know the charging station. \p ocpp_csms_url is a normalised_csms_url()
struct RegistrationEndpoint {
    int configuration_slot;
    std::string ocpp_csms_url;

    bool operator==(const RegistrationEndpoint& other) const {
        return configuration_slot == other.configuration_slot and ocpp_csms_url == other.ocpp_csms_url;
    }
    bool operator!=(const RegistrationEndpoint& other) const {
        return not(*this == other);
    }
};

/// \brief Tracks the endpoint of the current CSMS connection and the endpoint whose CSMS last answered a
/// BootNotification.req, to tell when the charging station has to register again (B10.FR.10)
class RegistrationEndpoints {
public:
    /// \brief Records \p endpoint as the endpoint of the current connection
    /// \return the endpoint whose CSMS last answered a BootNotification.req, if there is one and it differs from
    /// \p endpoint
    std::optional<RegistrationEndpoint> connect(const RegistrationEndpoint& endpoint) {
        auto state = this->m_state.handle();
        state->connected = endpoint;
        if (state->registered.has_value() and state->registered.value() != endpoint) {
            return state->registered;
        }
        return std::nullopt;
    }

    /// \brief Records the endpoint of the current connection as the one whose CSMS last answered a
    /// BootNotification.req
    void register_connected() {
        auto state = this->m_state.handle();
        state->registered = state->connected;
    }

private:
    struct State {
        std::optional<RegistrationEndpoint> connected;
        std::optional<RegistrationEndpoint> registered;
    };
    everest::lib::util::monitor<State> m_state;
};

} // namespace ocpp
