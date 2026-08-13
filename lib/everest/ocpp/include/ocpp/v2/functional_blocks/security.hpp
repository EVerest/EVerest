// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <optional>
#include <variant>

#include <ocpp/v2/message_handler.hpp>
#include <ocpp/v2/ocpp_types.hpp>
#include <ocpp/v2/ocsp_updater.hpp>

namespace ocpp {
class MessageLogging;

namespace v2 {
struct FunctionalBlockContext;

struct CertificateSignedRequest;
struct CertificateSignedResponse;
struct GetInstalledCertificateIdsRequest;
struct Get15118EVCertificateRequest;
struct Get15118EVCertificateResponse;
struct InstallCertificateRequest;
struct DeleteCertificateRequest;
struct SignCertificateResponse;

using SecurityEventCallback =
    std::function<void(const CiString<50>& event_type, const std::optional<CiString<255>>& tech_info)>;

class SecurityInterface : public MessageHandlerInterface {

public:
    ~SecurityInterface() override = default;
    virtual void security_event_notification_req(const CiString<50>& event_type,
                                                 const std::optional<CiString<255>>& tech_info,
                                                 const bool triggered_internally, const bool critical,
                                                 const std::optional<DateTime>& timestamp = std::nullopt) = 0;
    virtual void sign_certificate_req(const ocpp::CertificateSigningUseEnum& certificate_signing_use,
                                      const bool initiated_by_trigger_message = false) = 0;
    /// \brief Why a SignCertificate.req would not be sent if requested now, or std::nullopt when it would be.
    virtual std::optional<StatusInfo>
    is_sign_certificate_possible(const ocpp::CertificateSigningUseEnum& certificate_signing_use) const = 0;
    virtual void stop_certificate_signed_timer() = 0;
    virtual void init_certificate_expiration_check_timers() = 0;
    virtual void stop_certificate_expiration_check_timers() = 0;

    virtual Get15118EVCertificateResponse
    on_get_15118_ev_certificate_request(const Get15118EVCertificateRequest& request) = 0;
};

class Security : public SecurityInterface {
public:
    Security(const FunctionalBlockContext& functional_block_context, MessageLogging& logging,
             OcspUpdaterInterface& ocsp_updater, SecurityEventCallback security_event_callback);
    ~Security() override;
    void handle_message(const EnhancedMessage<MessageType>& message) override;
    void stop_certificate_signed_timer() override;
    void init_certificate_expiration_check_timers() override;
    void stop_certificate_expiration_check_timers() override;
    Get15118EVCertificateResponse
    on_get_15118_ev_certificate_request(const Get15118EVCertificateRequest& request) override;

    /* OCPP message requests */
    void security_event_notification_req(const CiString<50>& event_type, const std::optional<CiString<255>>& tech_info,
                                         const bool triggered_internally, const bool critical,
                                         const std::optional<DateTime>& timestamp = std::nullopt) override;
    void sign_certificate_req(const ocpp::CertificateSigningUseEnum& certificate_signing_use,
                              const bool initiated_by_trigger_message = false) override;
    std::optional<StatusInfo>
    is_sign_certificate_possible(const ocpp::CertificateSigningUseEnum& certificate_signing_use) const override;

private:
    /// \brief CSR device-model inputs, read together at a single point (see \ref get_csr_inputs).
    struct CsrInputs {
        std::string common_name;
        std::string organization;
        std::string country;
    };

    /// \brief Reads all CSR device model inputs, or names every missing one.
    std::variant<CsrInputs, StatusInfo>
    get_csr_inputs(const ocpp::CertificateSigningUseEnum& certificate_signing_use) const;

    /// \brief Stops awaiting a CertificateSigned.req, which the retry timer would otherwise be the only thing to do.
    void reset_certificate_signing_state();

    // Members
    const FunctionalBlockContext& context;
    MessageLogging& logging;
    OcspUpdaterInterface& ocsp_updater;

    SecurityEventCallback security_event_callback;

    int csr_attempt;
    std::optional<ocpp::CertificateSigningUseEnum> awaited_certificate_signing_use_enum;
    Everest::SteadyTimer certificate_signed_timer;
    Everest::SteadyTimer client_certificate_expiration_check_timer;
    Everest::SteadyTimer v2g_certificate_expiration_check_timer;

    // Functions
    /* OCPP message handlers */

    // Functional Block A: Security
    void handle_certificate_signed_req(Call<CertificateSignedRequest> call);
    void handle_sign_certificate_response(CallResult<SignCertificateResponse> call_result);

    // Functional Block M: ISO 15118 Certificate Management
    void handle_get_installed_certificate_ids_req(Call<GetInstalledCertificateIdsRequest> call);
    void handle_install_certificate_req(Call<InstallCertificateRequest> call);
    void handle_delete_certificate_req(Call<DeleteCertificateRequest> call);

    // Internal helper functions

    /// \brief Helper function to determine if a certificate installation should be allowed
    /// \param cert_type is the certificate type to be checked
    /// \return std::nullopt if it should be allowed, otherwise the StatusInfo describing why it was refused
    std::optional<StatusInfo> check_certificate_install_allowed(InstallCertificateUseEnum cert_type) const;
    void scheduled_check_client_certificate_expiration();
    void scheduled_check_v2g_certificate_expiration();
};
} // namespace v2
} // namespace ocpp
