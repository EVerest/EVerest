// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The enums in message_2/common_types.hpp (including the shared -2/DIN ones aliased from
// message/shared_datatypes.hpp) hand-mirror the cbv2g generated iso2_* enums so that cb_convert_enum
// can static_cast between them. EXI encodes the numeric value, so a divergence (a typo here, or a
// cbv2g regeneration) silently corrupts the wire format. This TU pins every mirrored enumerator to
// its generated counterpart at compile time (same idea as the static_assert block in EvseV2G's
// v2g_server.cpp); it intentionally contains no runtime code.

#include <cbv2g/iso_2/iso2_msgDefDatatypes.h>

#include <iso15118/message_2/common_types.hpp>

#define ASSERT_CB_ENUM_EQ(cpp_value, cb_value)                                                                         \
    static_assert(static_cast<int>(cpp_value) == static_cast<int>(cb_value),                                           \
                  #cpp_value " diverged from the cbv2g generated value " #cb_value)

namespace iso15118::message_2::datatypes {

ASSERT_CB_ENUM_EQ(ResponseCode::OK, iso2_responseCodeType_OK);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_NewSessionEstablished, iso2_responseCodeType_OK_NewSessionEstablished);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_OldSessionJoined, iso2_responseCodeType_OK_OldSessionJoined);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_CertificateExpiresSoon, iso2_responseCodeType_OK_CertificateExpiresSoon);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED, iso2_responseCodeType_FAILED);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_SequenceError, iso2_responseCodeType_FAILED_SequenceError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ServiceIDInvalid, iso2_responseCodeType_FAILED_ServiceIDInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_UnknownSession, iso2_responseCodeType_FAILED_UnknownSession);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ServiceSelectionInvalid, iso2_responseCodeType_FAILED_ServiceSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_PaymentSelectionInvalid, iso2_responseCodeType_FAILED_PaymentSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertificateExpired, iso2_responseCodeType_FAILED_CertificateExpired);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_SignatureError, iso2_responseCodeType_FAILED_SignatureError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_NoCertificateAvailable, iso2_responseCodeType_FAILED_NoCertificateAvailable);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertChainError, iso2_responseCodeType_FAILED_CertChainError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ChallengeInvalid, iso2_responseCodeType_FAILED_ChallengeInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ContractCanceled, iso2_responseCodeType_FAILED_ContractCanceled);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_WrongChargeParameter, iso2_responseCodeType_FAILED_WrongChargeParameter);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_PowerDeliveryNotApplied, iso2_responseCodeType_FAILED_PowerDeliveryNotApplied);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_TariffSelectionInvalid, iso2_responseCodeType_FAILED_TariffSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ChargingProfileInvalid, iso2_responseCodeType_FAILED_ChargingProfileInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_MeteringSignatureNotValid,
                  iso2_responseCodeType_FAILED_MeteringSignatureNotValid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_NoChargeServiceSelected, iso2_responseCodeType_FAILED_NoChargeServiceSelected);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_WrongEnergyTransferMode, iso2_responseCodeType_FAILED_WrongEnergyTransferMode);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ContactorError, iso2_responseCodeType_FAILED_ContactorError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertificateNotAllowedAtThisEVSE,
                  iso2_responseCodeType_FAILED_CertificateNotAllowedAtThisEVSE);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertificateRevoked, iso2_responseCodeType_FAILED_CertificateRevoked);

ASSERT_CB_ENUM_EQ(EVSEProcessing::Finished, iso2_EVSEProcessingType_Finished);
ASSERT_CB_ENUM_EQ(EVSEProcessing::Ongoing, iso2_EVSEProcessingType_Ongoing);
ASSERT_CB_ENUM_EQ(EVSEProcessing::Ongoing_WaitingForCustomerInteraction,
                  iso2_EVSEProcessingType_Ongoing_WaitingForCustomerInteraction);

ASSERT_CB_ENUM_EQ(PaymentOption::Contract, iso2_paymentOptionType_Contract);
ASSERT_CB_ENUM_EQ(PaymentOption::ExternalPayment, iso2_paymentOptionType_ExternalPayment);

ASSERT_CB_ENUM_EQ(EnergyTransferMode::AC_single_phase_core, iso2_EnergyTransferModeType_AC_single_phase_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::AC_three_phase_core, iso2_EnergyTransferModeType_AC_three_phase_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_core, iso2_EnergyTransferModeType_DC_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_extended, iso2_EnergyTransferModeType_DC_extended);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_combo_core, iso2_EnergyTransferModeType_DC_combo_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_unique, iso2_EnergyTransferModeType_DC_unique);

ASSERT_CB_ENUM_EQ(ServiceCategory::EVCharging, iso2_serviceCategoryType_EVCharging);
ASSERT_CB_ENUM_EQ(ServiceCategory::Internet, iso2_serviceCategoryType_Internet);
ASSERT_CB_ENUM_EQ(ServiceCategory::ContractCertificate, iso2_serviceCategoryType_ContractCertificate);
ASSERT_CB_ENUM_EQ(ServiceCategory::OtherCustom, iso2_serviceCategoryType_OtherCustom);

ASSERT_CB_ENUM_EQ(ChargingSession::Terminate, iso2_chargingSessionType_Terminate);
ASSERT_CB_ENUM_EQ(ChargingSession::Pause, iso2_chargingSessionType_Pause);

ASSERT_CB_ENUM_EQ(ChargeProgress::Start, iso2_chargeProgressType_Start);
ASSERT_CB_ENUM_EQ(ChargeProgress::Stop, iso2_chargeProgressType_Stop);
ASSERT_CB_ENUM_EQ(ChargeProgress::Renegotiate, iso2_chargeProgressType_Renegotiate);

ASSERT_CB_ENUM_EQ(Unit::h, iso2_unitSymbolType_h);
ASSERT_CB_ENUM_EQ(Unit::m, iso2_unitSymbolType_m);
ASSERT_CB_ENUM_EQ(Unit::s, iso2_unitSymbolType_s);
ASSERT_CB_ENUM_EQ(Unit::A, iso2_unitSymbolType_A);
ASSERT_CB_ENUM_EQ(Unit::V, iso2_unitSymbolType_V);
ASSERT_CB_ENUM_EQ(Unit::W, iso2_unitSymbolType_W);
ASSERT_CB_ENUM_EQ(Unit::Wh, iso2_unitSymbolType_Wh);

ASSERT_CB_ENUM_EQ(DC_EVErrorCode::NO_ERROR, iso2_DC_EVErrorCodeType_NO_ERROR);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_RESSTemperatureInhibit, iso2_DC_EVErrorCodeType_FAILED_RESSTemperatureInhibit);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_EVShiftPosition, iso2_DC_EVErrorCodeType_FAILED_EVShiftPosition);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_ChargerConnectorLockFault,
                  iso2_DC_EVErrorCodeType_FAILED_ChargerConnectorLockFault);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_EVRESSMalfunction, iso2_DC_EVErrorCodeType_FAILED_EVRESSMalfunction);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_ChargingCurrentdifferential,
                  iso2_DC_EVErrorCodeType_FAILED_ChargingCurrentdifferential);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_ChargingVoltageOutOfRange,
                  iso2_DC_EVErrorCodeType_FAILED_ChargingVoltageOutOfRange);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::Reserved_A, iso2_DC_EVErrorCodeType_Reserved_A);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::Reserved_B, iso2_DC_EVErrorCodeType_Reserved_B);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::Reserved_C, iso2_DC_EVErrorCodeType_Reserved_C);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::FAILED_ChargingSystemIncompatibility,
                  iso2_DC_EVErrorCodeType_FAILED_ChargingSystemIncompatibility);
ASSERT_CB_ENUM_EQ(DC_EVErrorCode::NoData, iso2_DC_EVErrorCodeType_NoData);

ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_NotReady, iso2_DC_EVSEStatusCodeType_EVSE_NotReady);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_Ready, iso2_DC_EVSEStatusCodeType_EVSE_Ready);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_Shutdown, iso2_DC_EVSEStatusCodeType_EVSE_Shutdown);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_UtilityInterruptEvent, iso2_DC_EVSEStatusCodeType_EVSE_UtilityInterruptEvent);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_IsolationMonitoringActive,
                  iso2_DC_EVSEStatusCodeType_EVSE_IsolationMonitoringActive);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_EmergencyShutdown, iso2_DC_EVSEStatusCodeType_EVSE_EmergencyShutdown);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::EVSE_Malfunction, iso2_DC_EVSEStatusCodeType_EVSE_Malfunction);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::Reserved_8, iso2_DC_EVSEStatusCodeType_Reserved_8);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::Reserved_9, iso2_DC_EVSEStatusCodeType_Reserved_9);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::Reserved_A, iso2_DC_EVSEStatusCodeType_Reserved_A);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::Reserved_B, iso2_DC_EVSEStatusCodeType_Reserved_B);
ASSERT_CB_ENUM_EQ(DC_EVSEStatusCode::Reserved_C, iso2_DC_EVSEStatusCodeType_Reserved_C);

ASSERT_CB_ENUM_EQ(IsolationLevel::Invalid, iso2_isolationLevelType_Invalid);
ASSERT_CB_ENUM_EQ(IsolationLevel::Valid, iso2_isolationLevelType_Valid);
ASSERT_CB_ENUM_EQ(IsolationLevel::Warning, iso2_isolationLevelType_Warning);
ASSERT_CB_ENUM_EQ(IsolationLevel::Fault, iso2_isolationLevelType_Fault);
ASSERT_CB_ENUM_EQ(IsolationLevel::No_IMD, iso2_isolationLevelType_No_IMD);

ASSERT_CB_ENUM_EQ(EVSENotification::None, iso2_EVSENotificationType_None);
ASSERT_CB_ENUM_EQ(EVSENotification::StopCharging, iso2_EVSENotificationType_StopCharging);
ASSERT_CB_ENUM_EQ(EVSENotification::ReNegotiation, iso2_EVSENotificationType_ReNegotiation);

ASSERT_CB_ENUM_EQ(FaultCode::ParsingError, iso2_faultCodeType_ParsingError);
ASSERT_CB_ENUM_EQ(FaultCode::NoTLSRootCertificatAvailable, iso2_faultCodeType_NoTLSRootCertificatAvailable);
ASSERT_CB_ENUM_EQ(FaultCode::UnknownError, iso2_faultCodeType_UnknownError);

} // namespace iso15118::message_2::datatypes

#undef ASSERT_CB_ENUM_EQ
