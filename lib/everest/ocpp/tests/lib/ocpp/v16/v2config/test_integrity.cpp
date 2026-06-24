// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include "configuration_stub.hpp"
#include <ocpp/v16/charge_point_configuration_devicemodel.hpp>

namespace {
using namespace ocpp::v16::stubs;

// Only the device-model backend implements check_integrity; the JSON backend is a no-op.
// All tests operate on a freshly constructed ChargePointConfigurationDeviceModel via the
// ConfigurationBase fixture (which loads config.json and populates MemoryStorage with the
// default set of required variables).

class IntegrityCheck : public ConfigurationBase {
protected:
    ocpp::v16::ChargePointConfigurationDeviceModel* dm() {
        return static_cast<ocpp::v16::ChargePointConfigurationDeviceModel*>(v2_config.get());
    }
};

// The default MemoryStorage has NumberOfConnectors=1 and all required keys populated.
TEST_F(IntegrityCheck, PassesWithFullConfig) {
    ASSERT_NE(dm(), nullptr);
    EXPECT_NO_THROW(dm()->check_integrity(1));
}

// Removing a Core required key must cause check_integrity to throw.
TEST_F(IntegrityCheck, FailsOnMissingCoreKey) {
    device_model->clear("Core", "HeartbeatInterval");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingAuthorizeRemoteTxRequests) {
    device_model->clear("Core", "AuthorizeRemoteTxRequests");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingClockAlignedDataInterval) {
    device_model->clear("Core", "ClockAlignedDataInterval");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingConnectionTimeOut) {
    device_model->clear("Core", "ConnectionTimeOut");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingConnectorPhaseRotation) {
    device_model->clear("Core", "ConnectorPhaseRotation");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingGetConfigurationMaxKeys) {
    device_model->clear("Core", "GetConfigurationMaxKeys");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingLocalAuthorizeOffline) {
    device_model->clear("Core", "LocalAuthorizeOffline");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingLocalPreAuthorize) {
    device_model->clear("Core", "LocalPreAuthorize");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingMeterValuesAlignedData) {
    device_model->clear("Core", "MeterValuesAlignedData");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingMeterValuesSampledData) {
    device_model->clear("Core", "MeterValuesSampledData");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingMeterValueSampleInterval) {
    device_model->clear("Core", "MeterValueSampleInterval");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingResetRetries) {
    device_model->clear("Core", "ResetRetries");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingStopTransactionOnInvalidId) {
    device_model->clear("Core", "StopTransactionOnInvalidId");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingStopTxnAlignedData) {
    device_model->clear("Core", "StopTxnAlignedData");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingStopTxnSampledData) {
    device_model->clear("Core", "StopTxnSampledData");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingSupportedFeatureProfiles) {
    // The constructor validates that Core is in SupportedFeatureProfiles, so we must clear the
    // key after construction (like the PnC tests) to test that check_integrity detects it.
    device_model->clear("Core", "SupportedFeatureProfiles");
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingTransactionMessageAttempts) {
    device_model->clear("Core", "TransactionMessageAttempts");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingTransactionMessageRetryInterval) {
    device_model->clear("Core", "TransactionMessageRetryInterval");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingUnlockConnectorOnEVSideDisconnect) {
    device_model->clear("Core", "UnlockConnectorOnEVSideDisconnect");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

// Internal required keys
TEST_F(IntegrityCheck, FailsOnMissingCentralSystemURI) {
    device_model->clear("Internal", "CentralSystemURI");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingChargePointId) {
    device_model->clear("Internal", "ChargePointId");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingChargeBoxSerialNumber) {
    device_model->clear("Internal", "ChargeBoxSerialNumber");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingChargePointModel) {
    device_model->clear("Internal", "ChargePointModel");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingChargePointVendor) {
    device_model->clear("Internal", "ChargePointVendor");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

TEST_F(IntegrityCheck, FailsOnMissingFirmwareVersion) {
    device_model->clear("Internal", "FirmwareVersion");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

// Security required key
TEST_F(IntegrityCheck, FailsOnMissingSecurityProfile) {
    device_model->clear("Security", "SecurityProfile");
    createV2Config();
    EXPECT_THROW(dm()->check_integrity(1), std::runtime_error);
}

// NumberOfConnectors mismatch
TEST_F(IntegrityCheck, FailsOnNumberOfConnectorsMismatch) {
    // MemoryStorage default has NumberOfConnectors=1, but we pass 2
    EXPECT_THROW(dm()->check_integrity(2), std::runtime_error);
}

TEST_F(IntegrityCheck, PassesWhenNumberOfConnectorsMatches) {
    device_model->set("Core", "NumberOfConnectors", "2");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(2));
}

// LocalAuthListManagement profile conditional keys
// Default config includes LocalAuthListManagement in SupportedFeatureProfiles.
// When a required key is missing the profile is stripped rather than causing a fatal error,
// to preserve backwards compatibility with migrated OCPP 1.6 configs.
TEST_F(IntegrityCheck, DropsLocalAuthListManagementProfileWhenLocalAuthListEnabledMissing) {
    device_model->clear("LocalAuthListManagement", "LocalAuthListEnabled");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(
        dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::LocalAuthListManagement));
}

TEST_F(IntegrityCheck, DropsLocalAuthListManagementProfileWhenLocalAuthListMaxLengthMissing) {
    device_model->clear("LocalAuthListManagement", "LocalAuthListMaxLength");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(
        dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::LocalAuthListManagement));
}

TEST_F(IntegrityCheck, DropsLocalAuthListManagementProfileWhenSendLocalListMaxLengthMissing) {
    device_model->clear("LocalAuthListManagement", "SendLocalListMaxLength");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(
        dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::LocalAuthListManagement));
}

// SmartCharging profile conditional keys
// Default config includes SmartCharging in SupportedFeatureProfiles.
TEST_F(IntegrityCheck, DropsSmartChargingProfileWhenChargeProfileMaxStackLevelMissing) {
    device_model->clear("SmartCharging", "ChargeProfileMaxStackLevel");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::SmartCharging));
}

TEST_F(IntegrityCheck, DropsSmartChargingProfileWhenChargingScheduleAllowedChargingRateUnitMissing) {
    device_model->clear("SmartCharging", "ChargingScheduleAllowedChargingRateUnit");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::SmartCharging));
}

TEST_F(IntegrityCheck, DropsSmartChargingProfileWhenChargingScheduleMaxPeriodsMissing) {
    device_model->clear("SmartCharging", "ChargingScheduleMaxPeriods");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::SmartCharging));
}

TEST_F(IntegrityCheck, DropsSmartChargingProfileWhenMaxChargingProfilesInstalledMissing) {
    device_model->clear("SmartCharging", "MaxChargingProfilesInstalled");
    createV2Config();
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::SmartCharging));
}

// PnC conditional keys.
//
// The PnC profile is inferred by the constructor: it is added to supported_feature_profiles
// only when ISO15118PnCEnabled, ISO15118CertificateManagementEnabled, and
// ContractValidationOffline are ALL present at construction time.
//
// To test that check_integrity strips PnC we must:
//   1. Let SetUp() run with all PnC vars present -> constructor adds PnC to supported profiles.
//   2. Then clear the key from the underlying storage (without reconstructing the config).
//   3. Call check_integrity -> PnC is still in supported_feature_profiles -> profile stripped.

TEST_F(IntegrityCheck, DropsPnCProfileWhenISO15118PnCEnabledMissing) {
    // SetUp left PnC in supported_feature_profiles; now remove one of its required keys.
    device_model->clear("PnC", "ISO15118PnCEnabled");
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::PnC));
}

TEST_F(IntegrityCheck, DropsPnCProfileWhenContractValidationOfflineMissing) {
    device_model->clear("PnC", "ContractValidationOffline");
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::PnC));
}

// CostAndPrice conditional keys.
//
// Same pattern: SetUp initialises with CustomDisplayCostAndPrice present -> constructor adds
// CostAndPrice to supported_feature_profiles. Clear the key, then check_integrity strips the profile.
TEST_F(IntegrityCheck, DropsCostAndPriceProfileWhenCustomDisplayCostAndPriceMissing) {
    device_model->clear("CostAndPrice", "CustomDisplayCostAndPrice");
    EXPECT_NO_THROW(dm()->check_integrity(1));
    EXPECT_FALSE(dm()->getSupportedFeatureProfilesSet().count(ocpp::v16::SupportedFeatureProfiles::CostAndPrice));
}

// Removing PnC config vars from storage BEFORE construction causes the constructor to NOT add
// PnC to supported_feature_profiles, so the PnC required keys are not checked.
TEST_F(IntegrityCheck, PnCKeysNotCheckedWhenPnCProfileAbsent) {
    device_model->clear("PnC", "ISO15118PnCEnabled");
    device_model->clear("PnC", "ISO15118CertificateManagementEnabled");
    device_model->clear("PnC", "ContractValidationOffline");
    createV2Config(); // reconstruct: PnC not added to supported profiles
    EXPECT_NO_THROW(dm()->check_integrity(1));
}

TEST_F(IntegrityCheck, CostAndPriceKeysNotCheckedWhenProfileAbsent) {
    device_model->clear("CostAndPrice", "CustomDisplayCostAndPrice");
    createV2Config(); // reconstruct: CostAndPrice not added to supported profiles
    EXPECT_NO_THROW(dm()->check_integrity(1));
}

} // namespace
