// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// The enums in message_din/common_types.hpp (including the shared -2/DIN ones aliased from
// message/shared_datatypes.hpp) hand-mirror the cbv2g generated din_* enums so that cb_convert_enum
// can static_cast between them. EXI encodes the numeric value, so a divergence (a typo here, or a
// cbv2g regeneration) silently corrupts the wire format. This TU pins every mirrored enumerator to
// its generated counterpart at compile time (same idea as the static_assert block in EvseV2G's
// v2g_server.cpp); it intentionally contains no runtime code.

#include <cbv2g/din/din_msgDefDatatypes.h>

#include <iso15118/message_din/common_types.hpp>

#define ASSERT_CB_ENUM_EQ(cpp_value, cb_value)                                                                         \
    static_assert(static_cast<int>(cpp_value) == static_cast<int>(cb_value),                                           \
                  #cpp_value " diverged from the cbv2g generated value " #cb_value)

namespace iso15118::message_din::datatypes {

ASSERT_CB_ENUM_EQ(ResponseCode::OK, din_responseCodeType_OK);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_NewSessionEstablished, din_responseCodeType_OK_NewSessionEstablished);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_OldSessionJoined, din_responseCodeType_OK_OldSessionJoined);
ASSERT_CB_ENUM_EQ(ResponseCode::OK_CertificateExpiresSoon, din_responseCodeType_OK_CertificateExpiresSoon);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED, din_responseCodeType_FAILED);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_SequenceError, din_responseCodeType_FAILED_SequenceError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ServiceIDInvalid, din_responseCodeType_FAILED_ServiceIDInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_UnknownSession, din_responseCodeType_FAILED_UnknownSession);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ServiceSelectionInvalid, din_responseCodeType_FAILED_ServiceSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_PaymentSelectionInvalid, din_responseCodeType_FAILED_PaymentSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertificateExpired, din_responseCodeType_FAILED_CertificateExpired);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_SignatureError, din_responseCodeType_FAILED_SignatureError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_NoCertificateAvailable, din_responseCodeType_FAILED_NoCertificateAvailable);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_CertChainError, din_responseCodeType_FAILED_CertChainError);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ChallengeInvalid, din_responseCodeType_FAILED_ChallengeInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ContractCanceled, din_responseCodeType_FAILED_ContractCanceled);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_WrongChargeParameter, din_responseCodeType_FAILED_WrongChargeParameter);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_PowerDeliveryNotApplied, din_responseCodeType_FAILED_PowerDeliveryNotApplied);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_TariffSelectionInvalid, din_responseCodeType_FAILED_TariffSelectionInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_ChargingProfileInvalid, din_responseCodeType_FAILED_ChargingProfileInvalid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_EVSEPresentVoltageToLow, din_responseCodeType_FAILED_EVSEPresentVoltageToLow);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_MeteringSignatureNotValid,
                  din_responseCodeType_FAILED_MeteringSignatureNotValid);
ASSERT_CB_ENUM_EQ(ResponseCode::FAILED_WrongEnergyTransferType, din_responseCodeType_FAILED_WrongEnergyTransferType);

ASSERT_CB_ENUM_EQ(EvseProcessing::Finished, din_EVSEProcessingType_Finished);
ASSERT_CB_ENUM_EQ(EvseProcessing::Ongoing, din_EVSEProcessingType_Ongoing);

ASSERT_CB_ENUM_EQ(PaymentOption::Contract, din_paymentOptionType_Contract);
ASSERT_CB_ENUM_EQ(PaymentOption::ExternalPayment, din_paymentOptionType_ExternalPayment);

ASSERT_CB_ENUM_EQ(ServiceCategory::EVCharging, din_serviceCategoryType_EVCharging);
ASSERT_CB_ENUM_EQ(ServiceCategory::Internet, din_serviceCategoryType_Internet);
ASSERT_CB_ENUM_EQ(ServiceCategory::ContractCertificate, din_serviceCategoryType_ContractCertificate);
ASSERT_CB_ENUM_EQ(ServiceCategory::OtherCustom, din_serviceCategoryType_OtherCustom);

ASSERT_CB_ENUM_EQ(EnergyTransferMode::AC_single_phase_core, din_EVRequestedEnergyTransferType_AC_single_phase_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::AC_three_phase_core, din_EVRequestedEnergyTransferType_AC_three_phase_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_core, din_EVRequestedEnergyTransferType_DC_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_extended, din_EVRequestedEnergyTransferType_DC_extended);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_combo_core, din_EVRequestedEnergyTransferType_DC_combo_core);
ASSERT_CB_ENUM_EQ(EnergyTransferMode::DC_unique, din_EVRequestedEnergyTransferType_DC_unique);

ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_single_phase_core,
                  din_EVSESupportedEnergyTransferType_AC_single_phase_core);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_three_phase_core,
                  din_EVSESupportedEnergyTransferType_AC_three_phase_core);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::DC_core, din_EVSESupportedEnergyTransferType_DC_core);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::DC_extended, din_EVSESupportedEnergyTransferType_DC_extended);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::DC_combo_core, din_EVSESupportedEnergyTransferType_DC_combo_core);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::DC_dual, din_EVSESupportedEnergyTransferType_DC_dual);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_core1p_DC_extended,
                  din_EVSESupportedEnergyTransferType_AC_core1p_DC_extended);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_single_DC_core,
                  din_EVSESupportedEnergyTransferType_AC_single_DC_core);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_single_phase_three_phase_core_DC_extended,
                  din_EVSESupportedEnergyTransferType_AC_single_phase_three_phase_core_DC_extended);
ASSERT_CB_ENUM_EQ(SupportedEnergyTransferMode::AC_core3p_DC_extended,
                  din_EVSESupportedEnergyTransferType_AC_core3p_DC_extended);

ASSERT_CB_ENUM_EQ(IsolationLevel::Invalid, din_isolationLevelType_Invalid);
ASSERT_CB_ENUM_EQ(IsolationLevel::Valid, din_isolationLevelType_Valid);
ASSERT_CB_ENUM_EQ(IsolationLevel::Warning, din_isolationLevelType_Warning);
ASSERT_CB_ENUM_EQ(IsolationLevel::Fault, din_isolationLevelType_Fault);

ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_NotReady, din_DC_EVSEStatusCodeType_EVSE_NotReady);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_Ready, din_DC_EVSEStatusCodeType_EVSE_Ready);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_Shutdown, din_DC_EVSEStatusCodeType_EVSE_Shutdown);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_UtilityInterruptEvent, din_DC_EVSEStatusCodeType_EVSE_UtilityInterruptEvent);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_IsolationMonitoringActive,
                  din_DC_EVSEStatusCodeType_EVSE_IsolationMonitoringActive);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_EmergencyShutdown, din_DC_EVSEStatusCodeType_EVSE_EmergencyShutdown);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::EVSE_Malfunction, din_DC_EVSEStatusCodeType_EVSE_Malfunction);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::Reserved_8, din_DC_EVSEStatusCodeType_Reserved_8);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::Reserved_9, din_DC_EVSEStatusCodeType_Reserved_9);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::Reserved_A, din_DC_EVSEStatusCodeType_Reserved_A);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::Reserved_B, din_DC_EVSEStatusCodeType_Reserved_B);
ASSERT_CB_ENUM_EQ(DcEvseStatusCode::Reserved_C, din_DC_EVSEStatusCodeType_Reserved_C);

ASSERT_CB_ENUM_EQ(DcEvErrorCode::NO_ERROR, din_DC_EVErrorCodeType_NO_ERROR);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_RESSTemperatureInhibit, din_DC_EVErrorCodeType_FAILED_RESSTemperatureInhibit);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_EVShiftPosition, din_DC_EVErrorCodeType_FAILED_EVShiftPosition);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_ChargerConnectorLockFault,
                  din_DC_EVErrorCodeType_FAILED_ChargerConnectorLockFault);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_EVRESSMalfunction, din_DC_EVErrorCodeType_FAILED_EVRESSMalfunction);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_ChargingCurrentdifferential,
                  din_DC_EVErrorCodeType_FAILED_ChargingCurrentdifferential);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_ChargingVoltageOutOfRange,
                  din_DC_EVErrorCodeType_FAILED_ChargingVoltageOutOfRange);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::Reserved_A, din_DC_EVErrorCodeType_Reserved_A);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::Reserved_B, din_DC_EVErrorCodeType_Reserved_B);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::Reserved_C, din_DC_EVErrorCodeType_Reserved_C);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::FAILED_ChargingSystemIncompatibility,
                  din_DC_EVErrorCodeType_FAILED_ChargingSystemIncompatibility);
ASSERT_CB_ENUM_EQ(DcEvErrorCode::NoData, din_DC_EVErrorCodeType_NoData);

ASSERT_CB_ENUM_EQ(EvseNotification::None, din_EVSENotificationType_None);
ASSERT_CB_ENUM_EQ(EvseNotification::StopCharging, din_EVSENotificationType_StopCharging);
ASSERT_CB_ENUM_EQ(EvseNotification::ReNegotiation, din_EVSENotificationType_ReNegotiation);

ASSERT_CB_ENUM_EQ(Unit::h, din_unitSymbolType_h);
ASSERT_CB_ENUM_EQ(Unit::m, din_unitSymbolType_m);
ASSERT_CB_ENUM_EQ(Unit::s, din_unitSymbolType_s);
ASSERT_CB_ENUM_EQ(Unit::A, din_unitSymbolType_A);
ASSERT_CB_ENUM_EQ(Unit::Ah, din_unitSymbolType_Ah);
ASSERT_CB_ENUM_EQ(Unit::V, din_unitSymbolType_V);
ASSERT_CB_ENUM_EQ(Unit::VA, din_unitSymbolType_VA);
ASSERT_CB_ENUM_EQ(Unit::W, din_unitSymbolType_W);
ASSERT_CB_ENUM_EQ(Unit::W_s, din_unitSymbolType_W_s);
ASSERT_CB_ENUM_EQ(Unit::Wh, din_unitSymbolType_Wh);

} // namespace iso15118::message_din::datatypes

#undef ASSERT_CB_ENUM_EQ
