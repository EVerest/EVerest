// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <ocpp/v2/ctrlr_component_variables.hpp>
#include <ocpp/v2/device_model.hpp>
#include <ocpp/v2/functional_blocks/functional_block_context.hpp>

#define private public // Make everything in security.hpp public so we can trigger the timer.
#include <ocpp/v2/functional_blocks/security.hpp>
#undef private
#include <ocpp/v2/messages/CertificateSigned.hpp>
#include <ocpp/v2/messages/InstallCertificate.hpp>
#include <ocpp/v2/messages/Reset.hpp>
#include <ocpp/v2/messages/SecurityEventNotification.hpp>
#include <ocpp/v2/messages/SignCertificate.hpp>

#include "component_state_manager_mock.hpp"
#include "connectivity_manager_mock.hpp"
#include "device_model_test_helper.hpp"
#include "evse_manager_fake.hpp"
#include "evse_security_mock.hpp"
#include "message_dispatcher_mock.hpp"
#include "mocks/database_handler_mock.hpp"
#include "ocsp_updater_mock.hpp"
#include "timer_stub.hpp"

using namespace ocpp;
using namespace ocpp::v2;
using ::testing::_;
using ::testing::Invoke;
using ::testing::MockFunction;
using ::testing::Return;

namespace {
/// \brief Removes \p variable_names from the SecurityCtrlr component before the device model is handed to the fixture,
/// so a test can exercise the paths taken when an optional device model value is absent. set_value cannot express this:
/// validate_value rejects an empty value for an integer variable.
DeviceModel* device_model_without_security_ctrlr_variables(DeviceModelTestHelper& device_model_test_helper,
                                                           const std::vector<std::string>& variable_names) {
    for (const auto& variable_name : variable_names) {
        EXPECT_TRUE(device_model_test_helper.remove_variable_from_db("SecurityCtrlr", std::nullopt, std::nullopt,
                                                                     std::nullopt, variable_name, std::nullopt));
    }
    return device_model_test_helper.get_device_model();
}
} // namespace

class SecurityTest : public ::testing::Test {
public:
protected: // Members
    DeviceModelTestHelper device_model_test_helper;
    DeviceModel* device_model;
    MockMessageDispatcher mock_dispatcher;
    ocpp::MessageLogging logging;
    ocpp::EvseSecurityMock evse_security;
    ConnectivityManagerMock connectivity_manager;
    EvseManagerFake evse_manager;
    ComponentStateManagerMock component_state_manager;
    ::testing::NiceMock<ocpp::v2::DatabaseHandlerMock> database_handler_mock;
    OcspUpdaterMock ocsp_updater;
    MockFunction<void(const ocpp::CiString<50>& event_type, const std::optional<ocpp::CiString<255>>& tech_info)>
        security_event_callback_mock;
    std::atomic<ocpp::OcppProtocolVersion> ocpp_version;
    FunctionalBlockContext functional_block_context;
    Security security;

protected: // Functions
    SecurityTest() : SecurityTest(std::vector<std::string>{}) {
    }

    explicit SecurityTest(const std::vector<std::string>& removed_security_ctrlr_variables) :
        device_model_test_helper(),
        device_model(
            device_model_without_security_ctrlr_variables(device_model_test_helper, removed_security_ctrlr_variables)),
        logging(false, "", "", false, false, false, false, false, false, false, nullptr),
        evse_security(),
        connectivity_manager(),
        evse_manager(2),
        component_state_manager(),
        ocpp_version(ocpp::OcppProtocolVersion::v201),
        functional_block_context{
            this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
            this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version},
        security(functional_block_context, logging, ocsp_updater, security_event_callback_mock.AsStdFunction()) {
    }

    ocpp::EnhancedMessage<MessageType> create_example_certificate_signed_request(
        const std::string& certificate_chain = "",
        const std::optional<ocpp::v2::CertificateSigningUseEnum> certificate_type = std::nullopt) {
        CertificateSignedRequest request;
        request.certificateChain = certificate_chain;
        request.certificateType = certificate_type;
        ocpp::Call<CertificateSignedRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::CertificateSigned;
        enhanced_message.message = call;
        return enhanced_message;
    }

    ocpp::EnhancedMessage<MessageType>
    create_example_sign_certificate_response(const GenericStatusEnum status = GenericStatusEnum::Accepted) {
        SignCertificateResponse response;
        response.status = status;
        ocpp::CallResult<SignCertificateResponse> call_result;
        call_result.msg = response;
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::SignCertificateResponse;
        enhanced_message.message = call_result;
        return enhanced_message;
    }

    void set_update_certificate_symlinks_enabled(DeviceModel* device_model, const bool enabled) {
        const auto& update_certificate_symlinks = ControllerComponentVariables::UpdateCertificateSymlinks;
        EXPECT_EQ(device_model->set_value(update_certificate_symlinks.component,
                                          update_certificate_symlinks.variable.value(), AttributeEnum::Actual,
                                          enabled ? "true" : "false", "default", true),
                  SetVariableStatusEnum::Accepted);
    }

    /// \brief Sets every device model value a ChargingStationCertificate CSR needs, so a test can reach the sending
    /// path without repeating the four set_value calls.
    void set_charging_station_csr_inputs() {
        this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                      ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                      AttributeEnum::Actual, "testserialnumber", "test", true);
        this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                      ControllerComponentVariables::OrganizationName.variable.value(),
                                      AttributeEnum::Actual, "testOrganization", "test", true);
        this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                      ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                      AttributeEnum::Actual, "testCountry", "test", true);
        this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                      ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual,
                                      "false", "test", true);
    }

    ocpp::EnhancedMessage<MessageType>
    create_example_install_certificate_request(const InstallCertificateUseEnum certificate_type) {
        InstallCertificateRequest request;
        request.certificateType = certificate_type;
        request.certificate = "";
        ocpp::Call<InstallCertificateRequest> call(request);
        ocpp::EnhancedMessage<MessageType> enhanced_message;
        enhanced_message.messageType = MessageType::InstallCertificate;
        enhanced_message.message = call;
        return enhanced_message;
    }

    void set_bool_variable(DeviceModel* device_model, const ComponentVariable& component_variable, const bool value) {
        EXPECT_EQ(device_model->set_value(component_variable.component, component_variable.variable.value(),
                                          AttributeEnum::Actual, value ? "true" : "false", "default", true),
                  SetVariableStatusEnum::Accepted);
    }

    void set_security_profile(DeviceModel* device_model, const int profile) {
        const auto& security_profile = ControllerComponentVariables::SecurityProfile;
        EXPECT_EQ(device_model->set_value(security_profile.component, security_profile.variable.value(),
                                          AttributeEnum::Actual, std::to_string(profile), "default", true),
                  SetVariableStatusEnum::Accepted);
    }
};

TEST_F(SecurityTest, handle_message_not_implemented) {
    // Try to handle a message with the wrong type, should throw an exception.
    ResetRequest request;
    request.type = ResetEnum::Immediate;
    ocpp::Call<ResetRequest> call(request);
    ocpp::EnhancedMessage<MessageType> enhanced_message;
    enhanced_message.messageType = MessageType::Reset;
    enhanced_message.message = call;

    EXPECT_THROW(security.handle_message(enhanced_message), MessageTypeNotImplementedException);
}

TEST_F(SecurityTest, handle_message_certificate_signed_v2gcertificate) {
    set_update_certificate_symlinks_enabled(this->device_model, true);

    // Leaf certificate should be updated.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::V2GCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    // For V2G certificates, OCSP cache update should be triggered.
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(1);
    // For V2G certificates, a symlink update should be triggered when that is set in the device model
    EXPECT_CALL(evse_security, update_certificate_links(ocpp::CertificateSigningUseEnum::V2GCertificate)).Times(1);
    // As updating the leaf certificate is accepted, the call result will be 'Accepted'.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Accepted);
    }));

    security.handle_message(
        create_example_certificate_signed_request("", ocpp::v2::CertificateSigningUseEnum::V2GCertificate));
}

TEST_F(SecurityTest, handle_message_certificate_signed_v2g20certificate) {
    set_update_certificate_symlinks_enabled(this->device_model, true);

    // The ISO 15118-20 leaf is installed as its own type ...
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::V2G20Certificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    // ... shares the SECC OCSP cache with the -2 leaf ...
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(1);
    // ... but the legacy -2 symlinks are never repointed at it, even with symlink updates enabled
    EXPECT_CALL(evse_security, update_certificate_links(_)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Accepted);
    }));

    security.handle_message(
        create_example_certificate_signed_request("", ocpp::v2::CertificateSigningUseEnum::V2G20Certificate));
}

TEST_F(SecurityTest, handle_message_certificate_signed_v2gcertificate_symlinks_disabled) {
    set_update_certificate_symlinks_enabled(this->device_model, false);

    // Leaf certificate should be updated.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::V2GCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    // For V2G certificates, OCSP cache update should be triggered.
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(1);
    // For V2G certificates, a symlink update should not be triggered when it is not set in the device model
    EXPECT_CALL(evse_security, update_certificate_links(ocpp::CertificateSigningUseEnum::V2GCertificate)).Times(0);
    // As updating the leaf certificate is accepted, the call result will be 'Accepted'.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Accepted);
    }));

    security.handle_message(
        create_example_certificate_signed_request("", ocpp::v2::CertificateSigningUseEnum::V2GCertificate));
}

TEST_F(SecurityTest, handle_message_certificate_signed_v2gcertificate_update_leaf_not_accepted) {
    set_update_certificate_symlinks_enabled(this->device_model, true);

    // Leaf certificate should be updated, returns 'Expired'.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::V2GCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Expired));
    // For V2G certificates, OCSP cache update should be triggered, but only when updating leaf certificate is accepted.
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(0);
    // For V2G certificates, a symlink update should be triggered when that is set in the device model
    EXPECT_CALL(evse_security, update_certificate_links(ocpp::CertificateSigningUseEnum::V2GCertificate)).Times(1);
    // As updating the leaf certificate is accepted, the call result will be 'Accepted'.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Rejected);
    }));
    // Install certificate is not accepted, this should trigger a security event notification.
    EXPECT_CALL(security_event_callback_mock,
                Call(CiString<50>("InvalidChargingStationCertificate"),
                     std::optional<CiString<255>>(ocpp::conversions::install_certificate_result_to_string(
                         ocpp::InstallCertificateResult::Expired))));

    security.handle_message(
        create_example_certificate_signed_request("", ocpp::v2::CertificateSigningUseEnum::V2GCertificate));
}

TEST_F(SecurityTest, handle_message_certificate_signed_chargingstationcertificate_accepted_securityprofile_3) {
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 3);

    // Leaf certificate should be updated.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::ChargingStationCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    // For Charging Station certificates, OCSP cache update should NOT be triggered.
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(0);
    // For V2G certificates, a symlink update should NOT be triggered, also not when it is set in the device model.
    EXPECT_CALL(evse_security, update_certificate_links(ocpp::CertificateSigningUseEnum::V2GCertificate)).Times(0);
    // As updating the leaf certificate is accepted, the call result will be 'Accepted'.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Accepted);
    }));
    // The connectivity manager should be informed of the changed certificate (because of security profile 3)
    EXPECT_CALL(connectivity_manager, on_charging_station_certificate_changed()).Times(1);
    // A security event notification should be sent (because of security profile 3)
    EXPECT_CALL(security_event_callback_mock,
                Call(CiString<50>("ReconfigurationOfSecurityParameters"),
                     std::optional<CiString<255>>("Changed charging station certificate")));

    security.handle_message(
        create_example_certificate_signed_request("", ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate));
}

TEST_F(SecurityTest, handle_message_certificate_signed_chargingstationcertificate_accepted_securityprofile_1) {
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 1);

    // Leaf certificate should be updated.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::ChargingStationCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    // For Charging Station certificates, OCSP cache update should NOT be triggered.
    EXPECT_CALL(ocsp_updater, trigger_ocsp_cache_update()).Times(0);
    // For V2G certificates, a symlink update should NOT be triggered, also not when it is set in the device model.
    EXPECT_CALL(evse_security, update_certificate_links(ocpp::CertificateSigningUseEnum::V2GCertificate)).Times(0);
    // As updating the leaf certificate is accepted, the call result will be 'Accepted'.
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<CertificateSignedResponse>();
        EXPECT_EQ(response.status, CertificateSignedStatusEnum::Accepted);
    }));
    // The connectivity manager should NOT be informed of the changed certificate (because of security profile < 3)
    EXPECT_CALL(connectivity_manager, on_charging_station_certificate_changed()).Times(0);
    // A security event notification should NOT be sent (because of security profile < 3)
    EXPECT_CALL(security_event_callback_mock, Call(_, _)).Times(0);

    // When no certificate type is given, charging station certificate type is used.
    security.handle_message(create_example_certificate_signed_request("", std::nullopt));
}

TEST_F(SecurityTest, handle_message_install_certificate_csms_root_not_allowed_on_unsecure_connection) {
    set_security_profile(this->device_model, 1);
    set_bool_variable(this->device_model, ControllerComponentVariables::AllowCSMSRootCertInstallWithUnsecureConnection,
                      false);

    // The certificate is never looked at.
    EXPECT_CALL(evse_security, install_ca_certificate(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<InstallCertificateResponse>();
        EXPECT_EQ(response.status, InstallCertificateStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo->reasonCode.get(), "Unspecified");
        ASSERT_TRUE(response.statusInfo->additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo->additionalInfo->get(),
                  "CSMSRootCertificateInstallationNotAllowedWithUnsecureConnection");
    }));

    security.handle_message(create_example_install_certificate_request(InstallCertificateUseEnum::CSMSRootCertificate));
}

TEST_F(SecurityTest, handle_message_install_certificate_manufacturer_root_not_allowed_on_unsecure_connection) {
    set_security_profile(this->device_model, 1);
    set_bool_variable(this->device_model, ControllerComponentVariables::AllowMFRootCertInstallWithUnsecureConnection,
                      false);

    EXPECT_CALL(evse_security, install_ca_certificate(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<InstallCertificateResponse>();
        EXPECT_EQ(response.status, InstallCertificateStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo->reasonCode.get(), "Unspecified");
        ASSERT_TRUE(response.statusInfo->additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo->additionalInfo->get(),
                  "ManufacturerRootCertificateInstallationNotAllowedWithUnsecureConnection");
    }));

    security.handle_message(
        create_example_install_certificate_request(InstallCertificateUseEnum::ManufacturerRootCertificate));
}

TEST_F(SecurityTest, handle_message_install_certificate_csms_root_allowed_on_unsecure_connection) {
    set_security_profile(this->device_model, 1);
    set_bool_variable(this->device_model, ControllerComponentVariables::AllowCSMSRootCertInstallWithUnsecureConnection,
                      true);

    EXPECT_CALL(evse_security, install_ca_certificate("", ocpp::CaCertificateType::CSMS))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<InstallCertificateResponse>();
        EXPECT_EQ(response.status, InstallCertificateStatusEnum::Accepted);
        EXPECT_FALSE(response.statusInfo.has_value());
    }));
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>("ReconfigurationOfSecurityParameters"), _));

    security.handle_message(create_example_install_certificate_request(InstallCertificateUseEnum::CSMSRootCertificate));
}

TEST_F(SecurityTest, handle_message_install_certificate_oem_root_not_supported_unsecure_connection) {
    set_security_profile(this->device_model, 1);

    // Installing an OEM root certificate is not implemented, the refusal has nothing to do with the connection.
    EXPECT_CALL(evse_security, install_ca_certificate(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<InstallCertificateResponse>();
        EXPECT_EQ(response.status, InstallCertificateStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo->reasonCode.get(), "UnsupportedRequest");
        ASSERT_TRUE(response.statusInfo->additionalInfo.has_value());
        EXPECT_EQ(response.statusInfo->additionalInfo->get(), "OEMRootCertificateInstallationNotSupported");
    }));

    security.handle_message(create_example_install_certificate_request(InstallCertificateUseEnum::OEMRootCertificate));
}

TEST_F(SecurityTest, handle_message_install_certificate_oem_root_not_supported_secure_connection) {
    set_security_profile(this->device_model, 3);

    // On a secure connection the request used to reach evse_security, where the conversion of the OEM certificate
    // type throws. It has to be rejected before that.
    EXPECT_CALL(evse_security, install_ca_certificate(_, _)).Times(0);
    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_)).WillOnce(Invoke([](const json& call_result) {
        auto response = call_result[ocpp::CALLRESULT_PAYLOAD].get<InstallCertificateResponse>();
        EXPECT_EQ(response.status, InstallCertificateStatusEnum::Rejected);
        ASSERT_TRUE(response.statusInfo.has_value());
        EXPECT_EQ(response.statusInfo->reasonCode.get(), "UnsupportedRequest");
    }));

    security.handle_message(create_example_install_certificate_request(InstallCertificateUseEnum::OEMRootCertificate));
}

TEST_F(SecurityTest, sign_certificate_request_accepted) {
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // Let the request be accepted, with a CSR  in the result.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    // This will send a 'sign certificate request' to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_FALSE(triggered);
    }));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_twice) {
    // When a sign certificate request is done twice and the first does not have a response yet, the second request is
    // not handled.
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_FALSE(triggered);
    }));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);

    // Now try a second time, which should fail because there is no answer yet.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_triggered_dropped_while_awaiting) {
    // A TriggerMessage must not supersede an in-flight request. handle_trigger_message rejects the trigger while a
    // CertificateSigned.req is awaited, and the send path has to agree with that answer: no new request, and the
    // attempt chain of the in-flight request left alone. Triggered mirror of sign_certificate_request_twice.
    set_charging_station_csr_inputs();

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .Times(1)
        .WillOnce(Return(sign_request_result));

    // Only the initial, non-triggered request reaches the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    ASSERT_TRUE(security.awaited_certificate_signing_use_enum.has_value());

    // A retry chain is already in progress; the dropped trigger must not restart it.
    security.csr_attempt = 4;

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, true);

    EXPECT_TRUE(security.awaited_certificate_signing_use_enum.has_value());
    EXPECT_EQ(security.csr_attempt, 4);
}

TEST_F(SecurityTest, is_sign_certificate_possible_absent_with_complete_configuration) {
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);

    EXPECT_FALSE(
        security.is_sign_certificate_possible(ocpp::CertificateSigningUseEnum::ChargingStationCertificate).has_value());
}

TEST_F(SecurityTest, is_sign_certificate_possible_without_country) {
    // Mirrors sign_certificate_request_no_country, but asserts the gate the TriggerMessage handler consults rather
    // than the silent return.
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);

    const auto rejection =
        security.is_sign_certificate_possible(ocpp::CertificateSigningUseEnum::ChargingStationCertificate);
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->reasonCode.get(), "MissingDevModelInfo");
    // The CSMS is told which input is missing, not just that something is.
    ASSERT_TRUE(rejection->additionalInfo.has_value());
    EXPECT_EQ(rejection->additionalInfo->get(), "Missing country");
}

TEST_F(SecurityTest, is_sign_certificate_possible_lists_every_missing_input) {
    // None of the ISO15118Ctrlr inputs a V2G CSR needs are configured. Reporting only the first missing one would send
    // the operator round the loop once per input, so all of them are listed at once.
    const auto rejection = security.is_sign_certificate_possible(ocpp::CertificateSigningUseEnum::V2GCertificate);
    ASSERT_TRUE(rejection.has_value());
    ASSERT_TRUE(rejection->additionalInfo.has_value());
    EXPECT_EQ(rejection->additionalInfo->get(), "Missing commonName, country, organizationName");
}

TEST_F(SecurityTest, is_sign_certificate_possible_while_awaiting) {
    // The configuration is complete, so the only thing standing in the way is the awaited CertificateSigned.req. The
    // send path would drop a request in that state, so the gate the TriggerMessage handler consults has to report it
    // and let the handler reject instead of accepting and then staying silent.
    set_charging_station_csr_inputs();

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";
    EXPECT_CALL(this->evse_security, generate_certificate_signing_request(_, _, _, _, _))
        .WillOnce(Return(sign_request_result));
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(1);

    ASSERT_FALSE(
        security.is_sign_certificate_possible(ocpp::CertificateSigningUseEnum::ChargingStationCertificate).has_value());

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    ASSERT_TRUE(security.awaited_certificate_signing_use_enum.has_value());

    const auto rejection =
        security.is_sign_certificate_possible(ocpp::CertificateSigningUseEnum::ChargingStationCertificate);
    ASSERT_TRUE(rejection.has_value());
    EXPECT_EQ(rejection->reasonCode.get(), "Unspecified");
}

TEST_F(SecurityTest, sign_certificate_request_accepted_no_csr) {
    // Try to sign a certificate request, but the 'evse security' does not return a CSR.
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // An empty CSR is returned, although the status is 'Accepted'.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = std::nullopt;

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    // A security event notification will be sent to inform the CSMS that the generation of the CSR has failed.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
        EXPECT_EQ(request.get_type(), "SecurityEventNotification");
        EXPECT_EQ(request.type.get(), ocpp::security_events::CSRGENERATIONFAILED);
        EXPECT_FALSE(triggered);
    }));

    // And a security event is sent as well.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>(ocpp::security_events::CSRGENERATIONFAILED), _));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_not_accepted) {
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // Try to send a certificate request, but the generation of the CSR fails.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::GenerationError;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    // A security event notification is sent to the CSMS to let it know that the CSR generation has failed.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
        EXPECT_EQ(request.get_type(), "SecurityEventNotification");
        EXPECT_EQ(request.type.get(), ocpp::security_events::CSRGENERATIONFAILED);
        EXPECT_FALSE(triggered);
    }));

    // And a security event is sent.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>(ocpp::security_events::CSRGENERATIONFAILED), _));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_no_organization_name) {
    // Try to sign certificate request, but the organization name is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::OrganizationName.component.name.get(), std::nullopt, std::nullopt, std::nullopt,
        ControllerComponentVariables::OrganizationName.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_no_serial_number) {
    // Try to sign certificate request, but the serial number is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::OrganizationName.component.name.get(), std::nullopt, std::nullopt, std::nullopt,
        ControllerComponentVariables::OrganizationName.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();

    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_no_country) {
    // Try to sign certificate request, but the country is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::OrganizationName.component.name.get(), std::nullopt, std::nullopt, std::nullopt,
        ControllerComponentVariables::OrganizationName.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_v2g_accepted) {
    // Try to sign v2g certificate request.
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrSeccId.component,
                                  ControllerComponentVariables::ISO15118CtrlrSeccId.variable.value(),
                                  AttributeEnum::Actual, "iso_testcommonname", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrOrganizationName.component,
                                  ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable.value(),
                                  AttributeEnum::Actual, "iso_testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "iso_testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPMSeccLeafCertificate.component,
                                  ControllerComponentVariables::UseTPMSeccLeafCertificate.variable.value(),
                                  AttributeEnum::Actual, "true", "test", true);

    // Which is accepted by 'evse security'.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::V2GCertificate, "iso_testCountry",
                                                     "iso_testOrganization", "iso_testcommonname", true))
        .WillOnce(Return(sign_request_result));

    // This will send a 'SignCertificateRequest' to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::V2GCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_TRUE(triggered);
    }));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::V2GCertificate, true);
}

TEST_F(SecurityTest, sign_certificate_request_v2g20_accepted) {
    // The ISO 15118-20 SECC leaf uses the same ISO15118Ctrlr CSR inputs and TPM setting as the -2 leaf, but is
    // requested with the OCPP 2.1 certificateType V2G20Certificate.
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrSeccId.component,
                                  ControllerComponentVariables::ISO15118CtrlrSeccId.variable.value(),
                                  AttributeEnum::Actual, "iso_testcommonname", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrOrganizationName.component,
                                  ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable.value(),
                                  AttributeEnum::Actual, "iso_testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "iso_testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPMSeccLeafCertificate.component,
                                  ControllerComponentVariables::UseTPMSeccLeafCertificate.variable.value(),
                                  AttributeEnum::Actual, "true", "test", true);

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr20";

    EXPECT_CALL(this->evse_security, generate_certificate_signing_request(
                                         ocpp::CertificateSigningUseEnum::V2G20Certificate, "iso_testCountry",
                                         "iso_testOrganization", "iso_testcommonname", true))
        .WillOnce(Return(sign_request_result));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::V2G20Certificate);
        EXPECT_EQ(request.csr, "csr20");
        EXPECT_FALSE(triggered);
    }));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::V2G20Certificate);
}

TEST_F(SecurityTest, v2g20_certificate_installation_enabled_requires_v21_and_both_flags) {
    const auto set_bool = [this](const ComponentVariable& cv, const bool value) {
        this->device_model->set_value(cv.component, cv.variable.value(), AttributeEnum::Actual,
                                      value ? "true" : "false", "test", true);
    };
    set_bool(ControllerComponentVariables::V2GCertificateInstallationEnabled, true);
    set_bool(ControllerComponentVariables::V2G20CertificateInstallationEnabled, true);

    // A 2.0.1 CSMS does not know V2G20Certificate
    this->ocpp_version = ocpp::OcppProtocolVersion::v201;
    EXPECT_FALSE(security.v2g20_certificate_installation_enabled());

    this->ocpp_version = ocpp::OcppProtocolVersion::v21;
    EXPECT_TRUE(security.v2g20_certificate_installation_enabled());

    set_bool(ControllerComponentVariables::V2G20CertificateInstallationEnabled, false);
    EXPECT_FALSE(security.v2g20_certificate_installation_enabled());

    set_bool(ControllerComponentVariables::V2G20CertificateInstallationEnabled, true);
    set_bool(ControllerComponentVariables::V2GCertificateInstallationEnabled, false);
    EXPECT_FALSE(security.v2g20_certificate_installation_enabled());
}

TEST_F(SecurityTest, sign_certificate_request_manufacturer_cert_accepted) {
    // Try to sign manufacturer certificate request.
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrSeccId.component,
                                  ControllerComponentVariables::ISO15118CtrlrSeccId.variable.value(),
                                  AttributeEnum::Actual, "iso_testcommonname", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrOrganizationName.component,
                                  ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable.value(),
                                  AttributeEnum::Actual, "iso_testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "iso_testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPMSeccLeafCertificate.component,
                                  ControllerComponentVariables::UseTPMSeccLeafCertificate.variable.value(),
                                  AttributeEnum::Actual, "true", "test", true);

    // Which is accepted by 'evse security'.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security, generate_certificate_signing_request(
                                         ocpp::CertificateSigningUseEnum::ManufacturerCertificate, "iso_testCountry",
                                         "iso_testOrganization", "iso_testcommonname", true))
        .WillOnce(Return(sign_request_result));

    // This will send a 'SignCertificateRequest' to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::V2GCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_TRUE(triggered);
    }));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ManufacturerCertificate, true);
}

TEST_F(SecurityTest, sign_certificate_request_v2g_no_common_name) {
    // Try to sign v2g certificate request, but the common name is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::ISO15118CtrlrSeccId.component.name.get(), std::nullopt, std::nullopt,
        std::nullopt, ControllerComponentVariables::ISO15118CtrlrSeccId.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrOrganizationName.component,
                                  ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable.value(),
                                  AttributeEnum::Actual, "iso_testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "iso_testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::V2GCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_v2g_no_organization) {
    // Try to sign v2g certificate request, but the organization is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::ISO15118CtrlrOrganizationName.component.name.get(), std::nullopt, std::nullopt,
        std::nullopt, ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrSeccId.component,
                                  ControllerComponentVariables::ISO15118CtrlrSeccId.variable.value(),
                                  AttributeEnum::Actual, "iso_testcommonname", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "iso_testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::ManufacturerCertificate, false);
}

TEST_F(SecurityTest, sign_certificate_request_v2g_no_country) {
    // Try to sign v2g certificate request, but the country is not given.
    device_model_test_helper.remove_variable_from_db(
        ControllerComponentVariables::ISO15118CtrlrCountryName.component.name.get(), std::nullopt, std::nullopt,
        std::nullopt, ControllerComponentVariables::ISO15118CtrlrCountryName.variable->name.get(), std::nullopt);

    device_model = device_model_test_helper.get_device_model();
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, security_event_callback_mock.AsStdFunction());

    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrSeccId.component,
                                  ControllerComponentVariables::ISO15118CtrlrSeccId.variable.value(),
                                  AttributeEnum::Actual, "iso_testcommonname", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrOrganizationName.component,
                                  ControllerComponentVariables::ISO15118CtrlrOrganizationName.variable.value(),
                                  AttributeEnum::Actual, "iso_testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);

    // So the request will not be sent at all.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    s.sign_certificate_req(ocpp::CertificateSigningUseEnum::V2GCertificate, false);
}

TEST_F(SecurityTest, security_event_notification_no_timestamp) {
    // Send security event notification, without giving a timestamp.
    // For testing purposes, store the current time.
    const DateTime timestamp_test_start = DateTime();

    // The security event notification request will be sent to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _))
        .WillOnce(Invoke([&timestamp_test_start](const json& call, bool triggered) {
            auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
            EXPECT_EQ(request.get_type(), "SecurityEventNotification");
            EXPECT_EQ(request.type.get(), "test");
            EXPECT_EQ(request.techInfo->get(), "tech info!!");
            // Datetime must be somewhere between the start of the test and 'now'.
            DateTime now;
            // With the conversion from and to rfc3339 (for conversion to / from json), precision is lost. So we do the
            // same here, otherwise the test might fail.
            now.from_rfc3339(DateTime().to_rfc3339());
            EXPECT_LE(request.timestamp.to_time_point(), now.to_time_point());
            // With the conversion from and to rfc3339 (for conversion to / from json), precision is lost. So we do the
            // same here, otherwise the test might fail.
            DateTime start_time;
            start_time.from_rfc3339(timestamp_test_start.to_rfc3339());
            EXPECT_GE(request.timestamp.to_time_point(), start_time.to_time_point());
            EXPECT_FALSE(triggered);
        }));

    // And the security event callback is called.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>("test"), _));

    security.security_event_notification_req("test", "tech info!!", true, true, std::nullopt);
}

TEST_F(SecurityTest, security_event_notification_with_timestamp) {
    // Send security event notification, with a given timestamp.
    const auto timestamp = DateTime(date::utc_clock::now() - std::chrono::minutes(15));

    // The security event notification request will be sent to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([&timestamp](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
        EXPECT_EQ(request.get_type(), "SecurityEventNotification");
        EXPECT_EQ(request.type.get(), "test_with_given_timestamp");
        // Timestamp must be the exact timestamp we have given.
        EXPECT_EQ(request.timestamp.to_rfc3339(), timestamp.to_rfc3339());
        EXPECT_FALSE(triggered);
    }));

    // And the security event callback is called.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>("test_with_given_timestamp"), _));

    security.security_event_notification_req("test_with_given_timestamp", std::nullopt, true, true, timestamp);
}

TEST_F(SecurityTest, security_event_notification_not_critical) {
    // Trigger a not critical security event notification
    // Which will not send the security event to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    // But it will call the security event callback.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>("test"), _));

    security.security_event_notification_req("test", std::nullopt, true, false, std::nullopt);
}

TEST_F(SecurityTest, security_event_notification_not_critital_not_triggered_internally) {
    // Trigger a not critical security event that is not triggered internally.
    // Which will not be sent to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(0);

    // And the callback is also not called.
    EXPECT_CALL(security_event_callback_mock, Call(CiString<50>("test"), _)).Times(0);

    security.security_event_notification_req("test", std::nullopt, false, false, std::nullopt);
}

TEST_F(SecurityTest, security_event_notification_not_triggered_internally) {
    // Trigger a critical security event.
    // Which will send a security event notification to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([&](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
        EXPECT_EQ(request.get_type(), "SecurityEventNotification");
        EXPECT_EQ(request.type.get(), "not_triggered_internally");
        EXPECT_FALSE(triggered);
    }));

    // But when not triggered internally, the callback is not called.
    EXPECT_CALL(security_event_callback_mock, Call(_, _)).Times(0);

    security.security_event_notification_req("not_triggered_internally", std::nullopt, false, true, std::nullopt);
}

TEST_F(SecurityTest, security_event_notification_no_callback) {
    // Trigger a critical security event, but there is no callback to call.
    const FunctionalBlockContext b{
        this->mock_dispatcher,       *this->device_model, this->connectivity_manager,    this->evse_manager,
        this->database_handler_mock, this->evse_security, this->component_state_manager, this->ocpp_version};
    Security s(b, logging, ocsp_updater, nullptr);

    // This will send a security event notification to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([&](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SecurityEventNotificationRequest>();
        EXPECT_EQ(request.get_type(), "SecurityEventNotification");
        EXPECT_EQ(request.type.get(), "no_callback");
        EXPECT_FALSE(triggered);
    }));

    // But the mock is not called, since it is not given in the constructor.
    EXPECT_CALL(security_event_callback_mock, Call(_, _)).Times(0);

    s.security_event_notification_req("no_callback", std::nullopt, true, true, std::nullopt);
}

TEST_F(SecurityTest, handle_sign_certificate_response_successful) {
    // Sign certificate and wait for certificate signed.
    timer_stub_reset_timeout_called_count();
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 1);
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningWaitMinimum.component,
                                  ControllerComponentVariables::CertSigningWaitMinimum.variable.value(),
                                  AttributeEnum::Actual, "1", "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningRepeatTimes.component,
                                  ControllerComponentVariables::CertSigningRepeatTimes.variable.value(),
                                  AttributeEnum::Actual, "2", "test", true);

    // The 'sign_certificate_req' will send a 'sign certificate request' to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillOnce(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_FALSE(triggered);
    }));

    // First do a request.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);

    // Then the response is processed.
    const ocpp::EnhancedMessage<MessageType> response =
        create_example_sign_certificate_response(GenericStatusEnum::Accepted);

    security.handle_message(response);
    EXPECT_GE(timer_stub_get_timeout_called_count(), 1);

    // Leaf certificate should be updated.
    EXPECT_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::ChargingStationCertificate))
        .WillOnce(Return(ocpp::InstallCertificateResult::Accepted));

    EXPECT_CALL(mock_dispatcher, dispatch_call_result(_));

    security.handle_message(create_example_certificate_signed_request("", std::nullopt));
}

TEST_F(SecurityTest, handle_sign_certificate_response_no_response) {
    // Sign certificate and wait for certificate signed, but call callback of timer instead (simulating that
    // certificate signed request is never called).
    timer_stub_reset_timeout_called_count();
    timer_stub_reset_callback();
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 1);
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningWaitMinimum.component,
                                  ControllerComponentVariables::CertSigningWaitMinimum.variable.value(),
                                  AttributeEnum::Actual, "1", "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningRepeatTimes.component,
                                  ControllerComponentVariables::CertSigningRepeatTimes.variable.value(),
                                  AttributeEnum::Actual, "2", "test", true);

    // The 'sign_certificate_req' will send a 'sign certificate request' to the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillRepeatedly(Invoke([](const json& call, bool triggered) {
        auto request = call[ocpp::CALL_PAYLOAD].get<SignCertificateRequest>();
        ASSERT_TRUE(request.certificateType.has_value());
        EXPECT_EQ(request.certificateType.value(), ocpp::v2::CertificateSigningUseEnum::ChargingStationCertificate);
        EXPECT_EQ(request.csr, "csr");
        EXPECT_FALSE(triggered);
    }));

    // First do a request.
    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);

    // Then the response is processed.
    const ocpp::EnhancedMessage<MessageType> response =
        create_example_sign_certificate_response(GenericStatusEnum::Accepted);

    security.handle_message(response);
    EXPECT_GE(timer_stub_get_timeout_called_count(), 1);

    // Leaf certificate should be updated.
    ON_CALL(evse_security, update_leaf_certificate("", ocpp::CertificateSigningUseEnum::ChargingStationCertificate))
        .WillByDefault(Return(ocpp::InstallCertificateResult::Accepted));

    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillOnce(Return(sign_request_result));

    // Timeout is over, callback is called.
    timer_stub_get_callback()();
}

TEST_F(SecurityTest, handle_sign_certificate_response_backoff_values) {
    // Verify the exponential backoff sequence: with CertSigningWaitMinimum=30 and CertSigningRepeatTimes=2,
    // first wait should be 30s, second wait 60s, then stop.
    timer_stub_reset_timeout_called_count();
    timer_stub_reset_callback();
    timer_stub_reset_timeout_interval();
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 1);
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningWaitMinimum.component,
                                  ControllerComponentVariables::CertSigningWaitMinimum.variable.value(),
                                  AttributeEnum::Actual, "30", "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningRepeatTimes.component,
                                  ControllerComponentVariables::CertSigningRepeatTimes.variable.value(),
                                  AttributeEnum::Actual, "2", "test", true);

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillRepeatedly(Invoke([](const json& call, bool triggered) {
        // Accept all SignCertificate.req dispatches
    }));
    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillRepeatedly(Return(sign_request_result));

    // Initial sign_certificate_req + Accepted response
    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    // First timeout should be 30s (CertSigningWaitMinimum * 2^0)
    EXPECT_EQ(timer_stub_get_timeout_interval_ms(), 30000);

    // Fire timeout callback → triggers retry → feed new Accepted response → new timer
    timer_stub_reset_timeout_interval();
    timer_stub_get_callback()(); // increments csr_attempt to 2
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    // Second timeout should be 60s (CertSigningWaitMinimum * 2^1)
    EXPECT_EQ(timer_stub_get_timeout_interval_ms(), 60000);

    // Fire timeout callback again → csr_attempt becomes 3 > RepeatTimes(2) → should stop
    timer_stub_reset_timeout_called_count();
    timer_stub_reset_timeout_interval();
    timer_stub_get_callback()(); // increments csr_attempt to 3
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    // Timer should not have been restarted (csr_attempt > CertSigningRepeatTimes)
    EXPECT_EQ(timer_stub_get_timeout_called_count(), 0);
    EXPECT_EQ(timer_stub_get_timeout_interval_ms(), 0);
}

TEST_F(SecurityTest, handle_sign_certificate_response_backoff_floor) {
    // Verify the 10s safety floor: with CertSigningWaitMinimum=0, the floor of 10s should apply.
    timer_stub_reset_timeout_called_count();
    timer_stub_reset_callback();
    timer_stub_reset_timeout_interval();
    set_update_certificate_symlinks_enabled(this->device_model, true);
    set_security_profile(this->device_model, 1);
    this->device_model->set_value(ControllerComponentVariables::ChargeBoxSerialNumber.component,
                                  ControllerComponentVariables::ChargeBoxSerialNumber.variable.value(),
                                  AttributeEnum::Actual, "testserialnumber", "test", true);
    this->device_model->set_value(ControllerComponentVariables::OrganizationName.component,
                                  ControllerComponentVariables::OrganizationName.variable.value(),
                                  AttributeEnum::Actual, "testOrganization", "test", true);
    this->device_model->set_value(ControllerComponentVariables::ISO15118CtrlrCountryName.component,
                                  ControllerComponentVariables::ISO15118CtrlrCountryName.variable.value(),
                                  AttributeEnum::Actual, "testCountry", "test", true);
    this->device_model->set_value(ControllerComponentVariables::UseTPM.component,
                                  ControllerComponentVariables::UseTPM.variable.value(), AttributeEnum::Actual, "false",
                                  "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningWaitMinimum.component,
                                  ControllerComponentVariables::CertSigningWaitMinimum.variable.value(),
                                  AttributeEnum::Actual, "0", "test", true);
    this->device_model->set_value(ControllerComponentVariables::CertSigningRepeatTimes.component,
                                  ControllerComponentVariables::CertSigningRepeatTimes.variable.value(),
                                  AttributeEnum::Actual, "1", "test", true);

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).WillRepeatedly(Invoke([](const json& call, bool triggered) {
        // Accept all SignCertificate.req dispatches
    }));
    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .WillRepeatedly(Return(sign_request_result));

    // Initial sign_certificate_req + Accepted response
    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    // First timeout should be 10s (max(10, 0) * 2^0 = 10s floor)
    EXPECT_EQ(timer_stub_get_timeout_interval_ms(), 10000);
}

/// \brief Device model without CertSigningWaitMinimum, so no retry timer can be armed for an awaited
/// CertificateSigned.req .
class SecurityWithoutCertSigningWaitMinimumTest : public SecurityTest {
protected:
    SecurityWithoutCertSigningWaitMinimumTest() : SecurityTest({"CertSigningWaitMinimum"}) {
    }
};

/// \brief Device model without CertSigningRepeatTimes, so no retry timer can be armed for an awaited
/// CertificateSigned.req .
class SecurityWithoutCertSigningRepeatTimesTest : public SecurityTest {
protected:
    SecurityWithoutCertSigningRepeatTimesTest() : SecurityTest({"CertSigningRepeatTimes"}) {
    }
};

TEST_F(SecurityWithoutCertSigningWaitMinimumTest, sign_certificate_response_does_not_block_later_requests) {
    // Without CertSigningWaitMinimum no retry timer is armed, so nothing else will ever clear the awaited state.
    // Leaving it set silences every later SignCertificate.req for the lifetime of the process.
    timer_stub_reset_timeout_called_count();
    set_charging_station_csr_inputs();

    ASSERT_FALSE(
        this->device_model->get_optional_value<int>(ControllerComponentVariables::CertSigningWaitMinimum).has_value());

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";
    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .Times(2)
        .WillRepeatedly(Return(sign_request_result));

    // Both the initial request and the one issued after the response must reach the CSMS.
    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(2);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    EXPECT_EQ(timer_stub_get_timeout_called_count(), 0);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}

TEST_F(SecurityWithoutCertSigningRepeatTimesTest, sign_certificate_response_does_not_block_later_requests) {
    // Same wedge as the CertSigningWaitMinimum case, reached through the second early return.
    timer_stub_reset_timeout_called_count();
    set_charging_station_csr_inputs();

    ASSERT_FALSE(
        this->device_model->get_optional_value<int>(ControllerComponentVariables::CertSigningRepeatTimes).has_value());

    ocpp::GetCertificateSignRequestResult sign_request_result;
    sign_request_result.status = GetCertificateSignRequestStatus::Accepted;
    sign_request_result.csr = "csr";
    EXPECT_CALL(this->evse_security,
                generate_certificate_signing_request(ocpp::CertificateSigningUseEnum::ChargingStationCertificate,
                                                     "testCountry", "testOrganization", "testserialnumber", false))
        .Times(2)
        .WillRepeatedly(Return(sign_request_result));

    EXPECT_CALL(mock_dispatcher, dispatch_call(_, _)).Times(2);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
    security.handle_message(create_example_sign_certificate_response(GenericStatusEnum::Accepted));

    EXPECT_EQ(timer_stub_get_timeout_called_count(), 0);

    security.sign_certificate_req(ocpp::CertificateSigningUseEnum::ChargingStationCertificate, false);
}
