
# OCPP 1.6 to DeviceModel Mapping Reference

This documents how OCPP 1.6 configuration keys map to the Device Model representation. OCPP introduced the device model with OCPP2.0.1. Initially the configuration parameters and storage were strictly separated in this 
library. In order to support a common configuration backend for all protocol versions, the OCPP 1.6 configuration keys are now mapped to the Device Model as well. This allows for a unified configuration management and storage approach across protocol versions.

This document is intended as a **human-readable reference** for integrators and maintainers.
The implementation in code (for example `known_keys` conversion logic and patching behaviour) remains authoritative.
A component or variable with an instance is written as `Name[instance]`.

A machine-readable list of the built-in mappings is generated from the code at build time by
`ocpp16_default_mappings_generator` (`tools/ocpp16_default_mappings/`) and installed as
`share/everest/modules/OCPP/default_ocpp-16_mappings.yaml`. It lists every known key with its section, target
Component/Variable and target field (`Actual` or `maxLimit`), plus the keys without an OCPP 2.x mapping. It is a
reference document, not a custom mapping file for `Ocpp16KeyMappingsPath`. The generator is not run when
cross-compiling without `CMAKE_CROSSCOMPILING_EMULATOR`.

## Core Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `AllowOfflineTxForUnknownId` | VariableAttribute | `AuthCtrlr` | `OfflineTxForUnknownIdEnabled` | `Actual` |
| `AuthorizationCacheEnabled` | VariableAttribute | `AuthCacheCtrlr` | `Enabled` | `Actual` |
| `AuthorizeRemoteTxRequests` | VariableAttribute | `AuthCtrlr` | `AuthorizeRemoteStart` | `Actual` |
| `ClockAlignedDataInterval` | VariableAttribute | `AlignedDataCtrlr` | `Interval` | `Actual` |
| `ConnectionTimeOut` | VariableAttribute | `TxCtrlr` | `EVConnectionTimeOut` | `Actual` |
| `HeartbeatInterval` | VariableAttribute | `OCPPCommCtrlr` | `HeartbeatInterval` | `Actual` |
| `LocalAuthorizeOffline` | VariableAttribute | `AuthCtrlr` | `LocalAuthorizeOffline` | `Actual` |
| `LocalPreAuthorize` | VariableAttribute | `AuthCtrlr` | `LocalPreAuthorize` | `Actual` |
| `MaxEnergyOnInvalidId` | VariableAttribute | `TxCtrlr` | `MaxEnergyOnInvalidId` | `Actual` |
| `MeterValuesAlignedData` | VariableAttribute | `AlignedDataCtrlr` | `Measurands` | `Actual` |
| `MeterValuesAlignedDataMaxLength` | VariableCharacteristics | `AlignedDataCtrlr` | `Measurands` | `maxLimit` |
| `MeterValuesSampledData` | VariableAttribute | `SampledDataCtrlr` | `TxUpdatedMeasurands` | `Actual` |
| `MeterValuesSampledDataMaxLength` | VariableCharacteristics | `SampledDataCtrlr` | `TxUpdatedMeasurands` | `maxLimit` |
| `MeterValueSampleInterval` | VariableAttribute | `SampledDataCtrlr` | `TxUpdatedInterval` | `Actual` |
| `ResetRetries` | VariableAttribute | `OCPPCommCtrlr` | `ResetRetries` | `Actual` |
| `StopTransactionOnEVSideDisconnect` | VariableAttribute | `TxCtrlr` | `StopTxOnEVSideDisconnect` | `Actual` |
| `StopTransactionOnInvalidId` | VariableAttribute | `TxCtrlr` | `StopTxOnInvalidId` | `Actual` |
| `StopTxnAlignedData` | VariableAttribute | `AlignedDataCtrlr` | `TxEndedMeasurands` | `Actual` |
| `StopTxnAlignedDataMaxLength` | VariableCharacteristics | `AlignedDataCtrlr` | `TxEndedMeasurands` | `maxLimit` |
| `StopTxnSampledData` | VariableAttribute | `SampledDataCtrlr` | `TxEndedMeasurands` | `Actual` |
| `StopTxnSampledDataMaxLength` | VariableCharacteristics | `SampledDataCtrlr` | `TxEndedMeasurands` | `maxLimit` |
| `TransactionMessageAttempts` | VariableAttribute | `OCPPCommCtrlr` | `MessageAttempts[TransactionEvent]` | `Actual` |
| `TransactionMessageRetryInterval` | VariableAttribute | `OCPPCommCtrlr` | `MessageAttemptInterval[TransactionEvent]` | `Actual` |
| `UnlockConnectorOnEVSideDisconnect` | VariableAttribute | `OCPPCommCtrlr` | `UnlockOnEVSideDisconnect` | `Actual` |
| `WebSocketPingInterval` | VariableAttribute | `OCPPCommCtrlr` | `WebSocketPingInterval` | `Actual` |
| `LocalAuthListEnabled` | VariableAttribute | `LocalAuthListCtrlr` | `Enabled` | `Actual` |
| `ChargeProfileMaxStackLevel` | VariableAttribute | `SmartChargingCtrlr` | `ProfileStackLevel` | `Actual` |
| `ChargingScheduleAllowedChargingRateUnit` | VariableAttribute | `SmartChargingCtrlr` | `RateUnit` | `Actual` |
| `ChargingScheduleMaxPeriods` | VariableAttribute | `SmartChargingCtrlr` | `PeriodsPerSchedule` | `Actual` |
| `ConnectorSwitch3to1PhaseSupported` | VariableAttribute | `SmartChargingCtrlr` | `Phases3to1` | `Actual` |
| `SupportedFileTransferProtocols` | VariableAttribute | `OCPPCommCtrlr` | `FileTransferProtocols` | `Actual` |

## Internal Keys

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `ChargeBoxSerialNumber` | VariableAttribute | `InternalCtrlr` | `ChargeBoxSerialNumber` | `Actual` |
| `ChargePointModel` | VariableAttribute | `InternalCtrlr` | `ChargePointModel` | `Actual` |
| `ChargePointSerialNumber` | VariableAttribute | `InternalCtrlr` | `ChargePointSerialNumber` | `Actual` |
| `ChargePointVendor` | VariableAttribute | `InternalCtrlr` | `ChargePointVendor` | `Actual` |
| `FirmwareVersion` | VariableAttribute | `InternalCtrlr` | `FirmwareVersion` | `Actual` |
| `ICCID` | VariableAttribute | `InternalCtrlr` | `ICCID` | `Actual` |
| `IFace` | VariableAttribute | `InternalCtrlr` | `IFace` | `Actual` |
| `IMSI` | VariableAttribute | `InternalCtrlr` | `IMSI` | `Actual` |
| `MeterSerialNumber` | VariableAttribute | `InternalCtrlr` | `MeterSerialNumber` | `Actual` |
| `MeterType` | VariableAttribute | `InternalCtrlr` | `MeterType` | `Actual` |
| `SupportedCiphers12` | VariableAttribute | `InternalCtrlr` | `SupportedCiphers12` | `Actual` |
| `SupportedCiphers13` | VariableAttribute | `InternalCtrlr` | `SupportedCiphers13` | `Actual` |
| `UseTPM` | VariableAttribute | `InternalCtrlr` | `UseTPM` | `Actual` |
| `UseTPMSeccLeafCertificate` | VariableAttribute | `InternalCtrlr` | `UseTPMSeccLeafCertificate` | `Actual` |
| `RetryBackoffRandomRange` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffRandomRange` | `Actual` |
| `RetryBackoffRepeatTimes` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffRepeatTimes` | `Actual` |
| `AuthEnabled` | VariableAttribute | `AuthCtrlr` | `Enabled` | `Actual` |
| `AuthorizeConnectorZeroOnConnectorOne` | VariableAttribute | `InternalCtrlr` | `AuthorizeConnectorZeroOnConnectorOne` | `Actual` |
| `LogMessages` | VariableAttribute | `InternalCtrlr` | `LogMessages` | `Actual` |
| `LogMessagesRaw` | VariableAttribute | `InternalCtrlr` | `LogMessagesRaw` | `Actual` |
| `LogMessagesFormat` | VariableAttribute | `InternalCtrlr` | `LogMessagesFormat` | `Actual` |
| `LogRotation` | VariableAttribute | `InternalCtrlr` | `LogRotation` | `Actual` |
| `LogRotationDateSuffix` | VariableAttribute | `InternalCtrlr` | `LogRotationDateSuffix` | `Actual` |
| `LogRotationMaximumFileSize` | VariableAttribute | `InternalCtrlr` | `LogRotationMaximumFileSize` | `Actual` |
| `LogRotationMaximumFileCount` | VariableAttribute | `InternalCtrlr` | `LogRotationMaximumFileCount` | `Actual` |
| `SupportedChargingProfilePurposeTypes` | VariableAttribute | `InternalCtrlr` | `SupportedChargingProfilePurposeTypes` | `Actual` |
| `IgnoredProfilePurposesOffline` | VariableAttribute | `SmartChargingCtrlr` | `IgnoredProfilePurposesOffline` | `Actual` |
| `MaxCompositeScheduleDuration` | VariableAttribute | `InternalCtrlr` | `MaxCompositeScheduleDuration` | `Actual` |
| `CompositeScheduleDefaultLimitAmps` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultLimitAmps` | `Actual` |
| `CompositeScheduleDefaultLimitWatts` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultLimitWatts` | `Actual` |
| `CompositeScheduleDefaultNumberPhases` | VariableAttribute | `SmartChargingCtrlr` | `CompositeScheduleDefaultNumberPhases` | `Actual` |
| `SupplyVoltage` | VariableAttribute | `SmartChargingCtrlr` | `SupplyVoltage` | `Actual` |
| `WebsocketPingPayload` | VariableAttribute | `InternalCtrlr` | `WebsocketPingPayload` | `Actual` |
| `WebsocketPongTimeout` | VariableAttribute | `InternalCtrlr` | `WebsocketPongTimeout` | `Actual` |
| `UseSslDefaultVerifyPaths` | VariableAttribute | `InternalCtrlr` | `UseSslDefaultVerifyPaths` | `Actual` |
| `VerifyCsmsCommonName` | VariableAttribute | `InternalCtrlr` | `VerifyCsmsCommonName` | `Actual` |
| `VerifyCsmsAllowWildcards` | VariableAttribute | `InternalCtrlr` | `VerifyCsmsAllowWildcards` | `Actual` |
| `OcspRequestInterval` | VariableAttribute | `InternalCtrlr` | `OcspRequestInterval` | `Actual` |
| `SeccLeafSubjectCommonName` | VariableAttribute | `ISO15118Ctrlr` | `SeccId` | `Actual` |
| `SeccLeafSubjectCountry` | VariableAttribute | `ISO15118Ctrlr` | `CountryName` | `Actual` |
| `SeccLeafSubjectOrganization` | VariableAttribute | `ISO15118Ctrlr` | `OrganizationName` | `Actual` |
| `QueueAllMessages` | VariableAttribute | `OCPPCommCtrlr` | `QueueAllMessages` | `Actual` |
| `MessageTypesDiscardForQueueing` | VariableAttribute | `OCPPCommCtrlr` | `MessageTypesDiscardForQueueing` | `Actual` |
| `MessageQueueSizeThreshold` | VariableAttribute | `InternalCtrlr` | `MessageQueueSizeThreshold` | `Actual` |
| `MaxMessageSize` | VariableAttribute | `InternalCtrlr` | `MaxMessageSize` | `Actual` |
| `TLSKeylogFile` | VariableAttribute | `InternalCtrlr` | `TLSKeylogFile` | `Actual` |
| `EnableTLSKeylog` | VariableAttribute | `InternalCtrlr` | `EnableTLSKeylog` | `Actual` |
| `RetryBackoffWaitMinimum` | VariableAttribute | `OCPPCommCtrlr` | `RetryBackOffWaitMinimum` | `Actual` |
| `ReportSuspendedEVSEReasonChange` | VariableAttribute | `InternalCtrlr` | `ReportSuspendedEVSEReasonChange` | `Actual` |

## Local Auth List Management Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `LocalAuthListMaxLength` | VariableCharacteristics | `LocalAuthListCtrlr` | `Entries` | `maxLimit` |
| `SendLocalListMaxLength` | VariableAttribute | `LocalAuthListCtrlr` | `ItemsPerMessage` | `Actual` |

## Smart Charging Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `MaxChargingProfilesInstalled` | VariableCharacteristics | `SmartChargingCtrlr` | `Entries[ChargingProfiles]` | `maxLimit` |

## Security Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `AdditionalRootCertificateCheck` | VariableAttribute | `SecurityCtrlr` | `AdditionalRootCertificateCheck` | `Actual` |
| `CertificateSignedMaxChainSize` | VariableAttribute | `SecurityCtrlr` | `MaxCertificateChainSize` | `Actual` |
| `CpoName` | VariableAttribute | `SecurityCtrlr` | `OrganizationName` | `Actual` |
| `CertSigningWaitMinimum` | VariableAttribute | `SecurityCtrlr` | `CertSigningWaitMinimum` | `Actual` |
| `CertSigningRepeatTimes` | VariableAttribute | `SecurityCtrlr` | `CertSigningRepeatTimes` | `Actual` |
| `CertificateStoreMaxLength` | VariableAttribute | `SecurityCtrlr` | `CertificateEntries` | `Actual` |

## PnC Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `ISO15118PnCEnabled` | VariableAttribute | `ISO15118Ctrlr` | `PnCEnabled` | `Actual` |
| `CentralContractValidationAllowed` | VariableAttribute | `ISO15118Ctrlr` | `CentralContractValidationAllowed` | `Actual` |
| `ContractValidationOffline` | VariableAttribute | `ISO15118Ctrlr` | `ContractValidationOffline` | `Actual` |

## CostAndPrice Profile

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `NumberOfDecimalsForCostValues` | VariableAttribute | `TariffCostCtrlr` | `NumberOfDecimalsForCostValues` | `Actual` |
| `CustomDisplayCostAndPrice` | VariableAttribute | `CustomizationCtrlr` | `CustomImplementationEnabled[org.openchargealliance.costmsg]` | `Actual` |
| `CustomMultiLanguageMessages` | VariableAttribute | `CustomizationCtrlr` | `CustomImplementationEnabled[org.openchargealliance.multilanguage]` | `Actual` |
| `TimeOffset` | VariableAttribute | `ClockCtrlr` | `TimeOffset` | `Actual` |
| `NextTimeOffsetTransitionDateTime` | VariableAttribute | `ClockCtrlr` | `NextTimeOffsetTransitionDateTime` | `Actual` |
| `TimeOffsetNextTransition` | VariableAttribute | `ClockCtrlr` | `TimeOffset[NextTransition]` | `Actual` |

## Mavericks Section

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `BlinkRepeat` | VariableAttribute | `OCPP16LegacyCtrlr` | `BlinkRepeat` | `Actual` |
| `ConnectorPhaseRotation` | VariableAttribute | `OCPP16LegacyCtrlr` | `ConnectorPhaseRotation` | `Actual` |
| `ConnectorPhaseRotationMaxLength` | VariableAttribute | `OCPP16LegacyCtrlr` | `ConnectorPhaseRotationMaxLength` | `Actual` |
| `GetConfigurationMaxKeys` | VariableAttribute | `OCPP16LegacyCtrlr` | `GetConfigurationMaxKeys` | `Actual` |
| `LightIntensity` | VariableAttribute | `OCPP16LegacyCtrlr` | `LightIntensity` | `Actual` |
| `MinimumStatusDuration` | VariableAttribute | `OCPP16LegacyCtrlr` | `MinimumStatusDuration` | `Actual` |
| `SupportedFeatureProfiles` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedFeatureProfiles` | `Actual` |
| `SupportedFeatureProfilesMaxLength` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedFeatureProfilesMaxLength` | `Actual` |
| `ReserveConnectorZeroSupported` | VariableAttribute | `OCPP16LegacyCtrlr` | `ReserveConnectorZeroSupported` | `Actual` |
| `AllowChargingProfileWithoutStartSchedule` | VariableAttribute | `OCPP16LegacyCtrlr` | `AllowChargingProfileWithoutStartSchedule` | `Actual` |
| `WaitForStopTransactionsOnResetTimeout` | VariableAttribute | `OCPP16LegacyCtrlr` | `WaitForStopTransactionsOnResetTimeout` | `Actual` |
| `StopTransactionIfUnlockNotSupported` | VariableAttribute | `OCPP16LegacyCtrlr` | `StopTransactionIfUnlockNotSupported` | `Actual` |
| `MeterPublicKeys` | VariableAttribute | `OCPP16LegacyCtrlr` | `MeterPublicKeys` | `Actual` |
| `DisableSecurityEventNotifications` | VariableAttribute | `OCPP16LegacyCtrlr` | `DisableSecurityEventNotifications` | `Actual` |
| `ISO15118CertificateManagementEnabled` | VariableAttribute | `OCPP16LegacyCtrlr` | `ISO15118CertificateManagementEnabled` | `Actual` |
| `DefaultPrice` | VariableAttribute | `OCPP16LegacyCtrlr` | `DefaultPrice` | `Actual` |
| `DefaultPriceText` | VariableAttribute | `OCPP16LegacyCtrlr` | `DefaultPriceText` | `Actual` |
| `CustomIdleFeeAfterStop` | VariableAttribute | `OCPP16LegacyCtrlr` | `CustomIdleFeeAfterStop` | `Actual` |
| `SupportedLanguages` | VariableAttribute | `OCPP16LegacyCtrlr` | `SupportedLanguages` | `Actual` |
| `Language` | VariableAttribute | `OCPP16LegacyCtrlr` | `Language` | `Actual` |
| `WaitForSetUserPriceTimeout` | VariableAttribute | `OCPP16LegacyCtrlr` | `WaitForSetUserPriceTimeout` | `Actual` |
| `NumberOfConnectors` | VariableAttribute | `OCPP16LegacyCtrlr` | `NumberOfConnectors` | `Actual` |
| `ConnectorEvseIds` | VariableAttribute | `OCPP16LegacyCtrlr` | `ConnectorEvseIds` | `Actual` |
| `RejectRemoteStartTransactionWithoutConnectorId` | VariableAttribute | `OCPP16LegacyCtrlr` | `RejectRemoteStartTransactionWithoutConnectorId` | `Actual` |
| `RemoteStartTransactionWithoutConnectorIdFindFirst` | VariableAttribute | `OCPP16LegacyCtrlr` | `RemoteStartTransactionWithoutConnectorIdFindFirst` | `Actual` |
| `ReportClearedErrors` | VariableAttribute | `OCPP16LegacyCtrlr` | `ReportClearedErrors` | `Actual` |
| `SwitchSecurityProfileConnectionTimeout` | VariableAttribute | `OCPP16LegacyCtrlr` | `SwitchSecurityProfileConnectionTimeout` | `Actual` |


## Network Connection Profile

Keys in this section target the active connection slot. The component is `NetworkConfiguration` with instance `"1"`, written as `NetworkConfiguration[1]` below.

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `HostName` | VariableAttribute | `NetworkConfiguration[1]` | `HostName` | `Actual` |
| `CentralSystemURI` | VariableAttribute | `NetworkConfiguration[1]` | `OcppCsmsUrl` | `Actual` |
| `SecurityProfile` | VariableAttribute | `NetworkConfiguration[1]` | `SecurityProfile` | `Actual` |
| `AuthorizationKey` | VariableAttribute | `NetworkConfiguration[1]` | `BasicAuthPassword` | `Actual` |
| `ChargePointId` | VariableAttribute | `NetworkConfiguration[1]` | `Identity` | `Actual` |

## Keys without a direct mapping

| OCPP1.6 key | Mapping type | Component | Variable | Target field |
| --- | --- | --- | --- | --- |
| `SupportedMeasurands` | VariableCharacteristics | `*` | `Measurands` | `valuesList` |
