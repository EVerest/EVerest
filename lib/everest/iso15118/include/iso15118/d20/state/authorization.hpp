// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../states.hpp"
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/certificate_installation.hpp>

namespace iso15118::d20::state {

// Authorization phase: AuthorizationReq (EIM or PnC) and, when the certificate installation service was
// offered, CertificateInstallationReq in the order the EV chooses ([V2G20-1970], [V2G20-1583]).
struct Authorization : public StateBase {
public:
    // \p challenge is present only when PnC was offered, handed over by AuthorizationSetup to the one
    // place [V2G20-2565] needs it. Absent never compares equal, so an EIM-only session cannot match.
    Authorization(Context& ctx, std::optional<dt::GenChallenge> challenge = std::nullopt) :
        StateBase(ctx, StateID::Authorization), gen_challenge(std::move(challenge)){};

    void enter() final;

    Result feed(Event) final;

private:
    Result handle_authorization_request(const message_20::AuthorizationRequest& req,
                                        const message_20::Variant& variant);
    Result handle_certificate_installation_request(const message_20::CertificateInstallationRequest& req,
                                                   const message_20::Variant& variant);
    void respond_certificate_installation(const message_20::CertificateInstallationRequest& req,
                                          message_20::datatypes::ResponseCode code,
                                          message_20::datatypes::Processing processing);
    Result finish_authorized(message_20::datatypes::Authorization via, bool allow_installation);

    message_20::datatypes::AuthStatus authorization_status{message_20::datatypes::AuthStatus::Pending};
    std::optional<AuthorizationResponse> backend_response{};
    bool timeout_ongoing_reached{false};
    bool eim_timer_started{false};

    // The contract chain handed to the backend; the EV repeats the unaltered request while the answer
    // is pending ([V2G20-1582]).
    struct PncAttempt {
        std::vector<uint8_t> leaf_der;
        std::string emaid;
        std::string chain_pem;
        bool expires_soon{false};
        std::vector<uint8_t> request;
    };
    const std::optional<dt::GenChallenge> gen_challenge;
    std::optional<PncAttempt> pnc_attempt{};

    // CertificateInstallation relay ([V2G20-1972..1975]).
    bool cert_install_forwarded{false};
    bool cert_install_failed{false};
    std::optional<std::vector<uint8_t>> cert_install_raw_response{};
    std::optional<uint8_t> cert_install_remaining{};
};

} // namespace iso15118::d20::state
