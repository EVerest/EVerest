
# OCPP 1.6 to DeviceModel Mapping Reference

This documents how OCPP 1.6 configuration keys map to the Device Model representation. OCPP introduced the device model with OCPP2.0.1. Initially the configuration parameters and storage were strictly separated in this 
library. In order to support a common configuration backend for all protocol versions, the OCPP 1.6 configuration keys are now mapped to the Device Model as well. This allows for a unified configuration management and storage approach across protocol versions.

This document is intended as a **human-readable reference** for integrators and maintainers.
The implementation in code (for example `known_keys` conversion logic and patching behaviour) remains authoritative.

## Core Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 1 | `AllowOfflineTxForUnknownId` | VariableAttribute | `AuthCtrlr` | `OfflineTxForUnknownIdEnabled` | `Actual` |
| 2 | `AuthorizationCacheEnabled` | VariableAttribute | `AuthCacheCtrlr` | `Enabled` | `Actual` |
| 3 | `AuthorizeRemoteTxRequests` | VariableAttribute | `AuthCtrlr` | `AuthorizeRemoteStart` | `Actual` |
| 4 | `ClockAlignedDataInterval` | VariableAttribute | `AlignedDataCtrlr` | `Interval` | `Actual` |
| 5 | `ConnectionTimeOut` | VariableAttribute | `TxCtrlr` | `EVConnectionTimeOut` | `Actual` |
| 6 | `HeartbeatInterval` | VariableAttribute | `OCPPCommCtrlr` | `HeartbeatInterval` | `Actual` |
| 7 | `LocalAuthorizeOffline` | VariableAttribute | `AuthCtrlr` | `LocalAuthorizeOffline` | `Actual` |
| 8 | `LocalPreAuthorize` | VariableAttribute | `AuthCtrlr` | `LocalPreAuthorize` | `Actual` |
| 9 | `MaxEnergyOnInvalidId` | VariableAttribute | `TxCtrlr` | `MaxEnergyOnInvalidId` | `Actual` |
| 10 | `MeterValuesAlignedData` | VariableAttribute | `AlignedDataCtrlr` | `Measurands` | `Actual` |
| 11 | `MeterValuesAlignedDataMaxLength` | VariableCharacteristics | `AlignedDataCtrlr` | `Measurands` | `maxLimit` |
| 12 | `MeterValuesSampledData` | VariableAttribute | `SampledDataCtrlr` | `TxUpdatedMeasurands` | `Actual` |
| 13 | `MeterValuesSampledDataMaxLength` | VariableCharacteristics | `SampledDataCtrlr` | `TxUpdatedMeasurands` | `maxLimit` |
| 14 | `MeterValueSampleInterval` | VariableAttribute | `SampledDataCtrlr` | `TxUpdatedInterval` | `Actual` |
| 15 | `ResetRetries` | VariableAttribute | `OCPPCommCtrlr` | `ResetRetries` | `Actual` |
| 16 | `StopTransactionOnInvalidId` | VariableAttribute | `TxCtrlr` | `StopTxOnInvalidId` | `Actual` |
| 17 | `StopTxnAlignedData` | VariableAttribute | `AlignedDataCtrlr` | `TxEndedMeasurands` | `Actual` |
| 18 | `StopTxnAlignedDataMaxLength` | VariableCharacteristics | `AlignedDataCtrlr` | `TxEndedMeasurands` | `maxLimit` |
| 19 | `StopTxnSampledData` | VariableAttribute | `SampledDataCtrlr` | `TxEndedMeasurands` | `Actual` |
| 20 | `StopTxnSampledDataMaxLength` | VariableCharacteristics | `SampledDataCtrlr` | `TxEndedMeasurands` | `maxLimit` |
| 21 | `TransactionMessageAttempts` | VariableAttribute | `OCPPCommCtrlr` | `MessageAttempts` | `Actual` |
| 22 | `TransactionMessageRetryInterval` | VariableAttribute | `OCPPCommCtrlr` | `MessageAttemptInterval` | `Actual` |
| 23 | `WebSocketPingInterval` | VariableAttribute | `OCPPCommCtrlr` | `WebSocketPingInterval` | `Actual` |
| 24 | `LocalAuthListEnabled` | VariableAttribute | `LocalAuthListCtrlr` | `Enabled` | `Actual` |
| 25 | `ChargeProfileMaxStackLevel` | VariableAttribute | `SmartChargingCtrlr` | `ProfileStackLevel` | `Actual` |
| 26 | `ChargingScheduleAllowedChargingRateUnit` | VariableAttribute | `SmartChargingCtrlr` | `RateUnit` | `Actual` |
| 27 | `ChargingScheduleMaxPeriods` | VariableAttribute | `SmartChargingCtrlr` | `PeriodsPerSchedule` | `Actual` |
| 28 | `ConnectorSwitch3to1PhaseSupported` | VariableAttribute | `SmartChargingCtrlr` | `Phases3to1` | `Actual` |
| 29 | `SupportedFileTransferProtocols` | VariableAttribute | `OCPPCommCtrlr` | `FileTransferProtocols` | `Actual` |

## Internal Keys

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 30 | `ChargeBoxSerialNumber` | VariableAttribute | `InternalCtrlr` | `ChargeBoxSerialNumber` | `Actual` |
| 32 | `ChargePointModel` | VariableAttribute | `InternalCtrlr` | `ChargePointModel` | `Actual` |
| 33 | `ChargePointSerialNumber` | VariableAttribute | `InternalCtrlr` | `ChargePointSerialNumber` | `Actual` |
| 34 | `ChargePointVendor` | VariableAttribute | `InternalCtrlr` | `ChargePointVendor` | `Actual` |
| 35 | `FirmwareVersion` | VariableAttribute | `InternalCtrlr` | `FirmwareVersion` | `Actual` |
| 36 | `ICCID` | VariableAttribute | `InternalCtrlr` | `ICCID` | `Actual` |
| 37 | `IFace` | VariableAttribute | `InternalCtrlr` | `IFace` | `Actual` |
| 38 | `IMSI` | VariableAttribute | `InternalCtrlr` | `IMSI` | `Actual` |
| 39 | `MeterSerialNumber` | VariableAttribute | `InternalCtrlr` | `MeterSerialNumber` | `Actual` |
| 40 | `MeterType` | VariableAttribute | `InternalCtrlr` | `MeterType` | `Actual` |
| 41 | `SupportedCiphers12` | VariableAttribute | `InternalCtrlr` | `SupportedCiphers12` | `Actual` |
| 42 | `SupportedCiphers13` | VariableAttribute | `InternalCtrlr` | `SupportedCiphers13` | `Actual` |
| 43 | `UseTPM` | VariableAttribute | `InternalCtrlr` | `UseTPM` | `Actual` |
| 44 | `UseTPMSeccLeafCertificate` | VariableAttribute | `InternalCtrlr` | `UseTPMSeccLeafCertificate` | `Actual` |
| 45 | `RetryBackoffRandomRange` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffRandomRange` | `Actual` |
| 46 | `RetryBackoffRepeatTimes` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffRepeatTimes` | `Actual` |
| 47 | `AuthorizeConnectorZeroOnConnectorOne` | VariableAttribute | `InternalCtrlr` | `AuthorizeConnectorZeroOnConnectorOne` | `Actual` |
| 48 | `LogMessages` | VariableAttribute | `InternalCtrlr` | `LogMessages` | `Actual` |
| 49 | `LogMessagesRaw` | VariableAttribute | `InternalCtrlr` | `LogMessagesRaw` | `Actual` |
| 50 | `LogMessagesFormat` | VariableAttribute | `InternalCtrlr` | `LogMessagesFormat` | `Actual` |
| 51 | `LogRotation` | VariableAttribute | `InternalCtrlr` | `LogRotation` | `Actual` |
| 52 | `LogRotationDateSuffix` | VariableAttribute | `InternalCtrlr` | `LogRotationDateSuffix` | `Actual` |
| 53 | `LogRotationMaximumFileSize` | VariableAttribute | `InternalCtrlr` | `LogRotationMaximumFileSize` | `Actual` |
| 54 | `LogRotationMaximumFileCount` | VariableAttribute | `InternalCtrlr` | `LogRotationMaximumFileCount` | `Actual` |
| 55 | `SupportedChargingProfilePurposeTypes` | VariableAttribute | `InternalCtrlr` | `SupportedChargingProfilePurposeTypes` | `Actual` |
| 56 | `IgnoredProfilePurposesOffline` | VariableAttribute | `SmartChargingCtrlr` | `IgnoredProfilePurposesOffline` | `Actual` |
| 57 | `MaxCompositeScheduleDuration` | VariableAttribute | `InternalCtrlr` | `MaxCompositeScheduleDuration` | `Actual` |
| 58 | `CompositeScheduleDefaultLimitAmps` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultLimitAmps` | `Actual` |
| 59 | `CompositeScheduleDefaultLimitWatts` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultLimitWatts` | `Actual` |
| 60 | `CompositeScheduleDefaultNumberPhases` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultNumberPhases` | `Actual` |
| 61 | `SupplyVoltage` | VariableAttribute | `SmartChargingCtrlr` | `SupplyVoltage` | `Actual` |
| 62 | `WebsocketPingPayload` | VariableAttribute | `InternalCtrlr` | `WebsocketPingPayload` | `Actual` |
| 63 | `WebsocketPongTimeout` | VariableAttribute | `InternalCtrlr` | `WebsocketPongTimeout` | `Actual` |
| 64 | `UseSslDefaultVerifyPaths` | VariableAttribute | `InternalCtrlr` | `UseSslDefaultVerifyPaths` | `Actual` |
| 65 | `VerifyCsmsCommonName` | VariableAttribute | `InternalCtrlr` | `VerifyCsmsCommonName` | `Actual` |
| 66 | `VerifyCsmsAllowWildcards` | VariableAttribute | `InternalCtrlr` | `VerifyCsmsAllowWildcards` | `Actual` |
| 67 | `OcspRequestInterval` | VariableAttribute | `InternalCtrlr` | `OcspRequestInterval` | `Actual` |
| 68 | `SeccLeafSubjectCommonName` | VariableAttribute | `ISO15118Ctrlr` | `SeccId` | `Actual` |
| 69 | `SeccLeafSubjectCountry` | VariableAttribute | `ISO15118Ctrlr` | `CountryName` | `Actual` |
| 70 | `SeccLeafSubjectOrganization` | VariableAttribute | `ISO15118Ctrlr` | `OrganizationName` | `Actual` |
| 71 | `QueueAllMessages` | VariableAttribute | `OCPPCommCtrlr` | `QueueAllMessages` | `Actual` |
| 72 | `MessageTypesDiscardForQueueing` | VariableAttribute | `OCPPCommCtrlr` | `MessageTypesDiscardForQueueing` | `Actual` |
| 73 | `MessageQueueSizeThreshold` | VariableAttribute | `InternalCtrlr` | `MessageQueueSizeThreshold` | `Actual` |
| 74 | `MaxMessageSize` | VariableAttribute | `InternalCtrlr` | `MaxMessageSize` | `Actual` |
| 75 | `TLSKeylogFile` | VariableAttribute | `InternalCtrlr` | `TLSKeylogFile` | `Actual` |
| 76 | `EnableTLSKeylog` | VariableAttribute | `InternalCtrlr` | `EnableTLSKeylog` | `Actual` |
| 77 | `RetryBackoffWaitMinimum` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffWaitMinimum` | `Actual` |

## Local Auth List Management Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 78 | `LocalAuthListMaxLength` | VariableCharacteristics | `LocalAuthListCtrlr` | `Entries` | `maxLimit` |
| 79 | `SendLocalListMaxLength` | VariableAttribute | `LocalAuthListCtrlr` | `ItemsPerMessage` | `Actual` |

## Smart Charging Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 80 | `MaxChargingProfilesInstalled` | VariableCharacteristics | `SmartChargingCtrlr` | `Entries` | `maxLimit` |

## Security Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 81 | `AdditionalRootCertificateCheck` | VariableAttribute | `SecurityCtrlr` | `AdditionalRootCertificateCheck` | `Actual` |
| 82 | `CertificateSignedMaxChainSize` | VariableAttribute | `SecurityCtrlr` | `MaxCertificateChainSize` | `Actual` |
| 83 | `CpoName` | VariableAttribute | `SecurityCtrlr` | `OrganizationName` | `Actual` |
| 84 | `CertSigningWaitMinimum` | VariableAttribute | `SecurityCtrlr` | `CertSigningWaitMinimum` | `Actual` |
| 85 | `CertSigningRepeatTimes` | VariableAttribute | `SecurityCtrlr` | `CertSigningRepeatTimes` | `Actual` |
| 86 | `CertificateStoreMaxLength` | VariableAttribute | `SecurityCtrlr` | `CertificateEntries` | `Actual` |

## PnC Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 87 | `ISO15118PnCEnabled` | VariableAttribute | `ISO15118Ctrlr` | `PnCEnabled` | `Actual` |
| 88 | `CentralContractValidationAllowed` | VariableAttribute | `ISO15118Ctrlr` | `CentralContractValidationAllowed` | `Actual` |
| 89 | `ContractValidationOffline` | VariableAttribute | `ISO15118Ctrlr` | `ContractValidationOffline` | `Actual` |

## CostAndPrice Profile

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 90 | `NumberOfDecimalsForCostValues` | VariableAttribute | `TariffCostCtrlr` | `NumberOfDecimalsForCostValues` | `Actual` |
| 91 | `TimeOffset` | VariableAttribute | `ClockCtrlr` | `TimeOffset` | `Actual` |
| 92 | `NextTimeOffsetTransitionDateTime` | VariableAttribute | `ClockCtrlr` | `NextTimeOffsetTransitionDateTime` | `Actual` |
| 93 | `TimeOffsetNextTransition` | VariableAttribute | `ClockCtrlr` | `NextTransition` | `Actual` |

## Mavericks Section

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 94 | `BlinkRepeat` | VariableAttribute | `OCPP16LegacyCtrlr` | `BlinkRepeat` | `Actual` |
| 95 | `ConnectorPhaseRotation` | VariableAttribute | `OCPP16LegacyCtrlr` | `PhaseRotation` | `Actual` |
| 96 | `ConnectorPhaseRotationMaxLength` | VariableAttribute | `OCPP16LegacyCtrlr` | `ConnectorPhaseRotationMaxLength` | `Actual` |
| 97 | `GetConfigurationMaxKeys` | VariableAttribute | `OCPP16LegacyCtrlr` | `GetConfigurationMaxKeys` | `Actual` |
| 98 | `LightIntensity` | VariableAttribute | `OCPP16LegacyCtrlr` | `LightIntensity` | `Actual` |
| 99 | `MinimumStatusDuration` | VariableAttribute | `OCPP16LegacyCtrlr` | `MinimumStatusDuration` | `Actual` |
| 100 | `StopTransactionOnEVSideDisconnect` | VariableAttribute | `OCPP16LegacyCtrlr` | `StopTransactionOnEVSideDisconnect` | `Actual` |
| 101 | `SupportedFeatureProfiles` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedFeatureProfiles` | `Actual` |
| 102 | `SupportedFeatureProfilesMaxLength` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedFeatureProfilesMaxLength` | `Actual` |
| 103 | `UnlockConnectorOnEVSideDisconnect` | VariableAttribute | `OCPP16LegacyCtrlr` | `UnlockConnectorOnEVSideDisconnect` | `Actual` |
| 104 | `ReserveConnectorZeroSupported` | VariableAttribute | `OCPP16LegacyCtrlr` | `ReserveConnectorZeroSupported` | `Actual` |
| 105 | `AllowChargingProfileWithoutStartSchedule` | VariableAttribute | `OCPP16LegacyCtrlr` | `AllowChargingProfileWithoutStartSchedule` | `Actual` |
| 106 | `WaitForStopTransactionsOnResetTimeout` | VariableAttribute | `OCPP16LegacyCtrlr` | `WaitForStopTransactionsOnResetTimeout` | `Actual` |
| 107 | `StopTransactionIfUnlockNotSupported` | VariableAttribute | `OCPP16LegacyCtrlr` | `StopTransactionIfUnlockNotSupported` | `Actual` |
| 108 | `MeterPublicKeys` | VariableAttribute | `OCPP16LegacyCtrlr` | `MeterPublicKeys` | `Actual` |
| 109 | `DisableSecurityEventNotifications` | VariableAttribute | `OCPP16LegacyCtrlr` | `DisableSecurityEventNotifications` | `Actual` |
| 110 | `ISO15118CertificateManagementEnabled` | VariableAttribute | `OCPP16LegacyCtrlr` | `ISO15118CertificateManagementEnabled` | `Actual` |
| 111 | `CustomDisplayCostAndPrice` | VariableAttribute | `OCPP16LegacyCtrlr` | `CustomDisplayCostAndPrice` | `Actual` |
| 112 | `DefaultPrice` | VariableAttribute | `OCPP16LegacyCtrlr` | `DefaultPrice` | `Actual` |
| 113 | `DefaultPriceText` | VariableAttribute | `OCPP16LegacyCtrlr` | `DefaultPriceText` | `Actual` |
| 114 | `CustomIdleFeeAfterStop` | VariableAttribute | `OCPP16LegacyCtrlr` | `CustomIdleFeeAfterStop` | `Actual` |
| 115 | `SupportedLanguages` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedLanguages` | `Actual` |
| 116 | `CustomMultiLanguageMessages` | VariableAttribute | `OCPP16LegacyCtrlr` | `CustomMultiLanguageMessages` | `Actual` |
| 117 | `Language` | VariableAttribute | `OCPP16LegacyCtrlr` | `Language` | `Actual` |
| 118 | `WaitForSetUserPriceTimeout` | VariableAttribute | `OCPP16LegacyCtrlr` | `WaitForSetUserPriceTimeout` | `Actual` |
| 119 | `NumberOfConnectors` | VariableAttribute | `OCPP16LegacyCtrlr` | `NumberOfConnectors` | `Actual` |
| 120 | `ConnectorEvseIds` | VariableAttribute | `OCPP16LegacyCtrlr` | `ConnectorEvseIds` | `Actual` |
| 127 | `ReportClearedErrors` | VariableAttribute | `OCPP16LegacyCtrlr` | `ReportClearedErrors` | `Actual` |


## Network Connection Profile

Keys in this section target the active connection slot. The component is `NetworkConfiguration` with instance `"1"`, written as `NetworkConfiguration[1]` below.

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 121 | `HostName` | VariableAttribute | `NetworkConfiguration[1]` | `HostName` | `Actual` |
| 122 | `CentralSystemURI` | VariableAttribute | `NetworkConfiguration[1]` | `OcppCsmsUrl` | `Actual` |
| 123 | `SecurityProfile` | VariableAttribute | `NetworkConfiguration[1]` | `SecurityProfile` | `Actual` |
| 124 | `AuthorizationKey` | VariableAttribute | `NetworkConfiguration[1]` | `BasicAuthPassword` | `Actual` |
| 125 | `ChargePointId` | VariableAttribute | `NetworkConfiguration[1]` | `Identity` | `Actual` |

## Keys without a direct mapping

| ID | OCPP1.6 key | Mapping type | Component | Variable | Target field |
| ---: | --- | --- | --- | --- | --- |
| 126 | `SupportedMeasurands` | VariableCharacteristics | `*` | `Measurands` | `valuesList` |
