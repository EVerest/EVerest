// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <mutex>
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
    /// \brief Whether the ISO 15118-20 SECC leaf (V2G20Certificate) is maintained next to the ISO 15118-2 one:
    /// V2GCertificateInstallationEnabled and V2G20CertificateInstallationEnabled
    virtual bool v2g20_certificate_installation_enabled() const = 0;
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

    /// \brief The SECC leaf to turn to once the current SECC signing round has ended (CertificateSigned.req handled,
    /// SignCertificate.req rejected or its retries given up). Only one SignCertificate.req is outstanding at a time,
    /// and before OCPP 2.1 both SECC leafs are requested as V2GCertificate, so the two CSRs are kept apart in time:
    /// the follow-up is handled V2GCertificateExpireCheckInitialDelaySeconds after the round has ended.
    struct SeccFollowUp {
        ocpp::CertificateSigningUseEnum certificate_signing_use;
        bool regardless_of_expiry; ///< TriggerMessage semantics: request the leaf even when it is not due
    };
    void set_secc_follow_up(const SeccFollowUp& follow_up);
    std::optional<SeccFollowUp> take_secc_follow_up();

    /// \brief Re-arms the SECC expiry check after V2GCertificateExpireCheckInitialDelaySeconds when \p finished is a
    /// SECC leaf and a follow-up is pending
    void on_secc_signing_round_finished(const ocpp::CertificateSigningUseEnum& finished);

    /// \brief Ends the outstanding SignCertificate.req round without a CertificateSigned.req
    void abandon_certificate_signing_round();

    /// \brief The V2G root the SECC leaf of \p certificate_signing_use is (or will be) issued under, for
    /// SignCertificateRequest.hashRootCertificate (A02.FR.27): the root of the installed leaf if there is one,
    /// otherwise the only installed V2G root. std::nullopt when this is ambiguous.
    std::optional<ocpp::CertificateHashDataType>
    get_secc_root_certificate_hash(const ocpp::CertificateSigningUseEnum& certificate_signing_use);

    // Members
    const FunctionalBlockContext& context;
    MessageLogging& logging;
    OcspUpdaterInterface& ocsp_updater;

    SecurityEventCallback security_event_callback;

    int csr_attempt;
    std::optional<ocpp::CertificateSigningUseEnum> awaited_certificate_signing_use_enum;
    /// \brief requestId of the outstanding SignCertificate.req (OCPP 2.1, A02.FR.24). A CertificateSigned.req that
    /// carries a different requestId is rejected (A02.FR.26). Not set on OCPP 2.0.1, whose schema lacks the field.
    std::optional<std::int32_t> awaited_sign_certificate_request_id;
    std::int32_t next_sign_certificate_request_id;
    /// \brief Written by the message handlers, consumed by the expiry check timer
    std::optional<SeccFollowUp> secc_follow_up;
    std::mutex secc_follow_up_mutex;
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

    /// \brief Whether one SECC leaf (V2GCertificate or V2G20Certificate) is missing or expires within 30 days
    bool is_secc_certificate_due(const ocpp::CertificateSigningUseEnum& certificate_signing_use) const;

public:
    bool v2g20_certificate_installation_enabled() const override;
};
} // namespace v2
} // namespace ocpp
