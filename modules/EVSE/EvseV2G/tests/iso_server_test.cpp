// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso_server.hpp>

#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <everest/tls/openssl_util.hpp>

#include "ISO15118_chargerImplStub.hpp"
#include "evse_securityIntfStub.hpp"
#include "iso15118_extensionsImplStub.hpp"
#include "utest_log.hpp"
#include "v2g.hpp"
#include "v2g_ctx.hpp"

#include <cstring>
#include <memory>
#include <vector>

uint64_t v2g_session_id_from_exi(bool is_iso, void* exi_in) {
    return 0;
}

namespace {

constexpr uint8_t offered_sa_schedule_tuple_id = 1;
constexpr uint8_t unknown_sa_schedule_tuple_id = 99;

class IsoServerPowerDeliveryTest : public testing::Test {
protected:
    std::unique_ptr<v2g_connection> conn;
    std::unique_ptr<v2g_context> ctx;
    std::unique_ptr<iso2_exiDocument> exi_in;
    std::unique_ptr<iso2_exiDocument> exi_out;

    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger;

    IsoServerPowerDeliveryTest() : charger(adapter) {
    }

    void SetUp() override {
        conn = std::make_unique<v2g_connection>();
        ctx = std::make_unique<v2g_context>();
        exi_in = std::make_unique<iso2_exiDocument>();
        exi_out = std::make_unique<iso2_exiDocument>();

        module::stub::clear_logs();
        conn->ctx = ctx.get();
        conn->ctx->p_charger = &charger;
        conn->exi_in.iso2EXIDocument = exi_in.get();
        conn->exi_out.iso2EXIDocument = exi_out.get();

        // Same SAScheduleList the module offers in ChargeParameterDiscoveryRes
        v2g_ctx_init_charging_values(ctx.get());

        ctx->is_dc_charger = false;
        ctx->contactor_is_closed = true;
        ctx->last_v2g_msg = V2G_CHARGE_PARAMETER_DISCOVERY_MSG;
        ctx->state = static_cast<int>(iso_ac_state_id::WAIT_FOR_POWERDELIVERY);

        init_iso2_exiDocument(exi_in.get());
        exi_in->V2G_Message.Body.PowerDeliveryReq_isUsed = 1u;
        auto& req = exi_in->V2G_Message.Body.PowerDeliveryReq;
        init_iso2_PowerDeliveryReqType(&req);
        req.ChargeProgress = iso2_chargeProgressType_Start;
    }

    void set_charging_profile(int16_t max_power_w) {
        auto& req = exi_in->V2G_Message.Body.PowerDeliveryReq;
        req.ChargingProfile_isUsed = 1u;
        req.ChargingProfile.ProfileEntry.arrayLen = 1;
        auto& entry = req.ChargingProfile.ProfileEntry.array[0];
        entry.ChargingProfileEntryStart = 0;
        entry.ChargingProfileEntryMaxPower.Value = max_power_w;
        entry.ChargingProfileEntryMaxPower.Multiplier = 0;
        entry.ChargingProfileEntryMaxPower.Unit = iso2_unitSymbolType_W;
    }

    void set_offered_pmax(int16_t pmax_w) {
        auto& pmax = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple.array[0]
                         .PMaxSchedule.PMaxScheduleEntry.array[0]
                         .PMax;
        pmax.Value = pmax_w;
        pmax.Multiplier = 0;
    }

    const iso2_PowerDeliveryResType& response() const {
        return exi_out->V2G_Message.Body.PowerDeliveryRes;
    }
};

TEST_F(IsoServerPowerDeliveryTest, offered_list_has_single_tuple) {
    const auto& tuples = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple;
    EXPECT_EQ(tuples.arrayLen, 1);
    EXPECT_EQ(tuples.array[0].SAScheduleTupleID, offered_sa_schedule_tuple_id);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_with_charging_profile_is_rejected) {
    // [V2G2-479]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;
    set_charging_profile(11000);
    ctx->session.sa_schedule_tuple_id = offered_sa_schedule_tuple_id;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(exi_out->V2G_Message.Body.PowerDeliveryRes_isUsed, 1u);
    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
    EXPECT_EQ(ctx->session.sa_schedule_tuple_id, offered_sa_schedule_tuple_id);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_with_full_tuple_list_is_rejected) {
    // [V2G2-479]
    auto& tuples = ctx->evse_v2g_data.evse_sa_schedule_list.SAScheduleTuple;
    for (uint8_t idx = 1; idx < iso2_SAScheduleTupleType_3_ARRAY_SIZE; idx++) {
        tuples.array[idx] = tuples.array[0];
        tuples.array[idx].SAScheduleTupleID = offered_sa_schedule_tuple_id + idx;
    }
    tuples.arrayLen = iso2_SAScheduleTupleType_3_ARRAY_SIZE;
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;
    set_charging_profile(11000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
}

TEST_F(IsoServerPowerDeliveryTest, unknown_tuple_id_without_charging_profile_is_rejected) {
    // [V2G2-479]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = unknown_sa_schedule_tuple_id;

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
}

TEST_F(IsoServerPowerDeliveryTest, offered_tuple_id_with_charging_profile_within_pmax_is_accepted) {
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = offered_sa_schedule_tuple_id;
    set_offered_pmax(11000);
    set_charging_profile(11000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_OK);
    EXPECT_EQ(ctx->session.sa_schedule_tuple_id, offered_sa_schedule_tuple_id);
    EXPECT_EQ(ctx->state, static_cast<int>(iso_ac_state_id::WAIT_FOR_CHARGINGSTATUS));
}

TEST_F(IsoServerPowerDeliveryTest, offered_tuple_id_with_charging_profile_above_pmax_is_rejected) {
    // [V2G2-224]
    exi_in->V2G_Message.Body.PowerDeliveryReq.SAScheduleTupleID = offered_sa_schedule_tuple_id;
    set_offered_pmax(11000);
    set_charging_profile(22000);

    EXPECT_EQ(iso_handle_request(conn.get()), V2G_EVENT_NO_EVENT);

    EXPECT_EQ(response().ResponseCode, iso2_responseCodeType_FAILED_ChargingProfileInvalid);
}

constexpr char test_emaid[] = "DEPNXC00000001";

std::vector<std::uint8_t> self_signed_contract_certificate_der(const char* common_name) {
    std::vector<std::uint8_t> result;
    auto* pkey = EVP_EC_gen("prime256v1");
    auto* cert = X509_new();
    if ((pkey != nullptr) && (cert != nullptr)) {
        X509_set_version(cert, X509_VERSION_3);
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), 0);
        X509_gmtime_adj(X509_getm_notAfter(cert), 60 * 60);
        X509_set_pubkey(cert, pkey);
        auto* name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>(common_name), -1,
                                   -1, 0);
        X509_set_issuer_name(cert, name);
        if (X509_sign(cert, pkey, EVP_sha256()) > 0) {
            unsigned char* der{nullptr};
            const auto len = i2d_X509(cert, &der);
            if (len > 0) {
                result.assign(der, der + len);
            }
            OPENSSL_free(der);
        }
    }
    X509_free(cert);
    EVP_PKEY_free(pkey);
    return result;
}

class IsoServerPaymentTest : public testing::Test {
protected:
    std::unique_ptr<v2g_context> ctx;
    std::unique_ptr<v2g_connection> conn;
    std::unique_ptr<iso2_exiDocument> exi_in;
    std::unique_ptr<iso2_exiDocument> exi_out;
    openssl::pkey_ptr contract_public_key{nullptr, nullptr};

    module::stub::QuietModuleAdapterStub adapter;
    module::stub::ISO15118_chargerImplStub charger;
    module::stub::evse_securityIntfStub security;
    module::stub::iso15118_extensionsImplStub extensions;

    IsoServerPaymentTest() : charger(adapter), security(adapter) {
    }

    void SetUp() override {
        ctx = std::make_unique<v2g_context>();
        ctx->p_charger = &charger;
        ctx->p_extensions = &extensions;
        ctx->r_security = &security;
        ctx->is_dc_charger = true;
        v2g_ctx_init_charging_session(ctx.get(), false);

        conn = std::make_unique<v2g_connection>();
        exi_in = std::make_unique<iso2_exiDocument>();
        exi_out = std::make_unique<iso2_exiDocument>();

        module::stub::clear_logs();
        conn->ctx = ctx.get();
        conn->exi_in.iso2EXIDocument = exi_in.get();
        conn->exi_out.iso2EXIDocument = exi_out.get();
        init_iso2_exiDocument(exi_in.get());
        init_iso2_BodyType(&exi_in->V2G_Message.Body);
    }

    void use_tls_connection() {
        conn->is_tls_connection = true;
        conn->pubkey = &contract_public_key;
    }

    void use_tcp_connection() {
        conn->is_tls_connection = false;
        conn->pubkey = nullptr;
    }

    void select_contract(const std::vector<iso2_paymentOptionType>& offered) {
        ctx->evse_v2g_data.payment_option_list = offered;
        ctx->state = static_cast<int>(iso_dc_state_id::WAIT_FOR_SVCDETAIL_PAYMENTSVCSEL);

        exi_in->V2G_Message.Body.PaymentServiceSelectionReq_isUsed = 1;
        auto& req = exi_in->V2G_Message.Body.PaymentServiceSelectionReq;
        init_iso2_PaymentServiceSelectionReqType(&req);
        req.SelectedPaymentOption = iso2_paymentOptionType_Contract;
        req.SelectedServiceList.SelectedService.array[0].ServiceID = V2G_SERVICE_ID_CHARGING;
        req.SelectedServiceList.SelectedService.arrayLen = 1;

        iso_handle_request(conn.get());
    }

    void send_payment_details(const std::vector<std::uint8_t>& certificate) {
        ctx->session.iso_selected_payment_option = iso2_paymentOptionType_Contract;
        ctx->session.verify_contract_cert_chain = false;
        ctx->state = static_cast<int>(iso_dc_state_id::WAIT_FOR_PAYMENTDETAILS_CERTINST_CERTUPD);

        request_payment_details(certificate);
    }

    v2g_event request_payment_details(const std::vector<std::uint8_t>& certificate) {
        init_iso2_BodyType(&exi_in->V2G_Message.Body);
        exi_in->V2G_Message.Body.PaymentDetailsReq_isUsed = 1;
        auto& req = exi_in->V2G_Message.Body.PaymentDetailsReq;
        init_iso2_PaymentDetailsReqType(&req);
        std::memcpy(req.eMAID.characters, test_emaid, sizeof(test_emaid) - 1);
        req.eMAID.charactersLen = sizeof(test_emaid) - 1;
        std::memcpy(req.ContractSignatureCertChain.Certificate.bytes, certificate.data(), certificate.size());
        req.ContractSignatureCertChain.Certificate.bytesLen = certificate.size();

        return iso_handle_request(conn.get());
    }

    void send_signed_authorization() {
        ctx->session.iso_selected_payment_option = iso2_paymentOptionType_Contract;
        ctx->state = static_cast<int>(iso_dc_state_id::WAIT_FOR_AUTHORIZATION);
        ctx->last_v2g_msg = V2G_PAYMENT_DETAILS_MSG;

        exi_in->V2G_Message.Header.Signature_isUsed = 1;
        exi_in->V2G_Message.Body.AuthorizationReq_isUsed = 1;
        auto& req = exi_in->V2G_Message.Body.AuthorizationReq;
        init_iso2_AuthorizationReqType(&req);
        req.GenChallenge_isUsed = 1;
        req.GenChallenge.bytesLen = sizeof(ctx->session.gen_challenge);
        std::memcpy(req.GenChallenge.bytes, ctx->session.gen_challenge, sizeof(ctx->session.gen_challenge));

        iso_handle_request(conn.get());
    }

    iso2_responseCodeType payment_service_selection_response() const {
        return exi_out->V2G_Message.Body.PaymentServiceSelectionRes.ResponseCode;
    }

    iso2_responseCodeType payment_details_response() const {
        return exi_out->V2G_Message.Body.PaymentDetailsRes.ResponseCode;
    }

    iso2_responseCodeType authorization_response() const {
        return exi_out->V2G_Message.Body.AuthorizationRes.ResponseCode;
    }
};

TEST_F(IsoServerPaymentTest, contract_selected_when_offered) {
    use_tls_connection();
    select_contract({iso2_paymentOptionType_Contract, iso2_paymentOptionType_ExternalPayment});

    EXPECT_EQ(payment_service_selection_response(), iso2_responseCodeType_OK);
    EXPECT_EQ(ctx->session.iso_selected_payment_option, iso2_paymentOptionType_Contract);
    EXPECT_EQ(ctx->state, static_cast<int>(iso_dc_state_id::WAIT_FOR_PAYMENTDETAILS_CERTINST_CERTUPD));
}

TEST_F(IsoServerPaymentTest, contract_rejected_when_not_offered) {
    use_tcp_connection();
    select_contract({iso2_paymentOptionType_ExternalPayment});

    EXPECT_EQ(payment_service_selection_response(), iso2_responseCodeType_FAILED_PaymentSelectionInvalid);
    EXPECT_NE(ctx->session.iso_selected_payment_option, iso2_paymentOptionType_Contract);
    EXPECT_NE(ctx->state, static_cast<int>(iso_dc_state_id::WAIT_FOR_PAYMENTDETAILS_CERTINST_CERTUPD));
}

TEST_F(IsoServerPaymentTest, payment_details_accepts_parsable_certificate) {
    use_tls_connection();
    const auto certificate = self_signed_contract_certificate_der(test_emaid);
    ASSERT_FALSE(certificate.empty());

    send_payment_details(certificate);

    EXPECT_EQ(payment_details_response(), iso2_responseCodeType_OK);
    EXPECT_NE(contract_public_key, nullptr);
}

TEST_F(IsoServerPaymentTest, payment_details_rejects_unparsable_certificate) {
    use_tls_connection();
    const std::vector<std::uint8_t> garbage(64, 0xA5);

    send_payment_details(garbage);

    EXPECT_EQ(payment_details_response(), iso2_responseCodeType_FAILED_CertChainError);
    EXPECT_EQ(contract_public_key, nullptr);
}

TEST_F(IsoServerPaymentTest, unparsable_certificate_after_rejected_contract_selection) {
    ctx->terminate_connection_on_failed_response = false;
    use_tcp_connection();
    select_contract({iso2_paymentOptionType_ExternalPayment});
    ASSERT_EQ(payment_service_selection_response(), iso2_responseCodeType_FAILED_PaymentSelectionInvalid);

    const std::vector<std::uint8_t> garbage(64, 0xA5);

    EXPECT_EQ(request_payment_details(garbage), V2G_EVENT_SEND_AND_TERMINATE);
    EXPECT_EQ(payment_details_response(), iso2_responseCodeType_FAILED_SequenceError);
}

TEST_F(IsoServerPaymentTest, payment_details_without_public_key_slot) {
    use_tcp_connection();
    const auto certificate = self_signed_contract_certificate_der(test_emaid);
    ASSERT_FALSE(certificate.empty());

    send_payment_details(certificate);

    EXPECT_EQ(payment_details_response(), iso2_responseCodeType_FAILED_CertChainError);
}

TEST_F(IsoServerPaymentTest, authorization_without_public_key_slot) {
    use_tcp_connection();

    send_signed_authorization();

    EXPECT_EQ(authorization_response(), iso2_responseCodeType_FAILED_SignatureError);
}

TEST_F(IsoServerPaymentTest, authorization_without_public_key) {
    use_tls_connection();

    send_signed_authorization();

    EXPECT_EQ(authorization_response(), iso2_responseCodeType_FAILED_SignatureError);
}

} // namespace
