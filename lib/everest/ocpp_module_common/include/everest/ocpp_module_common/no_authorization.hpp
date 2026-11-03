// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <everest/util/async/monitor.hpp>
#include <generated/types/authorization.hpp>
#include <generated/types/evse_manager.hpp>

namespace ocpp_module_common {

/// \brief idTag of the token that authorises a session while OCPP 1.6 AuthEnabled is false. StartTransaction.req
/// requires a non-empty idTag, while OCPP 2.x leaves the idToken of type NoAuthorization empty.
inline const std::string NO_AUTHORIZATION_ID_TAG_V16 = "FREESERVICE";

/// \brief Prevalidated token of type NoAuthorization that authorises \p evse_id while authorisation is disabled
/// (OCPP 2.x AuthCtrlr.Enabled, OCPP 1.6 AuthEnabled)
inline types::authorization::ProvidedIdToken make_no_authorization_token(std::int32_t evse_id,
                                                                         const std::string& id_token_value) {
    types::authorization::ProvidedIdToken provided_token;
    provided_token.id_token = {id_token_value, types::authorization::IdTokenType::NoAuthorization};
    provided_token.authorization_type = types::authorization::AuthorizationType::OCPP;
    provided_token.prevalidated = true;
    provided_token.connectors = std::vector<std::int32_t>{evse_id};
    return provided_token;
}

/// \brief Authorises the EVSEs that signalled the session event AuthRequired while authorisation is disabled (OCPP 2.x
/// AuthCtrlr.Enabled, OCPP 1.6 AuthEnabled). An EVSE that waits before the setting is read, or while authorisation is
/// still enabled, is authorised once it is read or switched to disabled.
class NoAuthorizationAuthorizer {
public:
    using AuthoriseCallback = std::function<void(std::int32_t evse_id)>;

    explicit NoAuthorizationAuthorizer(AuthoriseCallback authorise) : m_authorise(std::move(authorise)) {
    }

    void set_auth_enabled(bool auth_enabled) {
        // taken under the same lock as the switch, so an EVSE that starts waiting afterwards is authorised only by
        // its AuthRequired
        std::set<std::int32_t> evse_ids;
        {
            auto state = m_state.handle();
            const bool was_disabled = state->auth_enabled == false;
            state->auth_enabled = auth_enabled;
            if (not auth_enabled and not was_disabled) {
                evse_ids = state->waiting_evse_ids;
            }
        }
        authorise(evse_ids);
    }

    /// \brief Authorises the waiting EVSEs again while authorisation is disabled, e.g. when OCPP restarts after
    /// tokens were dropped while it was stopped
    void retry() {
        std::set<std::int32_t> evse_ids;
        {
            auto state = m_state.handle();
            if (state->auth_enabled == false) {
                evse_ids = state->waiting_evse_ids;
            }
        }
        authorise(evse_ids);
    }

    void on_session_event(std::int32_t evse_id, const types::evse_manager::SessionEvent& session_event) {
        using types::evse_manager::SessionEventEnum;
        if (session_event.event == SessionEventEnum::AuthRequired) {
            bool auth_disabled{false};
            {
                auto state = m_state.handle();
                state->waiting_evse_ids.insert(evse_id);
                auth_disabled = state->auth_enabled == false;
            }
            if (auth_disabled) {
                m_authorise(evse_id);
            }
        } else if (session_event.event == SessionEventEnum::Authorized or
                   session_event.event == SessionEventEnum::Deauthorized or
                   session_event.event == SessionEventEnum::TransactionStarted or
                   session_event.event == SessionEventEnum::SessionFinished) {
            m_state.handle()->waiting_evse_ids.erase(evse_id);
        }
    }

private:
    void authorise(const std::set<std::int32_t>& evse_ids) {
        for (const auto evse_id : evse_ids) {
            m_authorise(evse_id);
        }
    }

    struct State {
        // unset until the setting was read
        std::optional<bool> auth_enabled;
        std::set<std::int32_t> waiting_evse_ids;
    };

    AuthoriseCallback m_authorise;
    everest::lib::util::monitor<State> m_state;
};

/// \brief Whether \p provided_token was provided by a remote start from the CSMS. Every OCPP token is one, except the
/// NoAuthorization token without remoteStartId (request_id) that authorises a plug-in while authorisation is disabled.
inline bool is_remote_start(const types::authorization::ProvidedIdToken& provided_token) {
    return provided_token.authorization_type == types::authorization::AuthorizationType::OCPP and
           (provided_token.request_id.has_value() or
            provided_token.id_token.type != types::authorization::IdTokenType::NoAuthorization);
}

} // namespace ocpp_module_common
