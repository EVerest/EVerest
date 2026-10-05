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
    /// \brief Whether the ISO 15118-20 SECC leaf (OCPP 2.1 V2G20Certificate) is maintained next to the ISO 15118-2
    /// one: OCPP 2.1 connection and V2GCertificateInstallationEnabled
    virtual bool v2g20_certificate_installation_enabled() const = 0;
    virtual void init_certificate_expiration_check_timers() = 0;
    virtual void stop_certificate_expiration_check_timers() = 0;

    virtual Get15118EVCertificateResponse
    on_get_15118_ev_certificate_request(const Get15118EVCertificateRequest& request) = 0;
};

class Security : public SecurityInterface {
public:
    Security(const FunctionalBlockContext& functional_block_context, MessageQueue<v2::MessageType>& message_queue,
             MessageLogging& logging, OcspUpdaterInterface& ocsp_updater,
             SecurityEventCallback security_event_callback);
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

    /// \brief Sends a SignCertificate.req for \p certificate_signing_use (see \ref sign_certificate_req)
    /// \return false when nothing was sent: another request is outstanding, or the CSR could not be built
    bool send_sign_certificate_req(const ocpp::CertificateSigningUseEnum& certificate_signing_use,
                                   const bool initiated_by_trigger_message = false);

    /// \brief Forgets the last SignCertificate.req, once the CSMS has answered or rejected it.
    void reset_certificate_signing_state();

    /// \brief Allows new SignCertificate.req without forgetting the last one, whose CertificateSigned.req the CSMS
    /// may still send.
    void stop_awaiting_certificate_signed();

    /// \brief The V2G root the SECC leaf of \p certificate_signing_use is (or will be) issued under, for
    /// SignCertificateRequest.hashRootCertificate (A02.FR.27): the root of the installed leaf if there is one,
    /// otherwise the only installed V2G root. std::nullopt when this is ambiguous.
    std::optional<ocpp::CertificateHashDataType>
    get_secc_root_certificate_hash(const ocpp::CertificateSigningUseEnum& certificate_signing_use);

    // Members
    const FunctionalBlockContext& context;
    MessageQueue<v2::MessageType>& message_queue;
    MessageLogging& logging;
    OcspUpdaterInterface& ocsp_updater;

    SecurityEventCallback security_event_callback;

    int csr_attempt;
    /// \brief Type of the last SignCertificate.req. A CertificateSigned.req answers it until the next one is sent.
    std::optional<ocpp::CertificateSigningUseEnum> requested_certificate_signing_use;
    /// \brief requestId of the last SignCertificate.req (OCPP 2.1, A02.FR.24). A CertificateSigned.req that carries
    /// a different requestId is rejected (A02.FR.26). Not set on OCPP 2.0.1, whose schema lacks the field.
    std::optional<std::int32_t> sign_certificate_request_id;
    /// \brief No new SignCertificate.req is sent while the CertificateSigned.req for the last one is awaited
    bool awaiting_certificate_signed{false};
    std::int32_t next_sign_certificate_request_id;
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

    /// \brief Request a new SECC leaf (V2GCertificate or V2G20Certificate) when it is missing or expires within 30
    /// days
    /// \return true when a SignCertificate.req for it was sent; false when it is not due or the request could not be
    /// sent
    bool renew_secc_certificate_if_due(const ocpp::CertificateSigningUseEnum& certificate_signing_use);

    /// \brief Which SECC leaf goes first when both are due, alternated per check: only one SignCertificate.req can
    /// be outstanding, and a fixed order would let a leaf the CSMS never issues starve the other
    bool check_v2g20_leaf_first{false};

    /// \brief The SECC leaf to request once the CSMS has answered the SignCertificate.req the expiry check sent
    /// for the other one. Requested only when it is still due by then.
    std::optional<ocpp::CertificateSigningUseEnum> queued_secc_renewal;

    /// \brief Requests the queued SECC leaf, if any. Called once the CSMS has answered the outstanding
    /// SignCertificate.req, with a CertificateSigned.req or a rejecting SignCertificate.conf.
    void request_queued_secc_renewal();

public:
    bool v2g20_certificate_installation_enabled() const override;

    /// \brief Requests a new SECC leaf for each one that is missing or expires within 30 days, one at a time: the
    /// second is queued until the CSMS has answered the first. Run by the v2g_certificate_expiration_check_timer.
    void check_secc_certificates_expiration();
};
} // namespace v2
} // namespace ocpp
