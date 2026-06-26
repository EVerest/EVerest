/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2022 - 2023 chargebyte GmbH
 * Copyright (C) 2022 - 2023 Contributors to EVerest
 */

/*****************************************************
 *
 * @author
 * @version
 *
 * The Code is generated! Changes may be overwritten.
 *
 *****************************************************/

/**
  * @file iso20_AC_DER_SAE_Datatypes.c
  * @brief Description goes here
  *
  **/
#include "cbv2g/iso_20/iso20_AC_DER_SAE_Datatypes.h"



// root elements of EXI doc
void init_iso20_ac_der_sae_exiDocument(struct iso20_ac_der_sae_exiDocument* exiDoc) {
    exiDoc->AC_ChargeParameterDiscoveryReq_isUsed = 0u;
    exiDoc->AC_ChargeParameterDiscoveryRes_isUsed = 0u;
    exiDoc->AC_ChargeLoopReq_isUsed = 0u;
    exiDoc->AC_ChargeLoopRes_isUsed = 0u;
    exiDoc->AC_CPDReqEnergyTransferMode_isUsed = 0u;
    exiDoc->AC_CPDResEnergyTransferMode_isUsed = 0u;
    exiDoc->BPT_AC_CPDReqEnergyTransferMode_isUsed = 0u;
    exiDoc->BPT_AC_CPDResEnergyTransferMode_isUsed = 0u;
    exiDoc->DER_AC_CPDReqEnergyTransferMode_isUsed = 0u;
    exiDoc->DER_AC_CPDResEnergyTransferMode_isUsed = 0u;
    exiDoc->CLReqControlMode_isUsed = 0u;
    exiDoc->Scheduled_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->CLResControlMode_isUsed = 0u;
    exiDoc->Scheduled_AC_CLResControlMode_isUsed = 0u;
    exiDoc->BPT_Scheduled_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->BPT_Scheduled_AC_CLResControlMode_isUsed = 0u;
    exiDoc->Dynamic_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->Dynamic_AC_CLResControlMode_isUsed = 0u;
    exiDoc->BPT_Dynamic_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->BPT_Dynamic_AC_CLResControlMode_isUsed = 0u;
    exiDoc->DER_Scheduled_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->DER_Scheduled_AC_CLResControlMode_isUsed = 0u;
    exiDoc->DER_Dynamic_AC_CLReqControlMode_isUsed = 0u;
    exiDoc->DER_Dynamic_AC_CLResControlMode_isUsed = 0u;
    exiDoc->FrequencyDroop_isUsed = 0u;
    exiDoc->Signature_isUsed = 0u;
    exiDoc->SignatureValue_isUsed = 0u;
    exiDoc->SignedInfo_isUsed = 0u;
    exiDoc->CanonicalizationMethod_isUsed = 0u;
    exiDoc->SignatureMethod_isUsed = 0u;
    exiDoc->Reference_isUsed = 0u;
    exiDoc->Transforms_isUsed = 0u;
    exiDoc->Transform_isUsed = 0u;
    exiDoc->DigestMethod_isUsed = 0u;
    exiDoc->KeyInfo_isUsed = 0u;
    exiDoc->KeyValue_isUsed = 0u;
    exiDoc->RetrievalMethod_isUsed = 0u;
    exiDoc->X509Data_isUsed = 0u;
    exiDoc->PGPData_isUsed = 0u;
    exiDoc->SPKIData_isUsed = 0u;
    exiDoc->Object_isUsed = 0u;
    exiDoc->Manifest_isUsed = 0u;
    exiDoc->SignatureProperties_isUsed = 0u;
    exiDoc->SignatureProperty_isUsed = 0u;
    exiDoc->DSAKeyValue_isUsed = 0u;
    exiDoc->RSAKeyValue_isUsed = 0u;
}
void init_iso20_ac_der_sae_TransformType(struct iso20_ac_der_sae_TransformType* TransformType) {
    TransformType->ANY_isUsed = 0u;
    TransformType->XPath_isUsed = 0u;
}

void init_iso20_ac_der_sae_TransformsType(struct iso20_ac_der_sae_TransformsType* TransformsType) {
    (void) TransformsType;
}

void init_iso20_ac_der_sae_DSAKeyValueType(struct iso20_ac_der_sae_DSAKeyValueType* DSAKeyValueType) {
    DSAKeyValueType->P_isUsed = 0u;
    DSAKeyValueType->Q_isUsed = 0u;
    DSAKeyValueType->G_isUsed = 0u;
    DSAKeyValueType->J_isUsed = 0u;
    DSAKeyValueType->Seed_isUsed = 0u;
    DSAKeyValueType->PgenCounter_isUsed = 0u;
}

void init_iso20_ac_der_sae_X509IssuerSerialType(struct iso20_ac_der_sae_X509IssuerSerialType* X509IssuerSerialType) {
    (void) X509IssuerSerialType;
}

void init_iso20_ac_der_sae_DataTupleType(struct iso20_ac_der_sae_DataTupleType* DataTupleType) {
    (void) DataTupleType;
}

void init_iso20_ac_der_sae_DigestMethodType(struct iso20_ac_der_sae_DigestMethodType* DigestMethodType) {
    DigestMethodType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_RSAKeyValueType(struct iso20_ac_der_sae_RSAKeyValueType* RSAKeyValueType) {
    (void) RSAKeyValueType;
}

void init_iso20_ac_der_sae_CanonicalizationMethodType(struct iso20_ac_der_sae_CanonicalizationMethodType* CanonicalizationMethodType) {
    CanonicalizationMethodType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignatureMethodType(struct iso20_ac_der_sae_SignatureMethodType* SignatureMethodType) {
    SignatureMethodType->HMACOutputLength_isUsed = 0u;
    SignatureMethodType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_KeyValueType(struct iso20_ac_der_sae_KeyValueType* KeyValueType) {
    KeyValueType->DSAKeyValue_isUsed = 0u;
    KeyValueType->RSAKeyValue_isUsed = 0u;
    KeyValueType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_ReferenceType(struct iso20_ac_der_sae_ReferenceType* ReferenceType) {
    ReferenceType->Id_isUsed = 0u;
    ReferenceType->Type_isUsed = 0u;
    ReferenceType->URI_isUsed = 0u;
    ReferenceType->Transforms_isUsed = 0u;
}

void init_iso20_ac_der_sae_RetrievalMethodType(struct iso20_ac_der_sae_RetrievalMethodType* RetrievalMethodType) {
    RetrievalMethodType->Type_isUsed = 0u;
    RetrievalMethodType->URI_isUsed = 0u;
    RetrievalMethodType->Transforms_isUsed = 0u;
}

void init_iso20_ac_der_sae_FrequencyDroopSettingsType(struct iso20_ac_der_sae_FrequencyDroopSettingsType* FrequencyDroopSettingsType) {
    FrequencyDroopSettingsType->DroopFactor_L2_isUsed = 0u;
    FrequencyDroopSettingsType->DroopFactor_L3_isUsed = 0u;
    FrequencyDroopSettingsType->PowerReference_L2_isUsed = 0u;
    FrequencyDroopSettingsType->PowerReference_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_X509DataType(struct iso20_ac_der_sae_X509DataType* X509DataType) {
    X509DataType->X509IssuerSerial_isUsed = 0u;
    X509DataType->X509SKI_isUsed = 0u;
    X509DataType->X509SubjectName_isUsed = 0u;
    X509DataType->X509Certificate_isUsed = 0u;
    X509DataType->X509CRL_isUsed = 0u;
    X509DataType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_PGPDataType(struct iso20_ac_der_sae_PGPDataType* PGPDataType) {
    PGPDataType->choice_1_isUsed = 0u;
    PGPDataType->choice_2_isUsed = 0u;
}

void init_iso20_ac_der_sae_CurveDataPointsListType(struct iso20_ac_der_sae_CurveDataPointsListType* CurveDataPointsListType) {
    CurveDataPointsListType->CurveDataPoint.arrayLen = 0u;
}

void init_iso20_ac_der_sae_SPKIDataType(struct iso20_ac_der_sae_SPKIDataType* SPKIDataType) {
    SPKIDataType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignedInfoType(struct iso20_ac_der_sae_SignedInfoType* SignedInfoType) {
    SignedInfoType->Reference.arrayLen = 0u;
    SignedInfoType->Id_isUsed = 0u;
}

void init_iso20_ac_der_sae_DERCurveType(struct iso20_ac_der_sae_DERCurveType* DERCurveType) {
    DERCurveType->Priority_isUsed = 0u;
    DERCurveType->CurveDataPoints_L2_isUsed = 0u;
    DERCurveType->CurveDataPoints_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_ConstantPowerFactorType(struct iso20_ac_der_sae_ConstantPowerFactorType* ConstantPowerFactorType) {
    ConstantPowerFactorType->Priority_isUsed = 0u;
    ConstantPowerFactorType->PowerFactorValue_L2_isUsed = 0u;
    ConstantPowerFactorType->PowerFactorValue_L3_isUsed = 0u;
    ConstantPowerFactorType->PowerFactorExcitation_L2_isUsed = 0u;
    ConstantPowerFactorType->PowerFactorExcitation_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_FrequencyDroopType(struct iso20_ac_der_sae_FrequencyDroopType* FrequencyDroopType) {
    FrequencyDroopType->Priority_isUsed = 0u;
    FrequencyDroopType->OverFrequencyDroop_isUsed = 0u;
    FrequencyDroopType->UnderFrequencyDroop_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignatureValueType(struct iso20_ac_der_sae_SignatureValueType* SignatureValueType) {
    SignatureValueType->Id_isUsed = 0u;
}

void init_iso20_ac_der_sae_VoltVarType(struct iso20_ac_der_sae_VoltVarType* VoltVarType) {
    VoltVarType->Priority_isUsed = 0u;
    VoltVarType->CurveDataPoints_L2_isUsed = 0u;
    VoltVarType->CurveDataPoints_L3_isUsed = 0u;
    VoltVarType->TimeConstantPT1_isUsed = 0u;
}

void init_iso20_ac_der_sae_VoltWattType(struct iso20_ac_der_sae_VoltWattType* VoltWattType) {
    VoltWattType->Priority_isUsed = 0u;
    VoltWattType->CurveDataPoints_L2_isUsed = 0u;
    VoltWattType->CurveDataPoints_L3_isUsed = 0u;
    VoltWattType->TimeConstantPT1_isUsed = 0u;
}

void init_iso20_ac_der_sae_KeyInfoType(struct iso20_ac_der_sae_KeyInfoType* KeyInfoType) {
    KeyInfoType->Id_isUsed = 0u;
    KeyInfoType->KeyName_isUsed = 0u;
    KeyInfoType->KeyValue_isUsed = 0u;
    KeyInfoType->RetrievalMethod_isUsed = 0u;
    KeyInfoType->X509Data_isUsed = 0u;
    KeyInfoType->PGPData_isUsed = 0u;
    KeyInfoType->SPKIData_isUsed = 0u;
    KeyInfoType->MgmtData_isUsed = 0u;
    KeyInfoType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_WattVarType(struct iso20_ac_der_sae_WattVarType* WattVarType) {
    WattVarType->Priority_isUsed = 0u;
    WattVarType->CurveDataPoints_L2_isUsed = 0u;
    WattVarType->CurveDataPoints_L3_isUsed = 0u;
    WattVarType->OpenLoopResponseTime_isUsed = 0u;
    WattVarType->TimeConstantPT1_isUsed = 0u;
}

void init_iso20_ac_der_sae_ConstantWattType(struct iso20_ac_der_sae_ConstantWattType* ConstantWattType) {
    ConstantWattType->Priority_isUsed = 0u;
    ConstantWattType->WattSetpoint_L2_isUsed = 0u;
    ConstantWattType->WattSetpoint_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_ObjectType(struct iso20_ac_der_sae_ObjectType* ObjectType) {
    ObjectType->Encoding_isUsed = 0u;
    ObjectType->Id_isUsed = 0u;
    ObjectType->MimeType_isUsed = 0u;
    ObjectType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_ConstantVarType(struct iso20_ac_der_sae_ConstantVarType* ConstantVarType) {
    ConstantVarType->Priority_isUsed = 0u;
    ConstantVarType->VarSetpoint_L2_isUsed = 0u;
    ConstantVarType->VarSetpoint_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_LimitMaxDischargePowerType(struct iso20_ac_der_sae_LimitMaxDischargePowerType* LimitMaxDischargePowerType) {
    LimitMaxDischargePowerType->Priority_isUsed = 0u;
    LimitMaxDischargePowerType->PercentageValue_L2_isUsed = 0u;
    LimitMaxDischargePowerType->PercentageValue_L3_isUsed = 0u;
    LimitMaxDischargePowerType->OpenLoopResponseTime_isUsed = 0u;
}

void init_iso20_ac_der_sae_RationalNumberType(struct iso20_ac_der_sae_RationalNumberType* RationalNumberType) {
    (void) RationalNumberType;
}

void init_iso20_ac_der_sae_VoltageTripType(struct iso20_ac_der_sae_VoltageTripType* VoltageTripType) {
    VoltageTripType->OverVoltageMomentaryCessationTripCurve_isUsed = 0u;
    VoltageTripType->UnderVoltageMomentaryCessationTripCurve_isUsed = 0u;
    VoltageTripType->OverVoltageMayTripCurve_isUsed = 0u;
    VoltageTripType->UnderVoltageMayTripCurve_isUsed = 0u;
}

void init_iso20_ac_der_sae_DetailedCostType(struct iso20_ac_der_sae_DetailedCostType* DetailedCostType) {
    (void) DetailedCostType;
}

void init_iso20_ac_der_sae_FrequencyTripType(struct iso20_ac_der_sae_FrequencyTripType* FrequencyTripType) {
    FrequencyTripType->OverFrequencyMayTripCurve_isUsed = 0u;
    FrequencyTripType->UnderFrequencyMayTripCurve_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignatureType(struct iso20_ac_der_sae_SignatureType* SignatureType) {
    SignatureType->Id_isUsed = 0u;
    SignatureType->KeyInfo_isUsed = 0u;
    SignatureType->Object_isUsed = 0u;
}

void init_iso20_ac_der_sae_EnterServiceCPDResType(struct iso20_ac_der_sae_EnterServiceCPDResType* EnterServiceCPDResType) {
    EnterServiceCPDResType->EnterServiceDelay_isUsed = 0u;
    EnterServiceCPDResType->EnterServiceRandomizedDelay_isUsed = 0u;
    EnterServiceCPDResType->EnterServiceRampTime_isUsed = 0u;
}

void init_iso20_ac_der_sae_EnterServiceCLResType(struct iso20_ac_der_sae_EnterServiceCLResType* EnterServiceCLResType) {
    EnterServiceCLResType->EnterServiceVoltageHigh_isUsed = 0u;
    EnterServiceCLResType->EnterServiceVoltageLow_isUsed = 0u;
    EnterServiceCLResType->EnterServiceFrequencyHigh_isUsed = 0u;
    EnterServiceCLResType->EnterServiceFrequencyLow_isUsed = 0u;
    EnterServiceCLResType->EnterServiceDelay_isUsed = 0u;
    EnterServiceCLResType->EnterServiceRandomizedDelay_isUsed = 0u;
    EnterServiceCLResType->EnterServiceRampTime_isUsed = 0u;
}

void init_iso20_ac_der_sae_ReactivePowerSupportCPDResType(struct iso20_ac_der_sae_ReactivePowerSupportCPDResType* ReactivePowerSupportCPDResType) {
    (void) ReactivePowerSupportCPDResType;
}

void init_iso20_ac_der_sae_ReactivePowerSupportCLResType(struct iso20_ac_der_sae_ReactivePowerSupportCLResType* ReactivePowerSupportCLResType) {
    ReactivePowerSupportCLResType->ConstantPowerFactor_isUsed = 0u;
    ReactivePowerSupportCLResType->VoltVar_isUsed = 0u;
    ReactivePowerSupportCLResType->WattVar_isUsed = 0u;
    ReactivePowerSupportCLResType->ConstantVar_isUsed = 0u;
}

void init_iso20_ac_der_sae_ActivePowerSupportCPDResType(struct iso20_ac_der_sae_ActivePowerSupportCPDResType* ActivePowerSupportCPDResType) {
    (void) ActivePowerSupportCPDResType;
}

void init_iso20_ac_der_sae_ActivePowerSupportCLResType(struct iso20_ac_der_sae_ActivePowerSupportCLResType* ActivePowerSupportCLResType) {
    ActivePowerSupportCLResType->FrequencyDroop_isUsed = 0u;
    ActivePowerSupportCLResType->VoltWatt_isUsed = 0u;
    ActivePowerSupportCLResType->ConstantWatt_isUsed = 0u;
    ActivePowerSupportCLResType->LimitMaxDischargePower_isUsed = 0u;
}

void init_iso20_ac_der_sae_DetailedTaxType(struct iso20_ac_der_sae_DetailedTaxType* DetailedTaxType) {
    (void) DetailedTaxType;
}

void init_iso20_ac_der_sae_MessageHeaderType(struct iso20_ac_der_sae_MessageHeaderType* MessageHeaderType) {
    MessageHeaderType->Signature_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignaturePropertyType(struct iso20_ac_der_sae_SignaturePropertyType* SignaturePropertyType) {
    SignaturePropertyType->Id_isUsed = 0u;
    SignaturePropertyType->ANY_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType* AC_CPDReqEnergyTransferModeType) {
    AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L2_isUsed = 0u;
    AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L3_isUsed = 0u;
    AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L2_isUsed = 0u;
    AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_DisplayParametersType(struct iso20_ac_der_sae_DisplayParametersType* DisplayParametersType) {
    DisplayParametersType->PresentSOC_isUsed = 0u;
    DisplayParametersType->MinimumSOC_isUsed = 0u;
    DisplayParametersType->TargetSOC_isUsed = 0u;
    DisplayParametersType->MaximumSOC_isUsed = 0u;
    DisplayParametersType->RemainingTimeToMinimumSOC_isUsed = 0u;
    DisplayParametersType->RemainingTimeToTargetSOC_isUsed = 0u;
    DisplayParametersType->RemainingTimeToMaximumSOC_isUsed = 0u;
    DisplayParametersType->ChargingComplete_isUsed = 0u;
    DisplayParametersType->BatteryEnergyCapacity_isUsed = 0u;
    DisplayParametersType->InletHot_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_AC_CPDResEnergyTransferModeType* AC_CPDResEnergyTransferModeType) {
    AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L2_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L3_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L2_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L3_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->MaximumPowerAsymmetry_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEPowerRampLimitation_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVSEStatusType(struct iso20_ac_der_sae_EVSEStatusType* EVSEStatusType) {
    (void) EVSEStatusType;
}

void init_iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType* Scheduled_AC_CLReqControlModeType) {
    Scheduled_AC_CLReqControlModeType->EVTargetEnergyRequest_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMaximumEnergyRequest_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMinimumEnergyRequest_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType* Dynamic_AC_CLReqControlModeType) {
    Dynamic_AC_CLReqControlModeType->DepartureTime_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_CLReqControlModeType(struct iso20_ac_der_sae_CLReqControlModeType* CLReqControlModeType) {
    (void) CLReqControlModeType;
}

void init_iso20_ac_der_sae_MeterInfoType(struct iso20_ac_der_sae_MeterInfoType* MeterInfoType) {
    MeterInfoType->BPT_DischargedEnergyReadingWh_isUsed = 0u;
    MeterInfoType->CapacitiveEnergyReadingVARh_isUsed = 0u;
    MeterInfoType->BPT_InductiveEnergyReadingVARh_isUsed = 0u;
    MeterInfoType->MeterSignature_isUsed = 0u;
    MeterInfoType->MeterStatus_isUsed = 0u;
    MeterInfoType->MeterTimestamp_isUsed = 0u;
}

void init_iso20_ac_der_sae_ReceiptType(struct iso20_ac_der_sae_ReceiptType* ReceiptType) {
    ReceiptType->TaxCosts.arrayLen = 0u;
    ReceiptType->EnergyCosts_isUsed = 0u;
    ReceiptType->OccupancyCosts_isUsed = 0u;
    ReceiptType->AdditionalServicesCosts_isUsed = 0u;
    ReceiptType->OverstayCosts_isUsed = 0u;
}

void init_iso20_ac_der_sae_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_Dynamic_AC_CLResControlModeType* Dynamic_AC_CLResControlModeType) {
    Dynamic_AC_CLResControlModeType->DepartureTime_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->MinimumSOC_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->TargetSOC_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->AckMaxDelay_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_Scheduled_AC_CLResControlModeType* Scheduled_AC_CLResControlModeType) {
    Scheduled_AC_CLResControlModeType->EVSETargetActivePower_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_CLResControlModeType(struct iso20_ac_der_sae_CLResControlModeType* CLResControlModeType) {
    (void) CLResControlModeType;
}

void init_iso20_ac_der_sae_DERControlCLResType(struct iso20_ac_der_sae_DERControlCLResType* DERControlCLResType) {
    DERControlCLResType->VoltageTrip_isUsed = 0u;
    DERControlCLResType->FrequencyTrip_isUsed = 0u;
    DERControlCLResType->ReactivePowerSupportCLRes_isUsed = 0u;
    DERControlCLResType->ActivePowerSupportCLRes_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVApparentPowerLimitsType(struct iso20_ac_der_sae_EVApparentPowerLimitsType* EVApparentPowerLimitsType) {
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringChargingAndVarInjection_L2_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringChargingAndVarInjection_L3_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringDischargingAndVarInjection_L2_isUsed = 0u;
    EVApparentPowerLimitsType->EVMaximumApparentPowerDuringDischargingAndVarInjection_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_DERControlCPDResType(struct iso20_ac_der_sae_DERControlCPDResType* DERControlCPDResType) {
    (void) DERControlCPDResType;
}

void init_iso20_ac_der_sae_EVReactivePowerLimitsType(struct iso20_ac_der_sae_EVReactivePowerLimitsType* EVReactivePowerLimitsType) {
    EVReactivePowerLimitsType->EVMaximumVarAbsorptionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarAbsorptionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringCharging_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarInjectionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarInjectionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringCharging_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarAbsorptionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarAbsorptionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringDischarging_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarAbsorptionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarInjectionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMaximumVarInjectionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringDischarging_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVMinimumVarInjectionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerLimitsType->EVReactiveSusceptance_L2_isUsed = 0u;
    EVReactivePowerLimitsType->EVReactiveSusceptance_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVExcitationLimitsType(struct iso20_ac_der_sae_EVExcitationLimitsType* EVExcitationLimitsType) {
    EVExcitationLimitsType->EVSpecifiedOverExcitedPowerFactor_L2_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedOverExcitedPowerFactor_L3_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedOverExcitedDischargePower_L2_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedOverExcitedDischargePower_L3_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedUnderExcitedPowerFactor_L2_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedUnderExcitedPowerFactor_L3_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedUnderExcitedDischargePower_L2_isUsed = 0u;
    EVExcitationLimitsType->EVSpecifiedUnderExcitedDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVInverterDetailsType(struct iso20_ac_der_sae_EVInverterDetailsType* EVInverterDetailsType) {
    EVInverterDetailsType->EVInverterHwVersion_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVSEReactivePowerLimitsType(struct iso20_ac_der_sae_EVSEReactivePowerLimitsType* EVSEReactivePowerLimitsType) {
    EVSEReactivePowerLimitsType->EVSEMaximumVarAbsorptionDuringCharging_L2_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarAbsorptionDuringCharging_L3_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarInjectionDuringCharging_L2_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarInjectionDuringCharging_L3_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarAbsorptionDuringDischarging_L2_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarAbsorptionDuringDischarging_L3_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarInjectionDuringDischarging_L2_isUsed = 0u;
    EVSEReactivePowerLimitsType->EVSEMaximumVarInjectionDuringDischarging_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_GridLimitsType(struct iso20_ac_der_sae_GridLimitsType* GridLimitsType) {
    GridLimitsType->GridMinFrequency_isUsed = 0u;
    GridLimitsType->GridMaxFrequency_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVApparentPowerType(struct iso20_ac_der_sae_EVApparentPowerType* EVApparentPowerType) {
    EVApparentPowerType->EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringChargingAndVarInjection_L2_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringChargingAndVarInjection_L3_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringDischargingAndVarInjection_L2_isUsed = 0u;
    EVApparentPowerType->EVMaximumApparentPowerDuringDischargingAndVarInjection_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVReactivePowerType(struct iso20_ac_der_sae_EVReactivePowerType* EVReactivePowerType) {
    EVReactivePowerType->EVMaximumVarAbsorptionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarAbsorptionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringCharging_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarInjectionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarInjectionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringCharging_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringCharging_L2_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringCharging_L3_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarAbsorptionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarAbsorptionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringDischarging_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarAbsorptionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarInjectionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerType->EVMaximumVarInjectionDuringDischarging_L3_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringDischarging_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringDischarging_L2_isUsed = 0u;
    EVReactivePowerType->EVMinimumVarInjectionDuringDischarging_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_EVExcitationType(struct iso20_ac_der_sae_EVExcitationType* EVExcitationType) {
    EVExcitationType->EVSpecifiedOverExcitedPowerFactor_isUsed = 0u;
    EVExcitationType->EVSpecifiedOverExcitedPowerFactor_L2_isUsed = 0u;
    EVExcitationType->EVSpecifiedOverExcitedPowerFactor_L3_isUsed = 0u;
    EVExcitationType->EVSpecifiedOverExcitedDischargePower_L2_isUsed = 0u;
    EVExcitationType->EVSpecifiedOverExcitedDischargePower_L3_isUsed = 0u;
    EVExcitationType->EVSpecifiedUnderExcitedPowerFactor_isUsed = 0u;
    EVExcitationType->EVSpecifiedUnderExcitedPowerFactor_L2_isUsed = 0u;
    EVExcitationType->EVSpecifiedUnderExcitedPowerFactor_L3_isUsed = 0u;
    EVExcitationType->EVSpecifiedUnderExcitedDischargePower_L2_isUsed = 0u;
    EVExcitationType->EVSpecifiedUnderExcitedDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType(struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType* AC_ChargeParameterDiscoveryReqType) {
    AC_ChargeParameterDiscoveryReqType->AC_CPDReqEnergyTransferMode_isUsed = 0u;
    AC_ChargeParameterDiscoveryReqType->BPT_AC_CPDReqEnergyTransferMode_isUsed = 0u;
    AC_ChargeParameterDiscoveryReqType->DER_AC_CPDReqEnergyTransferMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType(struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType* AC_ChargeParameterDiscoveryResType) {
    AC_ChargeParameterDiscoveryResType->AC_CPDResEnergyTransferMode_isUsed = 0u;
    AC_ChargeParameterDiscoveryResType->BPT_AC_CPDResEnergyTransferMode_isUsed = 0u;
    AC_ChargeParameterDiscoveryResType->DER_AC_CPDResEnergyTransferMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_ChargeLoopReqType(struct iso20_ac_der_sae_AC_ChargeLoopReqType* AC_ChargeLoopReqType) {
    AC_ChargeLoopReqType->DisplayParameters_isUsed = 0u;
    AC_ChargeLoopReqType->BPT_Dynamic_AC_CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->BPT_Scheduled_AC_CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->DER_Dynamic_AC_CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->DER_Scheduled_AC_CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->Dynamic_AC_CLReqControlMode_isUsed = 0u;
    AC_ChargeLoopReqType->Scheduled_AC_CLReqControlMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_AC_ChargeLoopResType(struct iso20_ac_der_sae_AC_ChargeLoopResType* AC_ChargeLoopResType) {
    AC_ChargeLoopResType->EVSEStatus_isUsed = 0u;
    AC_ChargeLoopResType->MeterInfo_isUsed = 0u;
    AC_ChargeLoopResType->Receipt_isUsed = 0u;
    AC_ChargeLoopResType->EVSETargetFrequency_isUsed = 0u;
    AC_ChargeLoopResType->BPT_Dynamic_AC_CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->BPT_Scheduled_AC_CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->DER_Dynamic_AC_CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->DER_Scheduled_AC_CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->Dynamic_AC_CLResControlMode_isUsed = 0u;
    AC_ChargeLoopResType->Scheduled_AC_CLResControlMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType* BPT_AC_CPDReqEnergyTransferModeType) {
    BPT_AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L2_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L3_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L2_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L3_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    BPT_AC_CPDReqEnergyTransferModeType->EVMinimumDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType* BPT_AC_CPDResEnergyTransferModeType) {
    BPT_AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L2_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L3_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L2_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L3_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->MaximumPowerAsymmetry_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEPowerRampLimitation_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L3_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMaximumDischargePower_L2_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMaximumDischargePower_L3_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMinimumDischargePower_L2_isUsed = 0u;
    BPT_AC_CPDResEnergyTransferModeType->EVSEMinimumDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType* DER_AC_CPDReqEnergyTransferModeType) {
    DER_AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L2_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMaximumChargePower_L3_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L2_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMinimumChargePower_L3_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMinimumDischargePower_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVMinimumDischargePower_L3_isUsed = 0u;
    DER_AC_CPDReqEnergyTransferModeType->EVSessionTotalDischargeEnergyAvailable_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType* DER_AC_CPDResEnergyTransferModeType) {
    DER_AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEMaximumChargePower_L3_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEMinimumChargePower_L3_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->MaximumPowerAsymmetry_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEPowerRampLimitation_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEPresentActivePower_L3_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEStatus_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalChargePower_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalChargePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalChargePower_L3_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalDischargePower_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalDischargePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSENominalDischargePower_L3_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEMaximumDischargePower_L2_isUsed = 0u;
    DER_AC_CPDResEnergyTransferModeType->EVSEMaximumDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType* BPT_Scheduled_AC_CLReqControlModeType) {
    BPT_Scheduled_AC_CLReqControlModeType->EVTargetEnergyRequest_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumEnergyRequest_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumEnergyRequest_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType* BPT_Scheduled_AC_CLResControlModeType) {
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    BPT_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType* BPT_Dynamic_AC_CLReqControlModeType) {
    BPT_Dynamic_AC_CLReqControlModeType->DepartureTime_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMinimumDischargePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMaximumV2XEnergyRequest_isUsed = 0u;
    BPT_Dynamic_AC_CLReqControlModeType->EVMinimumV2XEnergyRequest_isUsed = 0u;
}

void init_iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType* BPT_Dynamic_AC_CLResControlModeType) {
    BPT_Dynamic_AC_CLResControlModeType->DepartureTime_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->MinimumSOC_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->TargetSOC_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->AckMaxDelay_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    BPT_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType* DER_Scheduled_AC_CLReqControlModeType) {
    DER_Scheduled_AC_CLReqControlModeType->EVTargetEnergyRequest_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumEnergyRequest_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumEnergyRequest_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumDischargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVApparentPower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVReactivePower_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVExcitation_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVMinimumChargingDuration_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVDurationMaximumChargeRate_isUsed = 0u;
    DER_Scheduled_AC_CLReqControlModeType->EVDurationMaximumDischargeRate_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType* DER_Scheduled_AC_CLResControlModeType) {
    DER_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumChargePower_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumChargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumChargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumDischargePower_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumDischargePower_L2_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->EVSEMaximumDischargePower_L3_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->RequiredDEROperatingMode_isUsed = 0u;
    DER_Scheduled_AC_CLResControlModeType->GridConnectionMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType* DER_Dynamic_AC_CLReqControlModeType) {
    DER_Dynamic_AC_CLReqControlModeType->DepartureTime_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMaximumChargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMinimumChargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVPresentActivePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVPresentReactivePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMaximumDischargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMaximumDischargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMinimumDischargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMinimumDischargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVSessionTotalDischargeEnergyAvailable_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVApparentPower_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVReactivePower_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVExcitation_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMaximumV2XEnergyRequest_isUsed = 0u;
    DER_Dynamic_AC_CLReqControlModeType->EVMinimumV2XEnergyRequest_isUsed = 0u;
}

void init_iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType* DER_Dynamic_AC_CLResControlModeType) {
    DER_Dynamic_AC_CLResControlModeType->DepartureTime_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->MinimumSOC_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->TargetSOC_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->AckMaxDelay_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSETargetActivePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSETargetReactivePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEPresentActivePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumChargePower_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumChargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumChargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumDischargePower_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumDischargePower_L2_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->EVSEMaximumDischargePower_L3_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->RequiredDEROperatingMode_isUsed = 0u;
    DER_Dynamic_AC_CLResControlModeType->GridConnectionMode_isUsed = 0u;
}

void init_iso20_ac_der_sae_ManifestType(struct iso20_ac_der_sae_ManifestType* ManifestType) {
    ManifestType->Reference.arrayLen = 0u;
    ManifestType->Id_isUsed = 0u;
}

void init_iso20_ac_der_sae_SignaturePropertiesType(struct iso20_ac_der_sae_SignaturePropertiesType* SignaturePropertiesType) {
    SignaturePropertiesType->Id_isUsed = 0u;
}

// init for fragment
void init_iso20_ac_der_sae_exiFragment(struct iso20_ac_der_sae_exiFragment* exiFrag) {
    exiFrag->SignedInfo_isUsed = 0u;
}

// init for xmldsig fragment
void init_iso20_ac_der_sae_xmldsigFragment(struct iso20_ac_der_sae_xmldsigFragment* xmldsigFrag) {
    xmldsigFrag->CanonicalizationMethod_isUsed = 0u;
    xmldsigFrag->DSAKeyValue_isUsed = 0u;
    xmldsigFrag->DigestMethod_isUsed = 0u;
    xmldsigFrag->KeyInfo_isUsed = 0u;
    xmldsigFrag->KeyValue_isUsed = 0u;
    xmldsigFrag->Manifest_isUsed = 0u;
    xmldsigFrag->Object_isUsed = 0u;
    xmldsigFrag->PGPData_isUsed = 0u;
    xmldsigFrag->RSAKeyValue_isUsed = 0u;
    xmldsigFrag->Reference_isUsed = 0u;
    xmldsigFrag->RetrievalMethod_isUsed = 0u;
    xmldsigFrag->SPKIData_isUsed = 0u;
    xmldsigFrag->Signature_isUsed = 0u;
    xmldsigFrag->SignatureMethod_isUsed = 0u;
    xmldsigFrag->SignatureProperties_isUsed = 0u;
    xmldsigFrag->SignatureProperty_isUsed = 0u;
    xmldsigFrag->SignatureValue_isUsed = 0u;
    xmldsigFrag->SignedInfo_isUsed = 0u;
    xmldsigFrag->Transform_isUsed = 0u;
    xmldsigFrag->Transforms_isUsed = 0u;
    xmldsigFrag->X509Data_isUsed = 0u;
    xmldsigFrag->X509IssuerSerial_isUsed = 0u;
}


