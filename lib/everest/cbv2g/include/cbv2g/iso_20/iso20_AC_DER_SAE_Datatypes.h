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
  * @file iso20_AC_DER_SAE_Datatypes.h
  * @brief Description goes here
  *
  **/

#ifndef ISO20_AC_DER_SAE_DATATYPES_H
#define ISO20_AC_DER_SAE_DATATYPES_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stdint.h>

#include "cbv2g/common/exi_basetypes.h"



#define iso20_ac_der_sae_Algorithm_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_anyType_BYTES_SIZE (4)
#define iso20_ac_der_sae_XPath_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_CryptoBinary_BYTES_SIZE (EXI_BYTE_ARRAY_MAX_LEN)
#define iso20_ac_der_sae_X509IssuerName_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_Id_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_Type_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_URI_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_DigestValueType_BYTES_SIZE (EXI_BYTE_ARRAY_MAX_LEN)
#define iso20_ac_der_sae_base64Binary_BYTES_SIZE (EXI_BYTE_ARRAY_MAX_LEN)
#define iso20_ac_der_sae_X509SubjectName_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_ReferenceType_4_ARRAY_SIZE (4)
#define iso20_ac_der_sae_SignatureValueType_BYTES_SIZE (EXI_BYTE_ARRAY_MAX_LEN)
#define iso20_ac_der_sae_KeyName_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_MgmtData_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_Encoding_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_MimeType_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_DataTupleType_10_ARRAY_SIZE (10)
#define iso20_ac_der_sae_sessionIDType_BYTES_SIZE (8)
#define iso20_ac_der_sae_Target_CHARACTER_SIZE (EXI_STRING_MAX_LEN + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_MeterID_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_meterSignatureType_BYTES_SIZE (64)
#define iso20_ac_der_sae_DetailedTaxType_10_ARRAY_SIZE (10)
#define iso20_ac_der_sae_EVInverterSwVersion_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_EVInverterHwVersion_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_EVInverterManufacturer_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_EVInverterModel_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)
#define iso20_ac_der_sae_EVInverterSerialNumber_CHARACTER_SIZE (32 + ASCII_EXTRA_CHAR)


// enum for function numbers
typedef enum {
    iso20_ac_der_sae_AC_CPDReqEnergyTransferMode = 0,
    iso20_ac_der_sae_AC_CPDResEnergyTransferMode = 1,
    iso20_ac_der_sae_AC_ChargeLoopReq = 2,
    iso20_ac_der_sae_AC_ChargeLoopRes = 3,
    iso20_ac_der_sae_AC_ChargeParameterDiscoveryReq = 4,
    iso20_ac_der_sae_AC_ChargeParameterDiscoveryRes = 5,
    iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferMode = 6,
    iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferMode = 7,
    iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlMode = 8,
    iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlMode = 9,
    iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlMode = 10,
    iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlMode = 11,
    iso20_ac_der_sae_CLReqControlMode = 12,
    iso20_ac_der_sae_CLResControlMode = 13,
    iso20_ac_der_sae_CanonicalizationMethod = 14,
    iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferMode = 15,
    iso20_ac_der_sae_DER_AC_CPDResEnergyTransferMode = 16,
    iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlMode = 17,
    iso20_ac_der_sae_DER_Dynamic_AC_CLResControlMode = 18,
    iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlMode = 19,
    iso20_ac_der_sae_DER_Scheduled_AC_CLResControlMode = 20,
    iso20_ac_der_sae_DSAKeyValue = 21,
    iso20_ac_der_sae_DigestMethod = 22,
    iso20_ac_der_sae_DigestValue = 23,
    iso20_ac_der_sae_Dynamic_AC_CLReqControlMode = 24,
    iso20_ac_der_sae_Dynamic_AC_CLResControlMode = 25,
    iso20_ac_der_sae_FrequencyDroop = 26,
    iso20_ac_der_sae_KeyInfo = 27,
    iso20_ac_der_sae_KeyName = 28,
    iso20_ac_der_sae_KeyValue = 29,
    iso20_ac_der_sae_Manifest = 30,
    iso20_ac_der_sae_MgmtData = 31,
    iso20_ac_der_sae_Object = 32,
    iso20_ac_der_sae_PGPData = 33,
    iso20_ac_der_sae_RSAKeyValue = 34,
    iso20_ac_der_sae_Reference = 35,
    iso20_ac_der_sae_RetrievalMethod = 36,
    iso20_ac_der_sae_SPKIData = 37,
    iso20_ac_der_sae_Scheduled_AC_CLReqControlMode = 38,
    iso20_ac_der_sae_Scheduled_AC_CLResControlMode = 39,
    iso20_ac_der_sae_Signature = 40,
    iso20_ac_der_sae_SignatureMethod = 41,
    iso20_ac_der_sae_SignatureProperties = 42,
    iso20_ac_der_sae_SignatureProperty = 43,
    iso20_ac_der_sae_SignatureValue = 44,
    iso20_ac_der_sae_SignedInfo = 45,
    iso20_ac_der_sae_Transform = 46,
    iso20_ac_der_sae_Transforms = 47,
    iso20_ac_der_sae_X509Data = 48
} iso20_ac_der_sae_generatedFunctionNumbersType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}PowerReference; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}powerReferenceType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_powerReferenceType_MaximumActivePower = 0,
    iso20_ac_der_sae_powerReferenceType_MomentaryPower = 1
} iso20_ac_der_sae_powerReferenceType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}xUnit; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}derUnitType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_derUnitType_V = 0,
    iso20_ac_der_sae_derUnitType_Hz = 1,
    iso20_ac_der_sae_derUnitType_W = 2,
    iso20_ac_der_sae_derUnitType_s = 3,
    iso20_ac_der_sae_derUnitType_var = 4,
    iso20_ac_der_sae_derUnitType_PercentageEVMaximumConfiguredActivePower = 5,
    iso20_ac_der_sae_derUnitType_PercentageEVMaximumConfiguredReactivePower = 6,
    iso20_ac_der_sae_derUnitType_PercentageEVMaximumConfiguredApparentPower = 7,
    iso20_ac_der_sae_derUnitType_PercentageEVMaximumAvailableActivePower = 8,
    iso20_ac_der_sae_derUnitType_PercentageEVMaximumAvailableReactivePower = 9,
    iso20_ac_der_sae_derUnitType_PercentageV = 10
} iso20_ac_der_sae_derUnitType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}PowerFactorExcitation; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}powerFactorExcitationType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_powerFactorExcitationType_OverExcited = 0,
    iso20_ac_der_sae_powerFactorExcitationType_UnderExcited = 1
} iso20_ac_der_sae_powerFactorExcitationType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:CommonTypes}EVSENotification; type={urn:iso:std:iso:15118:-20:CommonTypes}evseNotificationType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_evseNotificationType_Pause = 0,
    iso20_ac_der_sae_evseNotificationType_ExitStandby = 1,
    iso20_ac_der_sae_evseNotificationType_Terminate = 2,
    iso20_ac_der_sae_evseNotificationType_ScheduleRenegotiation = 3,
    iso20_ac_der_sae_evseNotificationType_ServiceRenegotiation = 4,
    iso20_ac_der_sae_evseNotificationType_MeteringConfirmation = 5
} iso20_ac_der_sae_evseNotificationType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:CommonTypes}ResponseCode; type={urn:iso:std:iso:15118:-20:CommonTypes}responseCodeType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_responseCodeType_OK = 0,
    iso20_ac_der_sae_responseCodeType_OK_CertificateExpiresSoon = 1,
    iso20_ac_der_sae_responseCodeType_OK_NewSessionEstablished = 2,
    iso20_ac_der_sae_responseCodeType_OK_OldSessionJoined = 3,
    iso20_ac_der_sae_responseCodeType_OK_PowerToleranceConfirmed = 4,
    iso20_ac_der_sae_responseCodeType_WARNING_AuthorizationSelectionInvalid = 5,
    iso20_ac_der_sae_responseCodeType_WARNING_CertificateExpired = 6,
    iso20_ac_der_sae_responseCodeType_WARNING_CertificateNotYetValid = 7,
    iso20_ac_der_sae_responseCodeType_WARNING_CertificateRevoked = 8,
    iso20_ac_der_sae_responseCodeType_WARNING_CertificateValidationError = 9,
    iso20_ac_der_sae_responseCodeType_WARNING_ChallengeInvalid = 10,
    iso20_ac_der_sae_responseCodeType_WARNING_EIMAuthorizationFailure = 11,
    iso20_ac_der_sae_responseCodeType_WARNING_eMSPUnknown = 12,
    iso20_ac_der_sae_responseCodeType_WARNING_EVPowerProfileViolation = 13,
    iso20_ac_der_sae_responseCodeType_WARNING_GeneralPnCAuthorizationError = 14,
    iso20_ac_der_sae_responseCodeType_WARNING_NoCertificateAvailable = 15,
    iso20_ac_der_sae_responseCodeType_WARNING_NoContractMatchingPCIDFound = 16,
    iso20_ac_der_sae_responseCodeType_WARNING_PowerToleranceNotConfirmed = 17,
    iso20_ac_der_sae_responseCodeType_WARNING_ScheduleRenegotiationFailed = 18,
    iso20_ac_der_sae_responseCodeType_WARNING_StandbyNotAllowed = 19,
    iso20_ac_der_sae_responseCodeType_WARNING_WPT = 20,
    iso20_ac_der_sae_responseCodeType_FAILED = 21,
    iso20_ac_der_sae_responseCodeType_FAILED_AssociationError = 22,
    iso20_ac_der_sae_responseCodeType_FAILED_ContactorError = 23,
    iso20_ac_der_sae_responseCodeType_FAILED_EVPowerProfileInvalid = 24,
    iso20_ac_der_sae_responseCodeType_FAILED_EVPowerProfileViolation = 25,
    iso20_ac_der_sae_responseCodeType_FAILED_MeteringSignatureNotValid = 26,
    iso20_ac_der_sae_responseCodeType_FAILED_NoEnergyTransferServiceSelected = 27,
    iso20_ac_der_sae_responseCodeType_FAILED_NoServiceRenegotiationSupported = 28,
    iso20_ac_der_sae_responseCodeType_FAILED_PauseNotAllowed = 29,
    iso20_ac_der_sae_responseCodeType_FAILED_PowerDeliveryNotApplied = 30,
    iso20_ac_der_sae_responseCodeType_FAILED_PowerToleranceNotConfirmed = 31,
    iso20_ac_der_sae_responseCodeType_FAILED_ScheduleRenegotiation = 32,
    iso20_ac_der_sae_responseCodeType_FAILED_ScheduleSelectionInvalid = 33,
    iso20_ac_der_sae_responseCodeType_FAILED_SequenceError = 34,
    iso20_ac_der_sae_responseCodeType_FAILED_ServiceIDInvalid = 35,
    iso20_ac_der_sae_responseCodeType_FAILED_ServiceSelectionInvalid = 36,
    iso20_ac_der_sae_responseCodeType_FAILED_SignatureError = 37,
    iso20_ac_der_sae_responseCodeType_FAILED_UnknownSession = 38,
    iso20_ac_der_sae_responseCodeType_FAILED_WrongChargeParameter = 39
} iso20_ac_der_sae_responseCodeType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVProcessing; type={urn:iso:std:iso:15118:-20:CommonTypes}processingType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_processingType_Finished = 0,
    iso20_ac_der_sae_processingType_Ongoing = 1,
    iso20_ac_der_sae_processingType_Ongoing_WaitingForCustomerInteraction = 2
} iso20_ac_der_sae_processingType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}IEEE1547NormalCategory; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ieee1547NormalCategoryType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_ieee1547NormalCategoryType_CategoryA = 0,
    iso20_ac_der_sae_ieee1547NormalCategoryType_CategoryB = 1
} iso20_ac_der_sae_ieee1547NormalCategoryType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}IEEE1547AbnormalCategory; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ieee1547AbnormalCategoryType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_ieee1547AbnormalCategoryType_CategoryI = 0,
    iso20_ac_der_sae_ieee1547AbnormalCategoryType_CategoryII = 1,
    iso20_ac_der_sae_ieee1547AbnormalCategoryType_CategoryIII = 2
} iso20_ac_der_sae_ieee1547AbnormalCategoryType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DEROperationalState; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}derOperationalStateType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_derOperationalStateType_On = 0,
    iso20_ac_der_sae_derOperationalStateType_Off = 1
} iso20_ac_der_sae_derOperationalStateType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERConnectionStatus; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}derConnectionStatusType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_derConnectionStatusType_Disconnected = 0,
    iso20_ac_der_sae_derConnectionStatusType_Connected = 1
} iso20_ac_der_sae_derConnectionStatusType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}RequiredDEROperatingMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}requiredDEROperatingModeType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_requiredDEROperatingModeType_GridFollowing = 0,
    iso20_ac_der_sae_requiredDEROperatingModeType_GridForming = 1
} iso20_ac_der_sae_requiredDEROperatingModeType;

// Element: definition=enum; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}GridConnectionMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}gridConnectionModeType; base type=string; content type=simple;
//          abstract=False; final=False; derivation=restriction;
typedef enum {
    iso20_ac_der_sae_gridConnectionModeType_GridConnected = 0,
    iso20_ac_der_sae_gridConnectionModeType_GridIslanded = 1
} iso20_ac_der_sae_gridConnectionModeType;

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Transform; type={http://www.w3.org/2000/09/xmldsig#}TransformType; base type=; content type=mixed;
//          abstract=False; final=False; choice=True;
// Particle: Algorithm, anyURI (1, 1); ANY, anyType (0, 1); XPath, string (0, 1);
struct iso20_ac_der_sae_TransformType {
    // Attribute: Algorithm, anyURI
    struct {
        char characters[iso20_ac_der_sae_Algorithm_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Algorithm;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;

    // XPath, string
    struct {
        char characters[iso20_ac_der_sae_XPath_CHARACTER_SIZE];
        uint16_t charactersLen;
    } XPath;
    unsigned int XPath_isUsed:1;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Transforms; type={http://www.w3.org/2000/09/xmldsig#}TransformsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Transform, TransformType (1, 1) (original max unbounded);
struct iso20_ac_der_sae_TransformsType {
    // Transform, TransformType
    struct iso20_ac_der_sae_TransformType Transform;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}DSAKeyValue; type={http://www.w3.org/2000/09/xmldsig#}DSAKeyValueType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: P, CryptoBinary (0, 1) (was 1, 1) (seq. ['P', 'Q']); Q, CryptoBinary (0, 1) (was 1, 1) (seq. ['P', 'Q']); G, CryptoBinary (0, 1); Y, CryptoBinary (1, 1); J, CryptoBinary (0, 1); Seed, CryptoBinary (0, 1) (was 1, 1) (seq. ['Seed', 'PgenCounter']); PgenCounter, CryptoBinary (0, 1) (was 1, 1) (seq. ['Seed', 'PgenCounter']);
struct iso20_ac_der_sae_DSAKeyValueType {
    // P, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } P;
    unsigned int P_isUsed:1;

    // Q, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } Q;
    unsigned int Q_isUsed:1;

    // G, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } G;
    unsigned int G_isUsed:1;

    // Y, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } Y;

    // J, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } J;
    unsigned int J_isUsed:1;

    // Seed, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } Seed;
    unsigned int Seed_isUsed:1;

    // PgenCounter, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } PgenCounter;
    unsigned int PgenCounter_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}X509IssuerSerial; type={http://www.w3.org/2000/09/xmldsig#}X509IssuerSerialType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: X509IssuerName, string (1, 1); X509SerialNumber, integer (1, 1);
struct iso20_ac_der_sae_X509IssuerSerialType {
    // X509IssuerName, string
    struct {
        char characters[iso20_ac_der_sae_X509IssuerName_CHARACTER_SIZE];
        uint16_t charactersLen;
    } X509IssuerName;
    // X509SerialNumber, integer (base: decimal)
    exi_signed_t X509SerialNumber;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}DigestMethod; type={http://www.w3.org/2000/09/xmldsig#}DigestMethodType; base type=; content type=mixed;
//          abstract=False; final=False;
// Particle: Algorithm, anyURI (1, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_DigestMethodType {
    // Attribute: Algorithm, anyURI
    struct {
        char characters[iso20_ac_der_sae_Algorithm_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Algorithm;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}RSAKeyValue; type={http://www.w3.org/2000/09/xmldsig#}RSAKeyValueType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Modulus, CryptoBinary (1, 1); Exponent, CryptoBinary (1, 1);
struct iso20_ac_der_sae_RSAKeyValueType {
    // Modulus, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } Modulus;

    // Exponent, CryptoBinary (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_CryptoBinary_BYTES_SIZE];
        uint16_t bytesLen;
    } Exponent;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}CanonicalizationMethod; type={http://www.w3.org/2000/09/xmldsig#}CanonicalizationMethodType; base type=; content type=mixed;
//          abstract=False; final=False;
// Particle: Algorithm, anyURI (1, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_CanonicalizationMethodType {
    // Attribute: Algorithm, anyURI
    struct {
        char characters[iso20_ac_der_sae_Algorithm_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Algorithm;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SignatureMethod; type={http://www.w3.org/2000/09/xmldsig#}SignatureMethodType; base type=; content type=mixed;
//          abstract=False; final=False;
// Particle: Algorithm, anyURI (1, 1); HMACOutputLength, HMACOutputLengthType (0, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_SignatureMethodType {
    // Attribute: Algorithm, anyURI
    struct {
        char characters[iso20_ac_der_sae_Algorithm_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Algorithm;
    // HMACOutputLength, HMACOutputLengthType (base: integer)
    exi_signed_t HMACOutputLength;
    unsigned int HMACOutputLength_isUsed:1;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}KeyValue; type={http://www.w3.org/2000/09/xmldsig#}KeyValueType; base type=; content type=mixed;
//          abstract=False; final=False; choice=True;
// Particle: DSAKeyValue, DSAKeyValueType (0, 1); RSAKeyValue, RSAKeyValueType (0, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_KeyValueType {
    // DSAKeyValue, DSAKeyValueType
    struct iso20_ac_der_sae_DSAKeyValueType DSAKeyValue;
    unsigned int DSAKeyValue_isUsed:1;
    // RSAKeyValue, RSAKeyValueType
    struct iso20_ac_der_sae_RSAKeyValueType RSAKeyValue;
    unsigned int RSAKeyValue_isUsed:1;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Reference; type={http://www.w3.org/2000/09/xmldsig#}ReferenceType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Id, ID (0, 1); Type, anyURI (0, 1); URI, anyURI (0, 1); Transforms, TransformsType (0, 1); DigestMethod, DigestMethodType (1, 1); DigestValue, DigestValueType (1, 1);
struct iso20_ac_der_sae_ReferenceType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // Attribute: Type, anyURI
    struct {
        char characters[iso20_ac_der_sae_Type_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Type;
    unsigned int Type_isUsed:1;
    // Attribute: URI, anyURI
    struct {
        char characters[iso20_ac_der_sae_URI_CHARACTER_SIZE];
        uint16_t charactersLen;
    } URI;
    unsigned int URI_isUsed:1;
    // Transforms, TransformsType
    struct iso20_ac_der_sae_TransformsType Transforms;
    unsigned int Transforms_isUsed:1;
    // DigestMethod, DigestMethodType
    struct iso20_ac_der_sae_DigestMethodType DigestMethod;
    // DigestValue, DigestValueType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_DigestValueType_BYTES_SIZE];
        uint16_t bytesLen;
    } DigestValue;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}RetrievalMethod; type={http://www.w3.org/2000/09/xmldsig#}RetrievalMethodType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Type, anyURI (0, 1); URI, anyURI (0, 1); Transforms, TransformsType (0, 1);
struct iso20_ac_der_sae_RetrievalMethodType {
    // Attribute: Type, anyURI
    struct {
        char characters[iso20_ac_der_sae_Type_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Type;
    unsigned int Type_isUsed:1;
    // Attribute: URI, anyURI
    struct {
        char characters[iso20_ac_der_sae_URI_CHARACTER_SIZE];
        uint16_t charactersLen;
    } URI;
    unsigned int URI_isUsed:1;
    // Transforms, TransformsType
    struct iso20_ac_der_sae_TransformsType Transforms;
    unsigned int Transforms_isUsed:1;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}X509Data; type={http://www.w3.org/2000/09/xmldsig#}X509DataType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: X509IssuerSerial, X509IssuerSerialType (0, 1); X509SKI, base64Binary (0, 1); X509SubjectName, string (0, 1); X509Certificate, base64Binary (0, 1); X509CRL, base64Binary (0, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_X509DataType {
    // X509IssuerSerial, X509IssuerSerialType
    struct iso20_ac_der_sae_X509IssuerSerialType X509IssuerSerial;
    unsigned int X509IssuerSerial_isUsed:1;
    // X509SKI, base64Binary
    struct {
        uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
        uint16_t bytesLen;
    } X509SKI;
    unsigned int X509SKI_isUsed:1;

    // X509SubjectName, string
    struct {
        char characters[iso20_ac_der_sae_X509SubjectName_CHARACTER_SIZE];
        uint16_t charactersLen;
    } X509SubjectName;
    unsigned int X509SubjectName_isUsed:1;
    // X509Certificate, base64Binary
    struct {
        uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
        uint16_t bytesLen;
    } X509Certificate;
    unsigned int X509Certificate_isUsed:1;

    // X509CRL, base64Binary
    struct {
        uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
        uint16_t bytesLen;
    } X509CRL;
    unsigned int X509CRL_isUsed:1;

    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}PGPData; type={http://www.w3.org/2000/09/xmldsig#}PGPDataType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False; choice=True; sequence=True (2;
// Particle: PGPKeyID, base64Binary (1, 1); PGPKeyPacket, base64Binary (0, 1); ANY, anyType (0, 1); PGPKeyPacket, base64Binary (1, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_PGPDataType {
    union {
        // sequence of choice 1
        struct {
            // PGPKeyID, base64Binary
            struct {
                uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
                uint16_t bytesLen;
            } PGPKeyID;

            // PGPKeyPacket, base64Binary
            struct {
                uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
                uint16_t bytesLen;
            } PGPKeyPacket;
            unsigned int PGPKeyPacket_isUsed:1;

            // ANY, anyType (base: base64Binary)
            struct {
                uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
                uint16_t bytesLen;
            } ANY;
            unsigned int ANY_isUsed:1;


        } choice_1;
        unsigned int choice_1_isUsed:1;

        // sequence of choice 2
        struct {
            // PGPKeyPacket, base64Binary
            struct {
                uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
                uint16_t bytesLen;
            } PGPKeyPacket;

            // ANY, anyType (base: base64Binary)
            struct {
                uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
                uint16_t bytesLen;
            } ANY;
            unsigned int ANY_isUsed:1;


        } choice_2;
        unsigned int choice_2_isUsed:1;


    };
};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SPKIData; type={http://www.w3.org/2000/09/xmldsig#}SPKIDataType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: SPKISexp, base64Binary (1, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_SPKIDataType {
    // SPKISexp, base64Binary
    struct {
        uint8_t bytes[iso20_ac_der_sae_base64Binary_BYTES_SIZE];
        uint16_t bytesLen;
    } SPKISexp;

    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SignedInfo; type={http://www.w3.org/2000/09/xmldsig#}SignedInfoType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Id, ID (0, 1); CanonicalizationMethod, CanonicalizationMethodType (1, 1); SignatureMethod, SignatureMethodType (1, 1); Reference, ReferenceType (1, 4) (original max unbounded);
struct iso20_ac_der_sae_SignedInfoType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // CanonicalizationMethod, CanonicalizationMethodType
    struct iso20_ac_der_sae_CanonicalizationMethodType CanonicalizationMethod;
    // SignatureMethod, SignatureMethodType
    struct iso20_ac_der_sae_SignatureMethodType SignatureMethod;
    // Reference, ReferenceType
    struct {
        struct iso20_ac_der_sae_ReferenceType array[iso20_ac_der_sae_ReferenceType_4_ARRAY_SIZE];
        uint16_t arrayLen;
    } Reference;
};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SignatureValue; type={http://www.w3.org/2000/09/xmldsig#}SignatureValueType; base type=base64Binary; content type=simple;
//          abstract=False; final=False; derivation=extension;
// Particle: Id, ID (0, 1); CONTENT, SignatureValueType (1, 1);
struct iso20_ac_der_sae_SignatureValueType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // CONTENT, SignatureValueType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_SignatureValueType_BYTES_SIZE];
        uint16_t bytesLen;
    } CONTENT;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}KeyInfo; type={http://www.w3.org/2000/09/xmldsig#}KeyInfoType; base type=; content type=mixed;
//          abstract=False; final=False; choice=True;
// Particle: Id, ID (0, 1); KeyName, string (0, 1); KeyValue, KeyValueType (0, 1); RetrievalMethod, RetrievalMethodType (0, 1); X509Data, X509DataType (0, 1); PGPData, PGPDataType (0, 1); SPKIData, SPKIDataType (0, 1); MgmtData, string (0, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_KeyInfoType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // KeyName, string
    struct {
        char characters[iso20_ac_der_sae_KeyName_CHARACTER_SIZE];
        uint16_t charactersLen;
    } KeyName;
    unsigned int KeyName_isUsed:1;
    // KeyValue, KeyValueType
    struct iso20_ac_der_sae_KeyValueType KeyValue;
    unsigned int KeyValue_isUsed:1;
    // RetrievalMethod, RetrievalMethodType
    struct iso20_ac_der_sae_RetrievalMethodType RetrievalMethod;
    unsigned int RetrievalMethod_isUsed:1;
    // X509Data, X509DataType
    struct iso20_ac_der_sae_X509DataType X509Data;
    unsigned int X509Data_isUsed:1;
    // PGPData, PGPDataType
    struct iso20_ac_der_sae_PGPDataType PGPData;
    unsigned int PGPData_isUsed:1;
    // SPKIData, SPKIDataType
    struct iso20_ac_der_sae_SPKIDataType SPKIData;
    unsigned int SPKIData_isUsed:1;
    // MgmtData, string
    struct {
        char characters[iso20_ac_der_sae_MgmtData_CHARACTER_SIZE];
        uint16_t charactersLen;
    } MgmtData;
    unsigned int MgmtData_isUsed:1;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Object; type={http://www.w3.org/2000/09/xmldsig#}ObjectType; base type=; content type=mixed;
//          abstract=False; final=False;
// Particle: Encoding, anyURI (0, 1); Id, ID (0, 1); MimeType, string (0, 1); ANY, anyType (0, 1) (old 1, 1);
struct iso20_ac_der_sae_ObjectType {
    // Attribute: Encoding, anyURI
    struct {
        char characters[iso20_ac_der_sae_Encoding_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Encoding;
    unsigned int Encoding_isUsed:1;
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // Attribute: MimeType, string
    struct {
        char characters[iso20_ac_der_sae_MimeType_CHARACTER_SIZE];
        uint16_t charactersLen;
    } MimeType;
    unsigned int MimeType_isUsed:1;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVMaximumChargePower; type={urn:iso:std:iso:15118:-20:CommonTypes}RationalNumberType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Exponent, byte (1, 1); Value, short (1, 1);
struct iso20_ac_der_sae_RationalNumberType {
    // Exponent, byte (base: short)
    int8_t Exponent;
    // Value, short (base: int)
    int16_t Value;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}CurveDataPoint; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DataTupleType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: xValue, RationalNumberType (1, 1); yValue, RationalNumberType (1, 1);
struct iso20_ac_der_sae_DataTupleType {
    // xValue, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType xValue;
    // yValue, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType yValue;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}OverFrequencyDroop; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}FrequencyDroopSettingsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: db, RationalNumberType (1, 1); DroopFactor, RationalNumberType (1, 1); DroopFactor_L2, RationalNumberType (0, 1); DroopFactor_L3, RationalNumberType (0, 1); PowerReference, powerReferenceType (1, 1); PowerReference_L2, powerReferenceType (0, 1); PowerReference_L3, powerReferenceType (0, 1); OpenLoopResponseTime, RationalNumberType (1, 1);
struct iso20_ac_der_sae_FrequencyDroopSettingsType {
    // db, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType db;
    // DroopFactor, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType DroopFactor;
    // DroopFactor_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType DroopFactor_L2;
    unsigned int DroopFactor_L2_isUsed:1;
    // DroopFactor_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType DroopFactor_L3;
    unsigned int DroopFactor_L3_isUsed:1;
    // PowerReference, powerReferenceType (base: string)
    iso20_ac_der_sae_powerReferenceType PowerReference;
    // PowerReference_L2, powerReferenceType (base: string)
    iso20_ac_der_sae_powerReferenceType PowerReference_L2;
    unsigned int PowerReference_L2_isUsed:1;
    // PowerReference_L3, powerReferenceType (base: string)
    iso20_ac_der_sae_powerReferenceType PowerReference_L3;
    unsigned int PowerReference_L3_isUsed:1;
    // OpenLoopResponseTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType OpenLoopResponseTime;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}CurveDataPoints; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}CurveDataPointsListType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: CurveDataPoint, DataTupleType (2, 10);
struct iso20_ac_der_sae_CurveDataPointsListType {
    // CurveDataPoint, DataTupleType
    struct {
        struct iso20_ac_der_sae_DataTupleType array[iso20_ac_der_sae_DataTupleType_10_ARRAY_SIZE];
        uint16_t arrayLen;
    } CurveDataPoint;
};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}OverVoltageMustTripCurve; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERCurveType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); xUnit, derUnitType (1, 1); yUnit, derUnitType (1, 1); CurveDataPoints, CurveDataPointsListType (1, 1); CurveDataPoints_L2, CurveDataPointsListType (0, 1); CurveDataPoints_L3, CurveDataPointsListType (0, 1);
struct iso20_ac_der_sae_DERCurveType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // xUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType xUnit;
    // yUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType yUnit;
    // CurveDataPoints, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints;
    // CurveDataPoints_L2, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L2;
    unsigned int CurveDataPoints_L2_isUsed:1;
    // CurveDataPoints_L3, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L3;
    unsigned int CurveDataPoints_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantPowerFactor; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantPowerFactorType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); PowerFactorValue, RationalNumberType (1, 1); PowerFactorValue_L2, RationalNumberType (0, 1); PowerFactorValue_L3, RationalNumberType (0, 1); PowerFactorExcitation, powerFactorExcitationType (1, 1); PowerFactorExcitation_L2, powerFactorExcitationType (0, 1); PowerFactorExcitation_L3, powerFactorExcitationType (0, 1);
struct iso20_ac_der_sae_ConstantPowerFactorType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // PowerFactorValue, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType PowerFactorValue;
    // PowerFactorValue_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType PowerFactorValue_L2;
    unsigned int PowerFactorValue_L2_isUsed:1;
    // PowerFactorValue_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType PowerFactorValue_L3;
    unsigned int PowerFactorValue_L3_isUsed:1;
    // PowerFactorExcitation, powerFactorExcitationType (base: string)
    iso20_ac_der_sae_powerFactorExcitationType PowerFactorExcitation;
    // PowerFactorExcitation_L2, powerFactorExcitationType (base: string)
    iso20_ac_der_sae_powerFactorExcitationType PowerFactorExcitation_L2;
    unsigned int PowerFactorExcitation_L2_isUsed:1;
    // PowerFactorExcitation_L3, powerFactorExcitationType (base: string)
    iso20_ac_der_sae_powerFactorExcitationType PowerFactorExcitation_L3;
    unsigned int PowerFactorExcitation_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}FrequencyDroop; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}FrequencyDroopType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); OverFrequencyDroop, FrequencyDroopSettingsType (0, 1); UnderFrequencyDroop, FrequencyDroopSettingsType (0, 1);
struct iso20_ac_der_sae_FrequencyDroopType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // OverFrequencyDroop, FrequencyDroopSettingsType
    struct iso20_ac_der_sae_FrequencyDroopSettingsType OverFrequencyDroop;
    unsigned int OverFrequencyDroop_isUsed:1;
    // UnderFrequencyDroop, FrequencyDroopSettingsType
    struct iso20_ac_der_sae_FrequencyDroopSettingsType UnderFrequencyDroop;
    unsigned int UnderFrequencyDroop_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltVar; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltVarType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); xUnit, derUnitType (1, 1); yUnit, derUnitType (1, 1); CurveDataPoints, CurveDataPointsListType (1, 1); CurveDataPoints_L2, CurveDataPointsListType (0, 1); CurveDataPoints_L3, CurveDataPointsListType (0, 1); OpenLoopResponseTime, RationalNumberType (1, 1); TimeConstantPT1, unsignedInt (0, 1); ReferenceVoltage, RationalNumberType (1, 1); AutonomousReferenceVoltageAdjustmentEnable, boolean (1, 1); ReferenceVoltageAdjustmentTimeConstant, unsignedInt (1, 1);
struct iso20_ac_der_sae_VoltVarType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // xUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType xUnit;
    // yUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType yUnit;
    // CurveDataPoints, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints;
    // CurveDataPoints_L2, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L2;
    unsigned int CurveDataPoints_L2_isUsed:1;
    // CurveDataPoints_L3, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L3;
    unsigned int CurveDataPoints_L3_isUsed:1;
    // OpenLoopResponseTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType OpenLoopResponseTime;
    // TimeConstantPT1, unsignedInt (base: unsignedLong)
    uint32_t TimeConstantPT1;
    unsigned int TimeConstantPT1_isUsed:1;
    // ReferenceVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType ReferenceVoltage;
    // AutonomousReferenceVoltageAdjustmentEnable, boolean
    int AutonomousReferenceVoltageAdjustmentEnable;
    // ReferenceVoltageAdjustmentTimeConstant, unsignedInt (base: unsignedLong)
    uint32_t ReferenceVoltageAdjustmentTimeConstant;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltWatt; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltWattType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); xUnit, derUnitType (1, 1); yUnit, derUnitType (1, 1); CurveDataPoints, CurveDataPointsListType (1, 1); CurveDataPoints_L2, CurveDataPointsListType (0, 1); CurveDataPoints_L3, CurveDataPointsListType (0, 1); OpenLoopResponseTime, RationalNumberType (1, 1); TimeConstantPT1, unsignedInt (0, 1);
struct iso20_ac_der_sae_VoltWattType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // xUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType xUnit;
    // yUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType yUnit;
    // CurveDataPoints, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints;
    // CurveDataPoints_L2, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L2;
    unsigned int CurveDataPoints_L2_isUsed:1;
    // CurveDataPoints_L3, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L3;
    unsigned int CurveDataPoints_L3_isUsed:1;
    // OpenLoopResponseTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType OpenLoopResponseTime;
    // TimeConstantPT1, unsignedInt (base: unsignedLong)
    uint32_t TimeConstantPT1;
    unsigned int TimeConstantPT1_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}WattVar; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}WattVarType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); xUnit, derUnitType (1, 1); yUnit, derUnitType (1, 1); CurveDataPoints, CurveDataPointsListType (1, 1); CurveDataPoints_L2, CurveDataPointsListType (0, 1); CurveDataPoints_L3, CurveDataPointsListType (0, 1); OpenLoopResponseTime, RationalNumberType (0, 1); TimeConstantPT1, unsignedInt (0, 1);
struct iso20_ac_der_sae_WattVarType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // xUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType xUnit;
    // yUnit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType yUnit;
    // CurveDataPoints, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints;
    // CurveDataPoints_L2, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L2;
    unsigned int CurveDataPoints_L2_isUsed:1;
    // CurveDataPoints_L3, CurveDataPointsListType
    struct iso20_ac_der_sae_CurveDataPointsListType CurveDataPoints_L3;
    unsigned int CurveDataPoints_L3_isUsed:1;
    // OpenLoopResponseTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType OpenLoopResponseTime;
    unsigned int OpenLoopResponseTime_isUsed:1;
    // TimeConstantPT1, unsignedInt (base: unsignedLong)
    uint32_t TimeConstantPT1;
    unsigned int TimeConstantPT1_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantWatt; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantWattType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); WattSetpoint, RationalNumberType (1, 1); WattSetpoint_L2, RationalNumberType (0, 1); WattSetpoint_L3, RationalNumberType (0, 1); Unit, derUnitType (1, 1);
struct iso20_ac_der_sae_ConstantWattType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // WattSetpoint, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType WattSetpoint;
    // WattSetpoint_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType WattSetpoint_L2;
    unsigned int WattSetpoint_L2_isUsed:1;
    // WattSetpoint_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType WattSetpoint_L3;
    unsigned int WattSetpoint_L3_isUsed:1;
    // Unit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType Unit;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantVar; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ConstantVarType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); VarSetpoint, RationalNumberType (1, 1); VarSetpoint_L2, RationalNumberType (0, 1); VarSetpoint_L3, RationalNumberType (0, 1); Unit, derUnitType (1, 1);
struct iso20_ac_der_sae_ConstantVarType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // VarSetpoint, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType VarSetpoint;
    // VarSetpoint_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType VarSetpoint_L2;
    unsigned int VarSetpoint_L2_isUsed:1;
    // VarSetpoint_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType VarSetpoint_L3;
    unsigned int VarSetpoint_L3_isUsed:1;
    // Unit, derUnitType (base: string)
    iso20_ac_der_sae_derUnitType Unit;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}LimitMaxDischargePower; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}LimitMaxDischargePowerType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Enable, boolean (1, 1); Priority, unsignedShort (0, 1); PercentageValue, unsignedShort (1, 1); PercentageValue_L2, unsignedShort (0, 1); PercentageValue_L3, unsignedShort (0, 1); OpenLoopResponseTime, RationalNumberType (0, 1);
struct iso20_ac_der_sae_LimitMaxDischargePowerType {
    // Enable, boolean
    int Enable;
    // Priority, unsignedShort (base: unsignedInt)
    uint16_t Priority;
    unsigned int Priority_isUsed:1;
    // PercentageValue, unsignedShort (base: unsignedInt)
    uint16_t PercentageValue;
    // PercentageValue_L2, unsignedShort (base: unsignedInt)
    uint16_t PercentageValue_L2;
    unsigned int PercentageValue_L2_isUsed:1;
    // PercentageValue_L3, unsignedShort (base: unsignedInt)
    uint16_t PercentageValue_L3;
    unsigned int PercentageValue_L3_isUsed:1;
    // OpenLoopResponseTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType OpenLoopResponseTime;
    unsigned int OpenLoopResponseTime_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltageTrip; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}VoltageTripType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: OverVoltageMustTripCurve, DERCurveType (1, 1); UnderVoltageMustTripCurve, DERCurveType (1, 1); OverVoltageMomentaryCessationTripCurve, DERCurveType (0, 1); UnderVoltageMomentaryCessationTripCurve, DERCurveType (0, 1); OverVoltageMayTripCurve, DERCurveType (0, 1); UnderVoltageMayTripCurve, DERCurveType (0, 1);
struct iso20_ac_der_sae_VoltageTripType {
    // OverVoltageMustTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType OverVoltageMustTripCurve;
    // UnderVoltageMustTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType UnderVoltageMustTripCurve;
    // OverVoltageMomentaryCessationTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType OverVoltageMomentaryCessationTripCurve;
    unsigned int OverVoltageMomentaryCessationTripCurve_isUsed:1;
    // UnderVoltageMomentaryCessationTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType UnderVoltageMomentaryCessationTripCurve;
    unsigned int UnderVoltageMomentaryCessationTripCurve_isUsed:1;
    // OverVoltageMayTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType OverVoltageMayTripCurve;
    unsigned int OverVoltageMayTripCurve_isUsed:1;
    // UnderVoltageMayTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType UnderVoltageMayTripCurve;
    unsigned int UnderVoltageMayTripCurve_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}EnergyCosts; type={urn:iso:std:iso:15118:-20:CommonTypes}DetailedCostType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Amount, RationalNumberType (1, 1); CostPerUnit, RationalNumberType (1, 1);
struct iso20_ac_der_sae_DetailedCostType {
    // Amount, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType Amount;
    // CostPerUnit, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType CostPerUnit;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}FrequencyTrip; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}FrequencyTripType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: OverFrequencyMustTripCurve, DERCurveType (1, 1); UnderFrequencyMustTripCurve, DERCurveType (1, 1); OverFrequencyMayTripCurve, DERCurveType (0, 1); UnderFrequencyMayTripCurve, DERCurveType (0, 1);
struct iso20_ac_der_sae_FrequencyTripType {
    // OverFrequencyMustTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType OverFrequencyMustTripCurve;
    // UnderFrequencyMustTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType UnderFrequencyMustTripCurve;
    // OverFrequencyMayTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType OverFrequencyMayTripCurve;
    unsigned int OverFrequencyMayTripCurve_isUsed:1;
    // UnderFrequencyMayTripCurve, DERCurveType
    struct iso20_ac_der_sae_DERCurveType UnderFrequencyMayTripCurve;
    unsigned int UnderFrequencyMayTripCurve_isUsed:1;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Signature; type={http://www.w3.org/2000/09/xmldsig#}SignatureType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Id, ID (0, 1); SignedInfo, SignedInfoType (1, 1); SignatureValue, SignatureValueType (1, 1); KeyInfo, KeyInfoType (0, 1); Object, ObjectType (0, 1) (original max unbounded);
struct iso20_ac_der_sae_SignatureType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // SignedInfo, SignedInfoType
    struct iso20_ac_der_sae_SignedInfoType SignedInfo;
    // SignatureValue, SignatureValueType (base: base64Binary)
    struct iso20_ac_der_sae_SignatureValueType SignatureValue;
    // KeyInfo, KeyInfoType
    struct iso20_ac_der_sae_KeyInfoType KeyInfo;
    unsigned int KeyInfo_isUsed:1;
    // Object, ObjectType
    struct iso20_ac_der_sae_ObjectType Object;
    unsigned int Object_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EnterServiceCPDRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EnterServiceCPDResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: PermitService, boolean (1, 1); EnterServiceVoltageHigh, RationalNumberType (1, 1); EnterServiceVoltageLow, RationalNumberType (1, 1); EnterServiceFrequencyHigh, RationalNumberType (1, 1); EnterServiceFrequencyLow, RationalNumberType (1, 1); EnterServiceDelay, RationalNumberType (0, 1); EnterServiceRandomizedDelay, RationalNumberType (0, 1); EnterServiceRampTime, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EnterServiceCPDResType {
    // PermitService, boolean
    int PermitService;
    // EnterServiceVoltageHigh, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceVoltageHigh;
    // EnterServiceVoltageLow, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceVoltageLow;
    // EnterServiceFrequencyHigh, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceFrequencyHigh;
    // EnterServiceFrequencyLow, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceFrequencyLow;
    // EnterServiceDelay, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceDelay;
    unsigned int EnterServiceDelay_isUsed:1;
    // EnterServiceRandomizedDelay, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceRandomizedDelay;
    unsigned int EnterServiceRandomizedDelay_isUsed:1;
    // EnterServiceRampTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceRampTime;
    unsigned int EnterServiceRampTime_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EnterServiceCLRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EnterServiceCLResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: PermitService, boolean (1, 1); EnterServiceVoltageHigh, RationalNumberType (0, 1); EnterServiceVoltageLow, RationalNumberType (0, 1); EnterServiceFrequencyHigh, RationalNumberType (0, 1); EnterServiceFrequencyLow, RationalNumberType (0, 1); EnterServiceDelay, RationalNumberType (0, 1); EnterServiceRandomizedDelay, RationalNumberType (0, 1); EnterServiceRampTime, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EnterServiceCLResType {
    // PermitService, boolean
    int PermitService;
    // EnterServiceVoltageHigh, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceVoltageHigh;
    unsigned int EnterServiceVoltageHigh_isUsed:1;
    // EnterServiceVoltageLow, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceVoltageLow;
    unsigned int EnterServiceVoltageLow_isUsed:1;
    // EnterServiceFrequencyHigh, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceFrequencyHigh;
    unsigned int EnterServiceFrequencyHigh_isUsed:1;
    // EnterServiceFrequencyLow, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceFrequencyLow;
    unsigned int EnterServiceFrequencyLow_isUsed:1;
    // EnterServiceDelay, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceDelay;
    unsigned int EnterServiceDelay_isUsed:1;
    // EnterServiceRandomizedDelay, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceRandomizedDelay;
    unsigned int EnterServiceRandomizedDelay_isUsed:1;
    // EnterServiceRampTime, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EnterServiceRampTime;
    unsigned int EnterServiceRampTime_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ReactivePowerSupportCPDRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ReactivePowerSupportCPDResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: ConstantPowerFactor, ConstantPowerFactorType (1, 1); VoltVar, VoltVarType (1, 1); WattVar, WattVarType (1, 1); ConstantVar, ConstantVarType (1, 1);
struct iso20_ac_der_sae_ReactivePowerSupportCPDResType {
    // ConstantPowerFactor, ConstantPowerFactorType
    struct iso20_ac_der_sae_ConstantPowerFactorType ConstantPowerFactor;
    // VoltVar, VoltVarType
    struct iso20_ac_der_sae_VoltVarType VoltVar;
    // WattVar, WattVarType
    struct iso20_ac_der_sae_WattVarType WattVar;
    // ConstantVar, ConstantVarType
    struct iso20_ac_der_sae_ConstantVarType ConstantVar;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ReactivePowerSupportCLRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ReactivePowerSupportCLResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: ConstantPowerFactor, ConstantPowerFactorType (0, 1); VoltVar, VoltVarType (0, 1); WattVar, WattVarType (0, 1); ConstantVar, ConstantVarType (0, 1);
struct iso20_ac_der_sae_ReactivePowerSupportCLResType {
    // ConstantPowerFactor, ConstantPowerFactorType
    struct iso20_ac_der_sae_ConstantPowerFactorType ConstantPowerFactor;
    unsigned int ConstantPowerFactor_isUsed:1;
    // VoltVar, VoltVarType
    struct iso20_ac_der_sae_VoltVarType VoltVar;
    unsigned int VoltVar_isUsed:1;
    // WattVar, WattVarType
    struct iso20_ac_der_sae_WattVarType WattVar;
    unsigned int WattVar_isUsed:1;
    // ConstantVar, ConstantVarType
    struct iso20_ac_der_sae_ConstantVarType ConstantVar;
    unsigned int ConstantVar_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ActivePowerSupportCPDRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ActivePowerSupportCPDResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: FrequencyDroop, FrequencyDroopType (1, 1); VoltWatt, VoltWattType (1, 1); ConstantWatt, ConstantWattType (1, 1); LimitMaxDischargePower, LimitMaxDischargePowerType (1, 1);
struct iso20_ac_der_sae_ActivePowerSupportCPDResType {
    // FrequencyDroop, FrequencyDroopType
    struct iso20_ac_der_sae_FrequencyDroopType FrequencyDroop;
    // VoltWatt, VoltWattType
    struct iso20_ac_der_sae_VoltWattType VoltWatt;
    // ConstantWatt, ConstantWattType
    struct iso20_ac_der_sae_ConstantWattType ConstantWatt;
    // LimitMaxDischargePower, LimitMaxDischargePowerType
    struct iso20_ac_der_sae_LimitMaxDischargePowerType LimitMaxDischargePower;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}ActivePowerSupportCLRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}ActivePowerSupportCLResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: FrequencyDroop, FrequencyDroopType (0, 1); VoltWatt, VoltWattType (0, 1); ConstantWatt, ConstantWattType (0, 1); LimitMaxDischargePower, LimitMaxDischargePowerType (0, 1);
struct iso20_ac_der_sae_ActivePowerSupportCLResType {
    // FrequencyDroop, FrequencyDroopType
    struct iso20_ac_der_sae_FrequencyDroopType FrequencyDroop;
    unsigned int FrequencyDroop_isUsed:1;
    // VoltWatt, VoltWattType
    struct iso20_ac_der_sae_VoltWattType VoltWatt;
    unsigned int VoltWatt_isUsed:1;
    // ConstantWatt, ConstantWattType
    struct iso20_ac_der_sae_ConstantWattType ConstantWatt;
    unsigned int ConstantWatt_isUsed:1;
    // LimitMaxDischargePower, LimitMaxDischargePowerType
    struct iso20_ac_der_sae_LimitMaxDischargePowerType LimitMaxDischargePower;
    unsigned int LimitMaxDischargePower_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}TaxCosts; type={urn:iso:std:iso:15118:-20:CommonTypes}DetailedTaxType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: TaxRuleID, numericIDType (1, 1); Amount, RationalNumberType (1, 1);
struct iso20_ac_der_sae_DetailedTaxType {
    // TaxRuleID, numericIDType (base: unsignedInt)
    uint32_t TaxRuleID;
    // Amount, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType Amount;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}Header; type={urn:iso:std:iso:15118:-20:CommonTypes}MessageHeaderType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: SessionID, sessionIDType (1, 1); TimeStamp, unsignedLong (1, 1); Signature, SignatureType (0, 1);
struct iso20_ac_der_sae_MessageHeaderType {
    // SessionID, sessionIDType (base: hexBinary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_sessionIDType_BYTES_SIZE];
        uint16_t bytesLen;
    } SessionID;

    // TimeStamp, unsignedLong (base: nonNegativeInteger)
    uint64_t TimeStamp;
    // Signature, SignatureType
    struct iso20_ac_der_sae_SignatureType Signature;
    unsigned int Signature_isUsed:1;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SignatureProperty; type={http://www.w3.org/2000/09/xmldsig#}SignaturePropertyType; base type=; content type=mixed;
//          abstract=False; final=False; choice=True;
// Particle: Id, ID (0, 1); Target, anyURI (1, 1); ANY, anyType (0, 1);
struct iso20_ac_der_sae_SignaturePropertyType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // Attribute: Target, anyURI
    struct {
        char characters[iso20_ac_der_sae_Target_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Target;
    // ANY, anyType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_anyType_BYTES_SIZE];
        uint16_t bytesLen;
    } ANY;
    unsigned int ANY_isUsed:1;


};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_CPDReqEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_CPDReqEnergyTransferModeType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType {
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}DisplayParameters; type={urn:iso:std:iso:15118:-20:CommonTypes}DisplayParametersType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: PresentSOC, percentValueType (0, 1); MinimumSOC, percentValueType (0, 1); TargetSOC, percentValueType (0, 1); MaximumSOC, percentValueType (0, 1); RemainingTimeToMinimumSOC, unsignedInt (0, 1); RemainingTimeToTargetSOC, unsignedInt (0, 1); RemainingTimeToMaximumSOC, unsignedInt (0, 1); ChargingComplete, boolean (0, 1); BatteryEnergyCapacity, RationalNumberType (0, 1); InletHot, boolean (0, 1);
struct iso20_ac_der_sae_DisplayParametersType {
    // PresentSOC, percentValueType (base: byte)
    int8_t PresentSOC;
    unsigned int PresentSOC_isUsed:1;
    // MinimumSOC, percentValueType (base: byte)
    int8_t MinimumSOC;
    unsigned int MinimumSOC_isUsed:1;
    // TargetSOC, percentValueType (base: byte)
    int8_t TargetSOC;
    unsigned int TargetSOC_isUsed:1;
    // MaximumSOC, percentValueType (base: byte)
    int8_t MaximumSOC;
    unsigned int MaximumSOC_isUsed:1;
    // RemainingTimeToMinimumSOC, unsignedInt (base: unsignedLong)
    uint32_t RemainingTimeToMinimumSOC;
    unsigned int RemainingTimeToMinimumSOC_isUsed:1;
    // RemainingTimeToTargetSOC, unsignedInt (base: unsignedLong)
    uint32_t RemainingTimeToTargetSOC;
    unsigned int RemainingTimeToTargetSOC_isUsed:1;
    // RemainingTimeToMaximumSOC, unsignedInt (base: unsignedLong)
    uint32_t RemainingTimeToMaximumSOC;
    unsigned int RemainingTimeToMaximumSOC_isUsed:1;
    // ChargingComplete, boolean
    int ChargingComplete;
    unsigned int ChargingComplete_isUsed:1;
    // BatteryEnergyCapacity, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType BatteryEnergyCapacity;
    unsigned int BatteryEnergyCapacity_isUsed:1;
    // InletHot, boolean
    int InletHot;
    unsigned int InletHot_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_CPDResEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_CPDResEnergyTransferModeType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVSEMaximumChargePower, RationalNumberType (1, 1); EVSEMaximumChargePower_L2, RationalNumberType (0, 1); EVSEMaximumChargePower_L3, RationalNumberType (0, 1); EVSEMinimumChargePower, RationalNumberType (1, 1); EVSEMinimumChargePower_L2, RationalNumberType (0, 1); EVSEMinimumChargePower_L3, RationalNumberType (0, 1); EVSENominalFrequency, RationalNumberType (1, 1); MaximumPowerAsymmetry, RationalNumberType (0, 1); EVSEPowerRampLimitation, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_AC_CPDResEnergyTransferModeType {
    // EVSEMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower;
    // EVSEMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L2;
    unsigned int EVSEMaximumChargePower_L2_isUsed:1;
    // EVSEMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L3;
    unsigned int EVSEMaximumChargePower_L3_isUsed:1;
    // EVSEMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower;
    // EVSEMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L2;
    unsigned int EVSEMinimumChargePower_L2_isUsed:1;
    // EVSEMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L3;
    unsigned int EVSEMinimumChargePower_L3_isUsed:1;
    // EVSENominalFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalFrequency;
    // MaximumPowerAsymmetry, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType MaximumPowerAsymmetry;
    unsigned int MaximumPowerAsymmetry_isUsed:1;
    // EVSEPowerRampLimitation, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPowerRampLimitation;
    unsigned int EVSEPowerRampLimitation_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}EVSEStatus; type={urn:iso:std:iso:15118:-20:CommonTypes}EVSEStatusType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: NotificationMaxDelay, unsignedShort (1, 1); EVSENotification, evseNotificationType (1, 1);
struct iso20_ac_der_sae_EVSEStatusType {
    // NotificationMaxDelay, unsignedShort (base: unsignedInt)
    uint16_t NotificationMaxDelay;
    // EVSENotification, evseNotificationType (base: string)
    iso20_ac_der_sae_evseNotificationType EVSENotification;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}Scheduled_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}Scheduled_AC_CLReqControlModeType; base type=Scheduled_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVTargetEnergyRequest, RationalNumberType (0, 1); EVMaximumEnergyRequest, RationalNumberType (0, 1); EVMinimumEnergyRequest, RationalNumberType (0, 1); EVMaximumChargePower, RationalNumberType (0, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (0, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (0, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType {
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    unsigned int EVTargetEnergyRequest_isUsed:1;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    unsigned int EVMaximumEnergyRequest_isUsed:1;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    unsigned int EVMinimumEnergyRequest_isUsed:1;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    unsigned int EVMaximumChargePower_isUsed:1;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    unsigned int EVMinimumChargePower_isUsed:1;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    unsigned int EVPresentReactivePower_isUsed:1;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}Dynamic_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}Dynamic_AC_CLReqControlModeType; base type=Dynamic_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); EVTargetEnergyRequest, RationalNumberType (1, 1); EVMaximumEnergyRequest, RationalNumberType (1, 1); EVMinimumEnergyRequest, RationalNumberType (1, 1); EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (1, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}CLReqControlMode; type={urn:iso:std:iso:15118:-20:CommonTypes}CLReqControlModeType; base type=; content type=empty;
//          abstract=False; final=False;
// Particle: 
struct iso20_ac_der_sae_CLReqControlModeType {
    int _unused;
};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}MeterInfo; type={urn:iso:std:iso:15118:-20:CommonTypes}MeterInfoType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: MeterID, meterIDType (1, 1); ChargedEnergyReadingWh, unsignedLong (1, 1); BPT_DischargedEnergyReadingWh, unsignedLong (0, 1); CapacitiveEnergyReadingVARh, unsignedLong (0, 1); BPT_InductiveEnergyReadingVARh, unsignedLong (0, 1); MeterSignature, meterSignatureType (0, 1); MeterStatus, short (0, 1); MeterTimestamp, unsignedLong (0, 1);
struct iso20_ac_der_sae_MeterInfoType {
    // MeterID, meterIDType (base: string)
    struct {
        char characters[iso20_ac_der_sae_MeterID_CHARACTER_SIZE];
        uint16_t charactersLen;
    } MeterID;
    // ChargedEnergyReadingWh, unsignedLong (base: nonNegativeInteger)
    uint64_t ChargedEnergyReadingWh;
    // BPT_DischargedEnergyReadingWh, unsignedLong (base: nonNegativeInteger)
    uint64_t BPT_DischargedEnergyReadingWh;
    unsigned int BPT_DischargedEnergyReadingWh_isUsed:1;
    // CapacitiveEnergyReadingVARh, unsignedLong (base: nonNegativeInteger)
    uint64_t CapacitiveEnergyReadingVARh;
    unsigned int CapacitiveEnergyReadingVARh_isUsed:1;
    // BPT_InductiveEnergyReadingVARh, unsignedLong (base: nonNegativeInteger)
    uint64_t BPT_InductiveEnergyReadingVARh;
    unsigned int BPT_InductiveEnergyReadingVARh_isUsed:1;
    // MeterSignature, meterSignatureType (base: base64Binary)
    struct {
        uint8_t bytes[iso20_ac_der_sae_meterSignatureType_BYTES_SIZE];
        uint16_t bytesLen;
    } MeterSignature;
    unsigned int MeterSignature_isUsed:1;

    // MeterStatus, short (base: int)
    int16_t MeterStatus;
    unsigned int MeterStatus_isUsed:1;
    // MeterTimestamp, unsignedLong (base: nonNegativeInteger)
    uint64_t MeterTimestamp;
    unsigned int MeterTimestamp_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}Receipt; type={urn:iso:std:iso:15118:-20:CommonTypes}ReceiptType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: TimeAnchor, unsignedLong (1, 1); EnergyCosts, DetailedCostType (0, 1); OccupancyCosts, DetailedCostType (0, 1); AdditionalServicesCosts, DetailedCostType (0, 1); OverstayCosts, DetailedCostType (0, 1); TaxCosts, DetailedTaxType (0, 10);
struct iso20_ac_der_sae_ReceiptType {
    // TimeAnchor, unsignedLong (base: nonNegativeInteger)
    uint64_t TimeAnchor;
    // EnergyCosts, DetailedCostType
    struct iso20_ac_der_sae_DetailedCostType EnergyCosts;
    unsigned int EnergyCosts_isUsed:1;
    // OccupancyCosts, DetailedCostType
    struct iso20_ac_der_sae_DetailedCostType OccupancyCosts;
    unsigned int OccupancyCosts_isUsed:1;
    // AdditionalServicesCosts, DetailedCostType
    struct iso20_ac_der_sae_DetailedCostType AdditionalServicesCosts;
    unsigned int AdditionalServicesCosts_isUsed:1;
    // OverstayCosts, DetailedCostType
    struct iso20_ac_der_sae_DetailedCostType OverstayCosts;
    unsigned int OverstayCosts_isUsed:1;
    // TaxCosts, DetailedTaxType
    struct {
        struct iso20_ac_der_sae_DetailedTaxType array[iso20_ac_der_sae_DetailedTaxType_10_ARRAY_SIZE];
        uint16_t arrayLen;
    } TaxCosts;
};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}Dynamic_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}Dynamic_AC_CLResControlModeType; base type=Dynamic_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); MinimumSOC, percentValueType (0, 1); TargetSOC, percentValueType (0, 1); AckMaxDelay, unsignedShort (0, 1); EVSETargetActivePower, RationalNumberType (1, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_Dynamic_AC_CLResControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // MinimumSOC, percentValueType (base: byte)
    int8_t MinimumSOC;
    unsigned int MinimumSOC_isUsed:1;
    // TargetSOC, percentValueType (base: byte)
    int8_t TargetSOC;
    unsigned int TargetSOC_isUsed:1;
    // AckMaxDelay, unsignedShort (base: unsignedInt)
    uint16_t AckMaxDelay;
    unsigned int AckMaxDelay_isUsed:1;
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}Scheduled_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}Scheduled_AC_CLResControlModeType; base type=Scheduled_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVSETargetActivePower, RationalNumberType (0, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_Scheduled_AC_CLResControlModeType {
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    unsigned int EVSETargetActivePower_isUsed:1;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:CommonTypes}CLResControlMode; type={urn:iso:std:iso:15118:-20:CommonTypes}CLResControlModeType; base type=; content type=empty;
//          abstract=False; final=False;
// Particle: 
struct iso20_ac_der_sae_CLResControlModeType {
    int _unused;
};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERControlCLRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERControlCLResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: VoltageTrip, VoltageTripType (0, 1); FrequencyTrip, FrequencyTripType (0, 1); EnterServiceCLRes, EnterServiceCLResType (1, 1); ReactivePowerSupportCLRes, ReactivePowerSupportCLResType (0, 1); ActivePowerSupportCLRes, ActivePowerSupportCLResType (0, 1);
struct iso20_ac_der_sae_DERControlCLResType {
    // VoltageTrip, VoltageTripType
    struct iso20_ac_der_sae_VoltageTripType VoltageTrip;
    unsigned int VoltageTrip_isUsed:1;
    // FrequencyTrip, FrequencyTripType
    struct iso20_ac_der_sae_FrequencyTripType FrequencyTrip;
    unsigned int FrequencyTrip_isUsed:1;
    // EnterServiceCLRes, EnterServiceCLResType
    struct iso20_ac_der_sae_EnterServiceCLResType EnterServiceCLRes;
    // ReactivePowerSupportCLRes, ReactivePowerSupportCLResType
    struct iso20_ac_der_sae_ReactivePowerSupportCLResType ReactivePowerSupportCLRes;
    unsigned int ReactivePowerSupportCLRes_isUsed:1;
    // ActivePowerSupportCLRes, ActivePowerSupportCLResType
    struct iso20_ac_der_sae_ActivePowerSupportCLResType ActivePowerSupportCLRes;
    unsigned int ActivePowerSupportCLRes_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVApparentPowerLimits; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVApparentPowerLimitsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVMaximumApparentPowerDuringChargingAndVarAbsorption, RationalNumberType (1, 1); EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarInjection, RationalNumberType (1, 1); EVMaximumApparentPowerDuringChargingAndVarInjection_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarInjection_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption, RationalNumberType (1, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection, RationalNumberType (1, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVApparentPowerLimitsType {
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption;
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarInjection, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection;
    // EVMaximumApparentPowerDuringChargingAndVarInjection_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection_L2;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarInjection_L2_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarInjection_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection_L3;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarInjection_L3_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection_L2;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarInjection_L2_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection_L3;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarInjection_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERControlCPDRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DERControlCPDResType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: VoltageTrip, VoltageTripType (1, 1); FrequencyTrip, FrequencyTripType (1, 1); EnterServiceCPDRes, EnterServiceCPDResType (1, 1); ReactivePowerSupportCPDRes, ReactivePowerSupportCPDResType (1, 1); ActivePowerSupportCPDRes, ActivePowerSupportCPDResType (1, 1);
struct iso20_ac_der_sae_DERControlCPDResType {
    // VoltageTrip, VoltageTripType
    struct iso20_ac_der_sae_VoltageTripType VoltageTrip;
    // FrequencyTrip, FrequencyTripType
    struct iso20_ac_der_sae_FrequencyTripType FrequencyTrip;
    // EnterServiceCPDRes, EnterServiceCPDResType
    struct iso20_ac_der_sae_EnterServiceCPDResType EnterServiceCPDRes;
    // ReactivePowerSupportCPDRes, ReactivePowerSupportCPDResType
    struct iso20_ac_der_sae_ReactivePowerSupportCPDResType ReactivePowerSupportCPDRes;
    // ActivePowerSupportCPDRes, ActivePowerSupportCPDResType
    struct iso20_ac_der_sae_ActivePowerSupportCPDResType ActivePowerSupportCPDRes;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVReactivePowerLimits; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVReactivePowerLimitsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVMaximumVarAbsorptionDuringCharging, RationalNumberType (1, 1); EVMaximumVarAbsorptionDuringCharging_L2, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringCharging_L3, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging_L2, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging_L3, RationalNumberType (0, 1); EVMaximumVarInjectionDuringCharging, RationalNumberType (1, 1); EVMaximumVarInjectionDuringCharging_L2, RationalNumberType (0, 1); EVMaximumVarInjectionDuringCharging_L3, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging_L2, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging_L3, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringDischarging, RationalNumberType (1, 1); EVMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging_L2, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging_L3, RationalNumberType (0, 1); EVMaximumVarInjectionDuringDischarging, RationalNumberType (1, 1); EVMaximumVarInjectionDuringDischarging_L2, RationalNumberType (0, 1); EVMaximumVarInjectionDuringDischarging_L3, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging_L2, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging_L3, RationalNumberType (0, 1); EVReactiveSusceptance, RationalNumberType (1, 1); EVReactiveSusceptance_L2, RationalNumberType (0, 1); EVReactiveSusceptance_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVReactivePowerLimitsType {
    // EVMaximumVarAbsorptionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging;
    // EVMaximumVarAbsorptionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging_L2;
    unsigned int EVMaximumVarAbsorptionDuringCharging_L2_isUsed:1;
    // EVMaximumVarAbsorptionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging_L3;
    unsigned int EVMaximumVarAbsorptionDuringCharging_L3_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging;
    unsigned int EVMinimumVarAbsorptionDuringCharging_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging_L2;
    unsigned int EVMinimumVarAbsorptionDuringCharging_L2_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging_L3;
    unsigned int EVMinimumVarAbsorptionDuringCharging_L3_isUsed:1;
    // EVMaximumVarInjectionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging;
    // EVMaximumVarInjectionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging_L2;
    unsigned int EVMaximumVarInjectionDuringCharging_L2_isUsed:1;
    // EVMaximumVarInjectionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging_L3;
    unsigned int EVMaximumVarInjectionDuringCharging_L3_isUsed:1;
    // EVMinimumVarInjectionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging;
    unsigned int EVMinimumVarInjectionDuringCharging_isUsed:1;
    // EVMinimumVarInjectionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging_L2;
    unsigned int EVMinimumVarInjectionDuringCharging_L2_isUsed:1;
    // EVMinimumVarInjectionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging_L3;
    unsigned int EVMinimumVarInjectionDuringCharging_L3_isUsed:1;
    // EVMaximumVarAbsorptionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging;
    // EVMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging_L2;
    unsigned int EVMaximumVarAbsorptionDuringDischarging_L2_isUsed:1;
    // EVMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging_L3;
    unsigned int EVMaximumVarAbsorptionDuringDischarging_L3_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging_L2;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_L2_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging_L3;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_L3_isUsed:1;
    // EVMaximumVarInjectionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging;
    // EVMaximumVarInjectionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging_L2;
    unsigned int EVMaximumVarInjectionDuringDischarging_L2_isUsed:1;
    // EVMaximumVarInjectionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging_L3;
    unsigned int EVMaximumVarInjectionDuringDischarging_L3_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging;
    unsigned int EVMinimumVarInjectionDuringDischarging_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging_L2;
    unsigned int EVMinimumVarInjectionDuringDischarging_L2_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging_L3;
    unsigned int EVMinimumVarInjectionDuringDischarging_L3_isUsed:1;
    // EVReactiveSusceptance, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVReactiveSusceptance;
    // EVReactiveSusceptance_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVReactiveSusceptance_L2;
    unsigned int EVReactiveSusceptance_L2_isUsed:1;
    // EVReactiveSusceptance_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVReactiveSusceptance_L3;
    unsigned int EVReactiveSusceptance_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVExcitationLimits; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVExcitationLimitsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVSpecifiedOverExcitedPowerFactor, RationalNumberType (1, 1); EVSpecifiedOverExcitedPowerFactor_L2, RationalNumberType (0, 1); EVSpecifiedOverExcitedPowerFactor_L3, RationalNumberType (0, 1); EVSpecifiedOverExcitedDischargePower, RationalNumberType (1, 1); EVSpecifiedOverExcitedDischargePower_L2, RationalNumberType (0, 1); EVSpecifiedOverExcitedDischargePower_L3, RationalNumberType (0, 1); EVSpecifiedUnderExcitedPowerFactor, RationalNumberType (1, 1); EVSpecifiedUnderExcitedPowerFactor_L2, RationalNumberType (0, 1); EVSpecifiedUnderExcitedPowerFactor_L3, RationalNumberType (0, 1); EVSpecifiedUnderExcitedDischargePower, RationalNumberType (1, 1); EVSpecifiedUnderExcitedDischargePower_L2, RationalNumberType (0, 1); EVSpecifiedUnderExcitedDischargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVExcitationLimitsType {
    // EVSpecifiedOverExcitedPowerFactor, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor;
    // EVSpecifiedOverExcitedPowerFactor_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor_L2;
    unsigned int EVSpecifiedOverExcitedPowerFactor_L2_isUsed:1;
    // EVSpecifiedOverExcitedPowerFactor_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor_L3;
    unsigned int EVSpecifiedOverExcitedPowerFactor_L3_isUsed:1;
    // EVSpecifiedOverExcitedDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower;
    // EVSpecifiedOverExcitedDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower_L2;
    unsigned int EVSpecifiedOverExcitedDischargePower_L2_isUsed:1;
    // EVSpecifiedOverExcitedDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower_L3;
    unsigned int EVSpecifiedOverExcitedDischargePower_L3_isUsed:1;
    // EVSpecifiedUnderExcitedPowerFactor, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor;
    // EVSpecifiedUnderExcitedPowerFactor_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor_L2;
    unsigned int EVSpecifiedUnderExcitedPowerFactor_L2_isUsed:1;
    // EVSpecifiedUnderExcitedPowerFactor_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor_L3;
    unsigned int EVSpecifiedUnderExcitedPowerFactor_L3_isUsed:1;
    // EVSpecifiedUnderExcitedDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower;
    // EVSpecifiedUnderExcitedDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower_L2;
    unsigned int EVSpecifiedUnderExcitedDischargePower_L2_isUsed:1;
    // EVSpecifiedUnderExcitedDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower_L3;
    unsigned int EVSpecifiedUnderExcitedDischargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVInverterDetails; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVInverterDetailsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVInverterSwVersion, evInverterSwVersionType (1, 1); EVInverterHwVersion, evInverterHwVersionType (0, 1); EVInverterManufacturer, evInverterManufacturerType (1, 1); EVInverterModel, evInverterModelType (1, 1); EVInverterSerialNumber, evInverterSerialNumberType (1, 1);
struct iso20_ac_der_sae_EVInverterDetailsType {
    // EVInverterSwVersion, evInverterSwVersionType (base: string)
    struct {
        char characters[iso20_ac_der_sae_EVInverterSwVersion_CHARACTER_SIZE];
        uint16_t charactersLen;
    } EVInverterSwVersion;
    // EVInverterHwVersion, evInverterHwVersionType (base: string)
    struct {
        char characters[iso20_ac_der_sae_EVInverterHwVersion_CHARACTER_SIZE];
        uint16_t charactersLen;
    } EVInverterHwVersion;
    unsigned int EVInverterHwVersion_isUsed:1;
    // EVInverterManufacturer, evInverterManufacturerType (base: string)
    struct {
        char characters[iso20_ac_der_sae_EVInverterManufacturer_CHARACTER_SIZE];
        uint16_t charactersLen;
    } EVInverterManufacturer;
    // EVInverterModel, evInverterModelType (base: string)
    struct {
        char characters[iso20_ac_der_sae_EVInverterModel_CHARACTER_SIZE];
        uint16_t charactersLen;
    } EVInverterModel;
    // EVInverterSerialNumber, evInverterSerialNumberType (base: string)
    struct {
        char characters[iso20_ac_der_sae_EVInverterSerialNumber_CHARACTER_SIZE];
        uint16_t charactersLen;
    } EVInverterSerialNumber;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVSEReactivePowerLimits; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVSEReactivePowerLimitsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVSEMaximumVarAbsorptionDuringCharging, RationalNumberType (1, 1); EVSEMaximumVarAbsorptionDuringCharging_L2, RationalNumberType (0, 1); EVSEMaximumVarAbsorptionDuringCharging_L3, RationalNumberType (0, 1); EVSEMaximumVarInjectionDuringCharging, RationalNumberType (1, 1); EVSEMaximumVarInjectionDuringCharging_L2, RationalNumberType (0, 1); EVSEMaximumVarInjectionDuringCharging_L3, RationalNumberType (0, 1); EVSEMaximumVarAbsorptionDuringDischarging, RationalNumberType (1, 1); EVSEMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType (0, 1); EVSEMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType (0, 1); EVSEMaximumVarInjectionDuringDischarging, RationalNumberType (1, 1); EVSEMaximumVarInjectionDuringDischarging_L2, RationalNumberType (0, 1); EVSEMaximumVarInjectionDuringDischarging_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVSEReactivePowerLimitsType {
    // EVSEMaximumVarAbsorptionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringCharging;
    // EVSEMaximumVarAbsorptionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringCharging_L2;
    unsigned int EVSEMaximumVarAbsorptionDuringCharging_L2_isUsed:1;
    // EVSEMaximumVarAbsorptionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringCharging_L3;
    unsigned int EVSEMaximumVarAbsorptionDuringCharging_L3_isUsed:1;
    // EVSEMaximumVarInjectionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringCharging;
    // EVSEMaximumVarInjectionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringCharging_L2;
    unsigned int EVSEMaximumVarInjectionDuringCharging_L2_isUsed:1;
    // EVSEMaximumVarInjectionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringCharging_L3;
    unsigned int EVSEMaximumVarInjectionDuringCharging_L3_isUsed:1;
    // EVSEMaximumVarAbsorptionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringDischarging;
    // EVSEMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringDischarging_L2;
    unsigned int EVSEMaximumVarAbsorptionDuringDischarging_L2_isUsed:1;
    // EVSEMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarAbsorptionDuringDischarging_L3;
    unsigned int EVSEMaximumVarAbsorptionDuringDischarging_L3_isUsed:1;
    // EVSEMaximumVarInjectionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringDischarging;
    // EVSEMaximumVarInjectionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringDischarging_L2;
    unsigned int EVSEMaximumVarInjectionDuringDischarging_L2_isUsed:1;
    // EVSEMaximumVarInjectionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumVarInjectionDuringDischarging_L3;
    unsigned int EVSEMaximumVarInjectionDuringDischarging_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}GridLimits; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}GridLimitsType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: GridNominalFrequency, RationalNumberType (1, 1); GridNominalVoltage, RationalNumberType (1, 1); GridNominalVoltageOffset, RationalNumberType (1, 1); GridMinFrequency, RationalNumberType (0, 1); GridMaxFrequency, RationalNumberType (0, 1); GridMaximumVoltage, RationalNumberType (1, 1); GridMinimumVoltage, RationalNumberType (1, 1);
struct iso20_ac_der_sae_GridLimitsType {
    // GridNominalFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridNominalFrequency;
    // GridNominalVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridNominalVoltage;
    // GridNominalVoltageOffset, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridNominalVoltageOffset;
    // GridMinFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridMinFrequency;
    unsigned int GridMinFrequency_isUsed:1;
    // GridMaxFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridMaxFrequency;
    unsigned int GridMaxFrequency_isUsed:1;
    // GridMaximumVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridMaximumVoltage;
    // GridMinimumVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType GridMinimumVoltage;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVApparentPower; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVApparentPowerType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVMaximumApparentPowerDuringChargingAndVarAbsorption, RationalNumberType (1, 1); EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarInjection, RationalNumberType (1, 1); EVMaximumApparentPowerDuringChargingAndVarInjection_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringChargingAndVarInjection_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption, RationalNumberType (1, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection, RationalNumberType (1, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection_L2, RationalNumberType (0, 1); EVMaximumApparentPowerDuringDischargingAndVarInjection_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVApparentPowerType {
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption;
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarAbsorption_L2_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarAbsorption_L3_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarInjection, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection;
    // EVMaximumApparentPowerDuringChargingAndVarInjection_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection_L2;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarInjection_L2_isUsed:1;
    // EVMaximumApparentPowerDuringChargingAndVarInjection_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringChargingAndVarInjection_L3;
    unsigned int EVMaximumApparentPowerDuringChargingAndVarInjection_L3_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L2_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarAbsorption_L3_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection_L2;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarInjection_L2_isUsed:1;
    // EVMaximumApparentPowerDuringDischargingAndVarInjection_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumApparentPowerDuringDischargingAndVarInjection_L3;
    unsigned int EVMaximumApparentPowerDuringDischargingAndVarInjection_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVReactivePower; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVReactivePowerType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVMaximumVarAbsorptionDuringCharging, RationalNumberType (1, 1); EVMaximumVarAbsorptionDuringCharging_L2, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringCharging_L3, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging_L2, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringCharging_L3, RationalNumberType (0, 1); EVMaximumVarInjectionDuringCharging, RationalNumberType (1, 1); EVMaximumVarInjectionDuringCharging_L2, RationalNumberType (0, 1); EVMaximumVarInjectionDuringCharging_L3, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging_L2, RationalNumberType (0, 1); EVMinimumVarInjectionDuringCharging_L3, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringDischarging, RationalNumberType (1, 1); EVMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType (0, 1); EVMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging_L2, RationalNumberType (0, 1); EVMinimumVarAbsorptionDuringDischarging_L3, RationalNumberType (0, 1); EVMaximumVarInjectionDuringDischarging, RationalNumberType (1, 1); EVMaximumVarInjectionDuringDischarging_L2, RationalNumberType (0, 1); EVMaximumVarInjectionDuringDischarging_L3, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging_L2, RationalNumberType (0, 1); EVMinimumVarInjectionDuringDischarging_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVReactivePowerType {
    // EVMaximumVarAbsorptionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging;
    // EVMaximumVarAbsorptionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging_L2;
    unsigned int EVMaximumVarAbsorptionDuringCharging_L2_isUsed:1;
    // EVMaximumVarAbsorptionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringCharging_L3;
    unsigned int EVMaximumVarAbsorptionDuringCharging_L3_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging;
    unsigned int EVMinimumVarAbsorptionDuringCharging_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging_L2;
    unsigned int EVMinimumVarAbsorptionDuringCharging_L2_isUsed:1;
    // EVMinimumVarAbsorptionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringCharging_L3;
    unsigned int EVMinimumVarAbsorptionDuringCharging_L3_isUsed:1;
    // EVMaximumVarInjectionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging;
    // EVMaximumVarInjectionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging_L2;
    unsigned int EVMaximumVarInjectionDuringCharging_L2_isUsed:1;
    // EVMaximumVarInjectionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringCharging_L3;
    unsigned int EVMaximumVarInjectionDuringCharging_L3_isUsed:1;
    // EVMinimumVarInjectionDuringCharging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging;
    unsigned int EVMinimumVarInjectionDuringCharging_isUsed:1;
    // EVMinimumVarInjectionDuringCharging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging_L2;
    unsigned int EVMinimumVarInjectionDuringCharging_L2_isUsed:1;
    // EVMinimumVarInjectionDuringCharging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringCharging_L3;
    unsigned int EVMinimumVarInjectionDuringCharging_L3_isUsed:1;
    // EVMaximumVarAbsorptionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging;
    // EVMaximumVarAbsorptionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging_L2;
    unsigned int EVMaximumVarAbsorptionDuringDischarging_L2_isUsed:1;
    // EVMaximumVarAbsorptionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarAbsorptionDuringDischarging_L3;
    unsigned int EVMaximumVarAbsorptionDuringDischarging_L3_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging_L2;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_L2_isUsed:1;
    // EVMinimumVarAbsorptionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarAbsorptionDuringDischarging_L3;
    unsigned int EVMinimumVarAbsorptionDuringDischarging_L3_isUsed:1;
    // EVMaximumVarInjectionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging;
    // EVMaximumVarInjectionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging_L2;
    unsigned int EVMaximumVarInjectionDuringDischarging_L2_isUsed:1;
    // EVMaximumVarInjectionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVarInjectionDuringDischarging_L3;
    unsigned int EVMaximumVarInjectionDuringDischarging_L3_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging;
    unsigned int EVMinimumVarInjectionDuringDischarging_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging_L2;
    unsigned int EVMinimumVarInjectionDuringDischarging_L2_isUsed:1;
    // EVMinimumVarInjectionDuringDischarging_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVarInjectionDuringDischarging_L3;
    unsigned int EVMinimumVarInjectionDuringDischarging_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVExcitation; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}EVExcitationType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: EVSpecifiedOverExcitedPowerFactor, RationalNumberType (0, 1); EVSpecifiedOverExcitedPowerFactor_L2, RationalNumberType (0, 1); EVSpecifiedOverExcitedPowerFactor_L3, RationalNumberType (0, 1); EVSpecifiedOverExcitedDischargePower, RationalNumberType (1, 1); EVSpecifiedOverExcitedDischargePower_L2, RationalNumberType (0, 1); EVSpecifiedOverExcitedDischargePower_L3, RationalNumberType (0, 1); EVSpecifiedUnderExcitedPowerFactor, RationalNumberType (0, 1); EVSpecifiedUnderExcitedPowerFactor_L2, RationalNumberType (0, 1); EVSpecifiedUnderExcitedPowerFactor_L3, RationalNumberType (0, 1); EVSpecifiedUnderExcitedDischargePower, RationalNumberType (1, 1); EVSpecifiedUnderExcitedDischargePower_L2, RationalNumberType (0, 1); EVSpecifiedUnderExcitedDischargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_EVExcitationType {
    // EVSpecifiedOverExcitedPowerFactor, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor;
    unsigned int EVSpecifiedOverExcitedPowerFactor_isUsed:1;
    // EVSpecifiedOverExcitedPowerFactor_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor_L2;
    unsigned int EVSpecifiedOverExcitedPowerFactor_L2_isUsed:1;
    // EVSpecifiedOverExcitedPowerFactor_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedPowerFactor_L3;
    unsigned int EVSpecifiedOverExcitedPowerFactor_L3_isUsed:1;
    // EVSpecifiedOverExcitedDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower;
    // EVSpecifiedOverExcitedDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower_L2;
    unsigned int EVSpecifiedOverExcitedDischargePower_L2_isUsed:1;
    // EVSpecifiedOverExcitedDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedOverExcitedDischargePower_L3;
    unsigned int EVSpecifiedOverExcitedDischargePower_L3_isUsed:1;
    // EVSpecifiedUnderExcitedPowerFactor, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor;
    unsigned int EVSpecifiedUnderExcitedPowerFactor_isUsed:1;
    // EVSpecifiedUnderExcitedPowerFactor_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor_L2;
    unsigned int EVSpecifiedUnderExcitedPowerFactor_L2_isUsed:1;
    // EVSpecifiedUnderExcitedPowerFactor_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedPowerFactor_L3;
    unsigned int EVSpecifiedUnderExcitedPowerFactor_L3_isUsed:1;
    // EVSpecifiedUnderExcitedDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower;
    // EVSpecifiedUnderExcitedDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower_L2;
    unsigned int EVSpecifiedUnderExcitedDischargePower_L2_isUsed:1;
    // EVSpecifiedUnderExcitedDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSpecifiedUnderExcitedDischargePower_L3;
    unsigned int EVSpecifiedUnderExcitedDischargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_AC_CPDReqEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_AC_CPDReqEnergyTransferModeType; base type=AC_CPDReqEnergyTransferModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVMaximumDischargePower, RationalNumberType (1, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (1, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType {
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_AC_CPDResEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_AC_CPDResEnergyTransferModeType; base type=AC_CPDResEnergyTransferModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVSEMaximumChargePower, RationalNumberType (1, 1); EVSEMaximumChargePower_L2, RationalNumberType (0, 1); EVSEMaximumChargePower_L3, RationalNumberType (0, 1); EVSEMinimumChargePower, RationalNumberType (1, 1); EVSEMinimumChargePower_L2, RationalNumberType (0, 1); EVSEMinimumChargePower_L3, RationalNumberType (0, 1); EVSENominalFrequency, RationalNumberType (1, 1); MaximumPowerAsymmetry, RationalNumberType (0, 1); EVSEPowerRampLimitation, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1); EVSEMaximumDischargePower, RationalNumberType (1, 1); EVSEMaximumDischargePower_L2, RationalNumberType (0, 1); EVSEMaximumDischargePower_L3, RationalNumberType (0, 1); EVSEMinimumDischargePower, RationalNumberType (1, 1); EVSEMinimumDischargePower_L2, RationalNumberType (0, 1); EVSEMinimumDischargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType {
    // EVSEMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower;
    // EVSEMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L2;
    unsigned int EVSEMaximumChargePower_L2_isUsed:1;
    // EVSEMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L3;
    unsigned int EVSEMaximumChargePower_L3_isUsed:1;
    // EVSEMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower;
    // EVSEMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L2;
    unsigned int EVSEMinimumChargePower_L2_isUsed:1;
    // EVSEMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L3;
    unsigned int EVSEMinimumChargePower_L3_isUsed:1;
    // EVSENominalFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalFrequency;
    // MaximumPowerAsymmetry, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType MaximumPowerAsymmetry;
    unsigned int MaximumPowerAsymmetry_isUsed:1;
    // EVSEPowerRampLimitation, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPowerRampLimitation;
    unsigned int EVSEPowerRampLimitation_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;
    // EVSEMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower;
    // EVSEMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L2;
    unsigned int EVSEMaximumDischargePower_L2_isUsed:1;
    // EVSEMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L3;
    unsigned int EVSEMaximumDischargePower_L3_isUsed:1;
    // EVSEMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumDischargePower;
    // EVSEMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumDischargePower_L2;
    unsigned int EVSEMinimumDischargePower_L2_isUsed:1;
    // EVSEMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumDischargePower_L3;
    unsigned int EVSEMinimumDischargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_AC_CPDReqEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_AC_CPDReqEnergyTransferModeType; base type=AC_CPDReqEnergyTransferModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVProcessing, processingType (1, 1); EVMaximumDischargePower, RationalNumberType (1, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (0, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1); EVSessionTotalDischargeEnergyAvailable, RationalNumberType (0, 1); EVApparentPowerLimits, EVApparentPowerLimitsType (1, 1); EVReactivePowerLimits, EVReactivePowerLimitsType (1, 1); EVExcitationLimits, EVExcitationLimitsType (1, 1); EVInverterDetails, EVInverterDetailsType (1, 1); IEEE1547NormalCategory, ieee1547NormalCategoryType (1, 1); IEEE1547AbnormalCategory, ieee1547AbnormalCategoryType (1, 1); EVNominalVoltage, RationalNumberType (1, 1); EVMaximumVoltage, RationalNumberType (1, 1); EVMinimumVoltage, RationalNumberType (1, 1); EVNominalVoltageOffset, RationalNumberType (1, 1); J3072Certified, boolean (1, 1); J3072CertificationDate, unsignedLong (1, 1); EVUseableWattHours, unsignedInt (1, 1); EVUpdateTime, unsignedLong (1, 1); SupportedModes, unsignedInt (1, 1); EnabledModes, unsignedInt (1, 1);
struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType {
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVProcessing, processingType (base: string)
    iso20_ac_der_sae_processingType EVProcessing;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    unsigned int EVMinimumDischargePower_isUsed:1;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;
    // EVSessionTotalDischargeEnergyAvailable, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSessionTotalDischargeEnergyAvailable;
    unsigned int EVSessionTotalDischargeEnergyAvailable_isUsed:1;
    // EVApparentPowerLimits, EVApparentPowerLimitsType
    struct iso20_ac_der_sae_EVApparentPowerLimitsType EVApparentPowerLimits;
    // EVReactivePowerLimits, EVReactivePowerLimitsType
    struct iso20_ac_der_sae_EVReactivePowerLimitsType EVReactivePowerLimits;
    // EVExcitationLimits, EVExcitationLimitsType
    struct iso20_ac_der_sae_EVExcitationLimitsType EVExcitationLimits;
    // EVInverterDetails, EVInverterDetailsType
    struct iso20_ac_der_sae_EVInverterDetailsType EVInverterDetails;
    // IEEE1547NormalCategory, ieee1547NormalCategoryType (base: string)
    iso20_ac_der_sae_ieee1547NormalCategoryType IEEE1547NormalCategory;
    // IEEE1547AbnormalCategory, ieee1547AbnormalCategoryType (base: string)
    iso20_ac_der_sae_ieee1547AbnormalCategoryType IEEE1547AbnormalCategory;
    // EVNominalVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVNominalVoltage;
    // EVMaximumVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumVoltage;
    // EVMinimumVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumVoltage;
    // EVNominalVoltageOffset, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVNominalVoltageOffset;
    // J3072Certified, boolean
    int J3072Certified;
    // J3072CertificationDate, unsignedLong (base: nonNegativeInteger)
    uint64_t J3072CertificationDate;
    // EVUseableWattHours, unsignedInt (base: unsignedLong)
    uint32_t EVUseableWattHours;
    // EVUpdateTime, unsignedLong (base: nonNegativeInteger)
    uint64_t EVUpdateTime;
    // SupportedModes, unsignedInt (base: unsignedLong)
    uint32_t SupportedModes;
    // EnabledModes, unsignedInt (base: unsignedLong)
    uint32_t EnabledModes;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeParameterDiscoveryReq; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeParameterDiscoveryReqType; base type=ChargeParameterDiscoveryReqType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: Header, MessageHeaderType (1, 1); AC_CPDReqEnergyTransferMode, AC_CPDReqEnergyTransferModeType (0, 1); BPT_AC_CPDReqEnergyTransferMode, BPT_AC_CPDReqEnergyTransferModeType (0, 1); DER_AC_CPDReqEnergyTransferMode, DER_AC_CPDReqEnergyTransferModeType (0, 1);
struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType {
    // Header, MessageHeaderType
    struct iso20_ac_der_sae_MessageHeaderType Header;
    // AC_CPDReqEnergyTransferMode, AC_CPDReqEnergyTransferModeType
    struct iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType AC_CPDReqEnergyTransferMode;
    unsigned int AC_CPDReqEnergyTransferMode_isUsed:1;
    // BPT_AC_CPDReqEnergyTransferMode, BPT_AC_CPDReqEnergyTransferModeType (base: AC_CPDReqEnergyTransferModeType)
    struct iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType BPT_AC_CPDReqEnergyTransferMode;
    unsigned int BPT_AC_CPDReqEnergyTransferMode_isUsed:1;
    // DER_AC_CPDReqEnergyTransferMode, DER_AC_CPDReqEnergyTransferModeType (base: AC_CPDReqEnergyTransferModeType)
    struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType DER_AC_CPDReqEnergyTransferMode;
    unsigned int DER_AC_CPDReqEnergyTransferMode_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_AC_CPDResEnergyTransferMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_AC_CPDResEnergyTransferModeType; base type=AC_CPDResEnergyTransferModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVSEMaximumChargePower, RationalNumberType (1, 1); EVSEMaximumChargePower_L2, RationalNumberType (0, 1); EVSEMaximumChargePower_L3, RationalNumberType (0, 1); EVSEMinimumChargePower, RationalNumberType (1, 1); EVSEMinimumChargePower_L2, RationalNumberType (0, 1); EVSEMinimumChargePower_L3, RationalNumberType (0, 1); EVSENominalFrequency, RationalNumberType (1, 1); MaximumPowerAsymmetry, RationalNumberType (0, 1); EVSEPowerRampLimitation, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1); EVSEProcessing, processingType (1, 1); EVSEStatus, EVSEStatusType (0, 1); DERControlCPDRes, DERControlCPDResType (1, 1); EVSENominalChargePower, RationalNumberType (0, 1); EVSENominalChargePower_L2, RationalNumberType (0, 1); EVSENominalChargePower_L3, RationalNumberType (0, 1); EVSENominalDischargePower, RationalNumberType (0, 1); EVSENominalDischargePower_L2, RationalNumberType (0, 1); EVSENominalDischargePower_L3, RationalNumberType (0, 1); EVSEMaximumDischargePower, RationalNumberType (1, 1); EVSEMaximumDischargePower_L2, RationalNumberType (0, 1); EVSEMaximumDischargePower_L3, RationalNumberType (0, 1); EVSEReactivePowerLimits, EVSEReactivePowerLimitsType (1, 1); GridLimits, GridLimitsType (1, 1); RequiredDEROperatingMode, requiredDEROperatingModeType (1, 1); GridConnectionMode, gridConnectionModeType (1, 1); EVSEUpdateTime, unsignedLong (1, 1);
struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType {
    // EVSEMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower;
    // EVSEMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L2;
    unsigned int EVSEMaximumChargePower_L2_isUsed:1;
    // EVSEMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L3;
    unsigned int EVSEMaximumChargePower_L3_isUsed:1;
    // EVSEMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower;
    // EVSEMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L2;
    unsigned int EVSEMinimumChargePower_L2_isUsed:1;
    // EVSEMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMinimumChargePower_L3;
    unsigned int EVSEMinimumChargePower_L3_isUsed:1;
    // EVSENominalFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalFrequency;
    // MaximumPowerAsymmetry, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType MaximumPowerAsymmetry;
    unsigned int MaximumPowerAsymmetry_isUsed:1;
    // EVSEPowerRampLimitation, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPowerRampLimitation;
    unsigned int EVSEPowerRampLimitation_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;
    // EVSEProcessing, processingType (base: string)
    iso20_ac_der_sae_processingType EVSEProcessing;
    // EVSEStatus, EVSEStatusType
    struct iso20_ac_der_sae_EVSEStatusType EVSEStatus;
    unsigned int EVSEStatus_isUsed:1;
    // DERControlCPDRes, DERControlCPDResType
    struct iso20_ac_der_sae_DERControlCPDResType DERControlCPDRes;
    // EVSENominalChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalChargePower;
    unsigned int EVSENominalChargePower_isUsed:1;
    // EVSENominalChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalChargePower_L2;
    unsigned int EVSENominalChargePower_L2_isUsed:1;
    // EVSENominalChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalChargePower_L3;
    unsigned int EVSENominalChargePower_L3_isUsed:1;
    // EVSENominalDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalDischargePower;
    unsigned int EVSENominalDischargePower_isUsed:1;
    // EVSENominalDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalDischargePower_L2;
    unsigned int EVSENominalDischargePower_L2_isUsed:1;
    // EVSENominalDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSENominalDischargePower_L3;
    unsigned int EVSENominalDischargePower_L3_isUsed:1;
    // EVSEMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower;
    // EVSEMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L2;
    unsigned int EVSEMaximumDischargePower_L2_isUsed:1;
    // EVSEMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L3;
    unsigned int EVSEMaximumDischargePower_L3_isUsed:1;
    // EVSEReactivePowerLimits, EVSEReactivePowerLimitsType
    struct iso20_ac_der_sae_EVSEReactivePowerLimitsType EVSEReactivePowerLimits;
    // GridLimits, GridLimitsType
    struct iso20_ac_der_sae_GridLimitsType GridLimits;
    // RequiredDEROperatingMode, requiredDEROperatingModeType (base: string)
    iso20_ac_der_sae_requiredDEROperatingModeType RequiredDEROperatingMode;
    // GridConnectionMode, gridConnectionModeType (base: string)
    iso20_ac_der_sae_gridConnectionModeType GridConnectionMode;
    // EVSEUpdateTime, unsignedLong (base: nonNegativeInteger)
    uint64_t EVSEUpdateTime;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeParameterDiscoveryRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeParameterDiscoveryResType; base type=ChargeParameterDiscoveryResType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: Header, MessageHeaderType (1, 1); ResponseCode, responseCodeType (1, 1); AC_CPDResEnergyTransferMode, AC_CPDResEnergyTransferModeType (0, 1); BPT_AC_CPDResEnergyTransferMode, BPT_AC_CPDResEnergyTransferModeType (0, 1); DER_AC_CPDResEnergyTransferMode, DER_AC_CPDResEnergyTransferModeType (0, 1);
struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType {
    // Header, MessageHeaderType
    struct iso20_ac_der_sae_MessageHeaderType Header;
    // ResponseCode, responseCodeType (base: string)
    iso20_ac_der_sae_responseCodeType ResponseCode;
    // AC_CPDResEnergyTransferMode, AC_CPDResEnergyTransferModeType
    struct iso20_ac_der_sae_AC_CPDResEnergyTransferModeType AC_CPDResEnergyTransferMode;
    unsigned int AC_CPDResEnergyTransferMode_isUsed:1;
    // BPT_AC_CPDResEnergyTransferMode, BPT_AC_CPDResEnergyTransferModeType (base: AC_CPDResEnergyTransferModeType)
    struct iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType BPT_AC_CPDResEnergyTransferMode;
    unsigned int BPT_AC_CPDResEnergyTransferMode_isUsed:1;
    // DER_AC_CPDResEnergyTransferMode, DER_AC_CPDResEnergyTransferModeType (base: AC_CPDResEnergyTransferModeType)
    struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType DER_AC_CPDResEnergyTransferMode;
    unsigned int DER_AC_CPDResEnergyTransferMode_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Scheduled_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Scheduled_AC_CLReqControlModeType; base type=Scheduled_AC_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVTargetEnergyRequest, RationalNumberType (0, 1); EVMaximumEnergyRequest, RationalNumberType (0, 1); EVMinimumEnergyRequest, RationalNumberType (0, 1); EVMaximumChargePower, RationalNumberType (0, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (0, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (0, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1); EVMaximumDischargePower, RationalNumberType (0, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (0, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType {
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    unsigned int EVTargetEnergyRequest_isUsed:1;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    unsigned int EVMaximumEnergyRequest_isUsed:1;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    unsigned int EVMinimumEnergyRequest_isUsed:1;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    unsigned int EVMaximumChargePower_isUsed:1;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    unsigned int EVMinimumChargePower_isUsed:1;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    unsigned int EVPresentReactivePower_isUsed:1;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    unsigned int EVMaximumDischargePower_isUsed:1;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    unsigned int EVMinimumDischargePower_isUsed:1;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Scheduled_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Scheduled_AC_CLResControlModeType; base type=Scheduled_AC_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVSETargetActivePower, RationalNumberType (0, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType {
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    unsigned int EVSETargetActivePower_isUsed:1;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Dynamic_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Dynamic_AC_CLReqControlModeType; base type=Dynamic_AC_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); EVTargetEnergyRequest, RationalNumberType (1, 1); EVMaximumEnergyRequest, RationalNumberType (1, 1); EVMinimumEnergyRequest, RationalNumberType (1, 1); EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (1, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1); EVMaximumDischargePower, RationalNumberType (1, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (1, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1); EVMaximumV2XEnergyRequest, RationalNumberType (0, 1); EVMinimumV2XEnergyRequest, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;
    // EVMaximumV2XEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumV2XEnergyRequest;
    unsigned int EVMaximumV2XEnergyRequest_isUsed:1;
    // EVMinimumV2XEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumV2XEnergyRequest;
    unsigned int EVMinimumV2XEnergyRequest_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Dynamic_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}BPT_Dynamic_AC_CLResControlModeType; base type=Dynamic_AC_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); MinimumSOC, percentValueType (0, 1); TargetSOC, percentValueType (0, 1); AckMaxDelay, unsignedShort (0, 1); EVSETargetActivePower, RationalNumberType (1, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1);
struct iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // MinimumSOC, percentValueType (base: byte)
    int8_t MinimumSOC;
    unsigned int MinimumSOC_isUsed:1;
    // TargetSOC, percentValueType (base: byte)
    int8_t TargetSOC;
    unsigned int TargetSOC_isUsed:1;
    // AckMaxDelay, unsignedShort (base: unsignedInt)
    uint16_t AckMaxDelay;
    unsigned int AckMaxDelay_isUsed:1;
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Scheduled_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Scheduled_AC_CLReqControlModeType; base type=Scheduled_AC_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVTargetEnergyRequest, RationalNumberType (0, 1); EVMaximumEnergyRequest, RationalNumberType (0, 1); EVMinimumEnergyRequest, RationalNumberType (0, 1); EVMaximumChargePower, RationalNumberType (0, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (0, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (0, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1); EVPresentVoltage, RationalNumberType (1, 1); EVPresentFrequency, RationalNumberType (1, 1); EVMaximumDischargePower, RationalNumberType (0, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (0, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1); DEROperationalState, derOperationalStateType (1, 1); DERConnectionStatus, derConnectionStatusType (1, 1); EVApparentPower, EVApparentPowerType (0, 1); EVReactivePower, EVReactivePowerType (0, 1); EVExcitation, EVExcitationType (0, 1); EVUpdateTime, unsignedLong (1, 1); EVMinimumChargingDuration, unsignedInt (0, 1); EVDurationMaximumChargeRate, unsignedInt (0, 1); EVDurationMaximumDischargeRate, unsignedInt (0, 1); DERAlarmStatus, unsignedInt (1, 1); EnabledModes, unsignedInt (1, 1);
struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType {
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    unsigned int EVTargetEnergyRequest_isUsed:1;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    unsigned int EVMaximumEnergyRequest_isUsed:1;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    unsigned int EVMinimumEnergyRequest_isUsed:1;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    unsigned int EVMaximumChargePower_isUsed:1;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    unsigned int EVMinimumChargePower_isUsed:1;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    unsigned int EVPresentReactivePower_isUsed:1;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;
    // EVPresentVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentVoltage;
    // EVPresentFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentFrequency;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    unsigned int EVMaximumDischargePower_isUsed:1;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    unsigned int EVMinimumDischargePower_isUsed:1;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;
    // DEROperationalState, derOperationalStateType (base: string)
    iso20_ac_der_sae_derOperationalStateType DEROperationalState;
    // DERConnectionStatus, derConnectionStatusType (base: string)
    iso20_ac_der_sae_derConnectionStatusType DERConnectionStatus;
    // EVApparentPower, EVApparentPowerType
    struct iso20_ac_der_sae_EVApparentPowerType EVApparentPower;
    unsigned int EVApparentPower_isUsed:1;
    // EVReactivePower, EVReactivePowerType
    struct iso20_ac_der_sae_EVReactivePowerType EVReactivePower;
    unsigned int EVReactivePower_isUsed:1;
    // EVExcitation, EVExcitationType
    struct iso20_ac_der_sae_EVExcitationType EVExcitation;
    unsigned int EVExcitation_isUsed:1;
    // EVUpdateTime, unsignedLong (base: nonNegativeInteger)
    uint64_t EVUpdateTime;
    // EVMinimumChargingDuration, unsignedInt (base: unsignedLong)
    uint32_t EVMinimumChargingDuration;
    unsigned int EVMinimumChargingDuration_isUsed:1;
    // EVDurationMaximumChargeRate, unsignedInt (base: unsignedLong)
    uint32_t EVDurationMaximumChargeRate;
    unsigned int EVDurationMaximumChargeRate_isUsed:1;
    // EVDurationMaximumDischargeRate, unsignedInt (base: unsignedLong)
    uint32_t EVDurationMaximumDischargeRate;
    unsigned int EVDurationMaximumDischargeRate_isUsed:1;
    // DERAlarmStatus, unsignedInt (base: unsignedLong)
    uint32_t DERAlarmStatus;
    // EnabledModes, unsignedInt (base: unsignedLong)
    uint32_t EnabledModes;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Scheduled_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Scheduled_AC_CLResControlModeType; base type=Scheduled_AC_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: EVSETargetActivePower, RationalNumberType (0, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1); DERControlCLRes, DERControlCLResType (1, 1); EVSEMaximumChargePower, RationalNumberType (0, 1); EVSEMaximumChargePower_L2, RationalNumberType (0, 1); EVSEMaximumChargePower_L3, RationalNumberType (0, 1); EVSEMaximumDischargePower, RationalNumberType (0, 1); EVSEMaximumDischargePower_L2, RationalNumberType (0, 1); EVSEMaximumDischargePower_L3, RationalNumberType (0, 1); RequiredDEROperatingMode, requiredDEROperatingModeType (0, 1); GridConnectionMode, gridConnectionModeType (0, 1);
struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType {
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    unsigned int EVSETargetActivePower_isUsed:1;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;
    // DERControlCLRes, DERControlCLResType
    struct iso20_ac_der_sae_DERControlCLResType DERControlCLRes;
    // EVSEMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower;
    unsigned int EVSEMaximumChargePower_isUsed:1;
    // EVSEMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L2;
    unsigned int EVSEMaximumChargePower_L2_isUsed:1;
    // EVSEMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L3;
    unsigned int EVSEMaximumChargePower_L3_isUsed:1;
    // EVSEMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower;
    unsigned int EVSEMaximumDischargePower_isUsed:1;
    // EVSEMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L2;
    unsigned int EVSEMaximumDischargePower_L2_isUsed:1;
    // EVSEMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L3;
    unsigned int EVSEMaximumDischargePower_L3_isUsed:1;
    // RequiredDEROperatingMode, requiredDEROperatingModeType (base: string)
    iso20_ac_der_sae_requiredDEROperatingModeType RequiredDEROperatingMode;
    unsigned int RequiredDEROperatingMode_isUsed:1;
    // GridConnectionMode, gridConnectionModeType (base: string)
    iso20_ac_der_sae_gridConnectionModeType GridConnectionMode;
    unsigned int GridConnectionMode_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Dynamic_AC_CLReqControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Dynamic_AC_CLReqControlModeType; base type=Dynamic_AC_CLReqControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); EVTargetEnergyRequest, RationalNumberType (1, 1); EVMaximumEnergyRequest, RationalNumberType (1, 1); EVMinimumEnergyRequest, RationalNumberType (1, 1); EVMaximumChargePower, RationalNumberType (1, 1); EVMaximumChargePower_L2, RationalNumberType (0, 1); EVMaximumChargePower_L3, RationalNumberType (0, 1); EVMinimumChargePower, RationalNumberType (1, 1); EVMinimumChargePower_L2, RationalNumberType (0, 1); EVMinimumChargePower_L3, RationalNumberType (0, 1); EVPresentActivePower, RationalNumberType (1, 1); EVPresentActivePower_L2, RationalNumberType (0, 1); EVPresentActivePower_L3, RationalNumberType (0, 1); EVPresentReactivePower, RationalNumberType (1, 1); EVPresentReactivePower_L2, RationalNumberType (0, 1); EVPresentReactivePower_L3, RationalNumberType (0, 1); EVMaximumDischargePower, RationalNumberType (1, 1); EVMaximumDischargePower_L2, RationalNumberType (0, 1); EVMaximumDischargePower_L3, RationalNumberType (0, 1); EVMinimumDischargePower, RationalNumberType (1, 1); EVMinimumDischargePower_L2, RationalNumberType (0, 1); EVMinimumDischargePower_L3, RationalNumberType (0, 1); EVPresentVoltage, RationalNumberType (1, 1); EVPresentFrequency, RationalNumberType (1, 1); EVSessionTotalDischargeEnergyAvailable, RationalNumberType (0, 1); EVApparentPower, EVApparentPowerType (0, 1); EVReactivePower, EVReactivePowerType (0, 1); EVExcitation, EVExcitationType (0, 1); EVMaximumV2XEnergyRequest, RationalNumberType (0, 1); EVMinimumV2XEnergyRequest, RationalNumberType (0, 1); DEROperationalState, derOperationalStateType (1, 1); DERConnectionStatus, derConnectionStatusType (1, 1); EVUpdateTime, unsignedLong (1, 1); EVMinimumChargingDuration, unsignedInt (1, 1); EVDurationMaximumChargeRate, unsignedInt (1, 1); EVDurationMaximumDischargeRate, unsignedInt (1, 1); DERAlarmStatus, unsignedInt (1, 1); EnabledModes, unsignedInt (1, 1);
struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // EVTargetEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVTargetEnergyRequest;
    // EVMaximumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumEnergyRequest;
    // EVMinimumEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumEnergyRequest;
    // EVMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower;
    // EVMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L2;
    unsigned int EVMaximumChargePower_L2_isUsed:1;
    // EVMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumChargePower_L3;
    unsigned int EVMaximumChargePower_L3_isUsed:1;
    // EVMinimumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower;
    // EVMinimumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L2;
    unsigned int EVMinimumChargePower_L2_isUsed:1;
    // EVMinimumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumChargePower_L3;
    unsigned int EVMinimumChargePower_L3_isUsed:1;
    // EVPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower;
    // EVPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L2;
    unsigned int EVPresentActivePower_L2_isUsed:1;
    // EVPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentActivePower_L3;
    unsigned int EVPresentActivePower_L3_isUsed:1;
    // EVPresentReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower;
    // EVPresentReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L2;
    unsigned int EVPresentReactivePower_L2_isUsed:1;
    // EVPresentReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentReactivePower_L3;
    unsigned int EVPresentReactivePower_L3_isUsed:1;
    // EVMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower;
    // EVMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L2;
    unsigned int EVMaximumDischargePower_L2_isUsed:1;
    // EVMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumDischargePower_L3;
    unsigned int EVMaximumDischargePower_L3_isUsed:1;
    // EVMinimumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower;
    // EVMinimumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L2;
    unsigned int EVMinimumDischargePower_L2_isUsed:1;
    // EVMinimumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumDischargePower_L3;
    unsigned int EVMinimumDischargePower_L3_isUsed:1;
    // EVPresentVoltage, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentVoltage;
    // EVPresentFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVPresentFrequency;
    // EVSessionTotalDischargeEnergyAvailable, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSessionTotalDischargeEnergyAvailable;
    unsigned int EVSessionTotalDischargeEnergyAvailable_isUsed:1;
    // EVApparentPower, EVApparentPowerType
    struct iso20_ac_der_sae_EVApparentPowerType EVApparentPower;
    unsigned int EVApparentPower_isUsed:1;
    // EVReactivePower, EVReactivePowerType
    struct iso20_ac_der_sae_EVReactivePowerType EVReactivePower;
    unsigned int EVReactivePower_isUsed:1;
    // EVExcitation, EVExcitationType
    struct iso20_ac_der_sae_EVExcitationType EVExcitation;
    unsigned int EVExcitation_isUsed:1;
    // EVMaximumV2XEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMaximumV2XEnergyRequest;
    unsigned int EVMaximumV2XEnergyRequest_isUsed:1;
    // EVMinimumV2XEnergyRequest, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVMinimumV2XEnergyRequest;
    unsigned int EVMinimumV2XEnergyRequest_isUsed:1;
    // DEROperationalState, derOperationalStateType (base: string)
    iso20_ac_der_sae_derOperationalStateType DEROperationalState;
    // DERConnectionStatus, derConnectionStatusType (base: string)
    iso20_ac_der_sae_derConnectionStatusType DERConnectionStatus;
    // EVUpdateTime, unsignedLong (base: nonNegativeInteger)
    uint64_t EVUpdateTime;
    // EVMinimumChargingDuration, unsignedInt (base: unsignedLong)
    uint32_t EVMinimumChargingDuration;
    // EVDurationMaximumChargeRate, unsignedInt (base: unsignedLong)
    uint32_t EVDurationMaximumChargeRate;
    // EVDurationMaximumDischargeRate, unsignedInt (base: unsignedLong)
    uint32_t EVDurationMaximumDischargeRate;
    // DERAlarmStatus, unsignedInt (base: unsignedLong)
    uint32_t DERAlarmStatus;
    // EnabledModes, unsignedInt (base: unsignedLong)
    uint32_t EnabledModes;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeLoopReq; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeLoopReqType; base type=ChargeLoopReqType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: Header, MessageHeaderType (1, 1); DisplayParameters, DisplayParametersType (0, 1); MeterInfoRequested, boolean (1, 1); BPT_Dynamic_AC_CLReqControlMode, BPT_Dynamic_AC_CLReqControlModeType (0, 1); BPT_Scheduled_AC_CLReqControlMode, BPT_Scheduled_AC_CLReqControlModeType (0, 1); CLReqControlMode, CLReqControlModeType (0, 1); DER_Dynamic_AC_CLReqControlMode, DER_Dynamic_AC_CLReqControlModeType (0, 1); DER_Scheduled_AC_CLReqControlMode, DER_Scheduled_AC_CLReqControlModeType (0, 1); Dynamic_AC_CLReqControlMode, Dynamic_AC_CLReqControlModeType (0, 1); Scheduled_AC_CLReqControlMode, Scheduled_AC_CLReqControlModeType (0, 1);
struct iso20_ac_der_sae_AC_ChargeLoopReqType {
    // Header, MessageHeaderType
    struct iso20_ac_der_sae_MessageHeaderType Header;
    // DisplayParameters, DisplayParametersType
    struct iso20_ac_der_sae_DisplayParametersType DisplayParameters;
    unsigned int DisplayParameters_isUsed:1;
    // MeterInfoRequested, boolean
    int MeterInfoRequested;
    // BPT_Dynamic_AC_CLReqControlMode, BPT_Dynamic_AC_CLReqControlModeType (base: Dynamic_AC_CLReqControlModeType)
    struct iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType BPT_Dynamic_AC_CLReqControlMode;
    unsigned int BPT_Dynamic_AC_CLReqControlMode_isUsed:1;
    // BPT_Scheduled_AC_CLReqControlMode, BPT_Scheduled_AC_CLReqControlModeType (base: Scheduled_AC_CLReqControlModeType)
    struct iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType BPT_Scheduled_AC_CLReqControlMode;
    unsigned int BPT_Scheduled_AC_CLReqControlMode_isUsed:1;
    // CLReqControlMode, CLReqControlModeType
    struct iso20_ac_der_sae_CLReqControlModeType CLReqControlMode;
    unsigned int CLReqControlMode_isUsed:1;
    // DER_Dynamic_AC_CLReqControlMode, DER_Dynamic_AC_CLReqControlModeType (base: Dynamic_AC_CLReqControlModeType)
    struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType DER_Dynamic_AC_CLReqControlMode;
    unsigned int DER_Dynamic_AC_CLReqControlMode_isUsed:1;
    // DER_Scheduled_AC_CLReqControlMode, DER_Scheduled_AC_CLReqControlModeType (base: Scheduled_AC_CLReqControlModeType)
    struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType DER_Scheduled_AC_CLReqControlMode;
    unsigned int DER_Scheduled_AC_CLReqControlMode_isUsed:1;
    // Dynamic_AC_CLReqControlMode, Dynamic_AC_CLReqControlModeType (base: Dynamic_CLReqControlModeType)
    struct iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType Dynamic_AC_CLReqControlMode;
    unsigned int Dynamic_AC_CLReqControlMode_isUsed:1;
    // Scheduled_AC_CLReqControlMode, Scheduled_AC_CLReqControlModeType (base: Scheduled_CLReqControlModeType)
    struct iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType Scheduled_AC_CLReqControlMode;
    unsigned int Scheduled_AC_CLReqControlMode_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Dynamic_AC_CLResControlMode; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}DER_Dynamic_AC_CLResControlModeType; base type=Dynamic_AC_CLResControlModeType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: DepartureTime, unsignedInt (0, 1); MinimumSOC, percentValueType (0, 1); TargetSOC, percentValueType (0, 1); AckMaxDelay, unsignedShort (0, 1); EVSETargetActivePower, RationalNumberType (1, 1); EVSETargetActivePower_L2, RationalNumberType (0, 1); EVSETargetActivePower_L3, RationalNumberType (0, 1); EVSETargetReactivePower, RationalNumberType (0, 1); EVSETargetReactivePower_L2, RationalNumberType (0, 1); EVSETargetReactivePower_L3, RationalNumberType (0, 1); EVSEPresentActivePower, RationalNumberType (0, 1); EVSEPresentActivePower_L2, RationalNumberType (0, 1); EVSEPresentActivePower_L3, RationalNumberType (0, 1); DERControlCLRes, DERControlCLResType (1, 1); EVSEMaximumChargePower, RationalNumberType (0, 1); EVSEMaximumChargePower_L2, RationalNumberType (0, 1); EVSEMaximumChargePower_L3, RationalNumberType (0, 1); EVSEMaximumDischargePower, RationalNumberType (0, 1); EVSEMaximumDischargePower_L2, RationalNumberType (0, 1); EVSEMaximumDischargePower_L3, RationalNumberType (0, 1); RequiredDEROperatingMode, requiredDEROperatingModeType (0, 1); GridConnectionMode, gridConnectionModeType (0, 1);
struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType {
    // DepartureTime, unsignedInt (base: unsignedLong)
    uint32_t DepartureTime;
    unsigned int DepartureTime_isUsed:1;
    // MinimumSOC, percentValueType (base: byte)
    int8_t MinimumSOC;
    unsigned int MinimumSOC_isUsed:1;
    // TargetSOC, percentValueType (base: byte)
    int8_t TargetSOC;
    unsigned int TargetSOC_isUsed:1;
    // AckMaxDelay, unsignedShort (base: unsignedInt)
    uint16_t AckMaxDelay;
    unsigned int AckMaxDelay_isUsed:1;
    // EVSETargetActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower;
    // EVSETargetActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L2;
    unsigned int EVSETargetActivePower_L2_isUsed:1;
    // EVSETargetActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetActivePower_L3;
    unsigned int EVSETargetActivePower_L3_isUsed:1;
    // EVSETargetReactivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower;
    unsigned int EVSETargetReactivePower_isUsed:1;
    // EVSETargetReactivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L2;
    unsigned int EVSETargetReactivePower_L2_isUsed:1;
    // EVSETargetReactivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetReactivePower_L3;
    unsigned int EVSETargetReactivePower_L3_isUsed:1;
    // EVSEPresentActivePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower;
    unsigned int EVSEPresentActivePower_isUsed:1;
    // EVSEPresentActivePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L2;
    unsigned int EVSEPresentActivePower_L2_isUsed:1;
    // EVSEPresentActivePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEPresentActivePower_L3;
    unsigned int EVSEPresentActivePower_L3_isUsed:1;
    // DERControlCLRes, DERControlCLResType
    struct iso20_ac_der_sae_DERControlCLResType DERControlCLRes;
    // EVSEMaximumChargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower;
    unsigned int EVSEMaximumChargePower_isUsed:1;
    // EVSEMaximumChargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L2;
    unsigned int EVSEMaximumChargePower_L2_isUsed:1;
    // EVSEMaximumChargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumChargePower_L3;
    unsigned int EVSEMaximumChargePower_L3_isUsed:1;
    // EVSEMaximumDischargePower, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower;
    unsigned int EVSEMaximumDischargePower_isUsed:1;
    // EVSEMaximumDischargePower_L2, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L2;
    unsigned int EVSEMaximumDischargePower_L2_isUsed:1;
    // EVSEMaximumDischargePower_L3, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSEMaximumDischargePower_L3;
    unsigned int EVSEMaximumDischargePower_L3_isUsed:1;
    // RequiredDEROperatingMode, requiredDEROperatingModeType (base: string)
    iso20_ac_der_sae_requiredDEROperatingModeType RequiredDEROperatingMode;
    unsigned int RequiredDEROperatingMode_isUsed:1;
    // GridConnectionMode, gridConnectionModeType (base: string)
    iso20_ac_der_sae_gridConnectionModeType GridConnectionMode;
    unsigned int GridConnectionMode_isUsed:1;

};

// Element: definition=complex; name={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeLoopRes; type={urn:iso:std:iso:15118:-20:AC-DER-SAE}AC_ChargeLoopResType; base type=ChargeLoopResType; content type=ELEMENT-ONLY;
//          abstract=False; final=False; derivation=extension;
// Particle: Header, MessageHeaderType (1, 1); ResponseCode, responseCodeType (1, 1); EVSEStatus, EVSEStatusType (0, 1); MeterInfo, MeterInfoType (0, 1); Receipt, ReceiptType (0, 1); EVSETargetFrequency, RationalNumberType (0, 1); BPT_Dynamic_AC_CLResControlMode, BPT_Dynamic_AC_CLResControlModeType (0, 1); BPT_Scheduled_AC_CLResControlMode, BPT_Scheduled_AC_CLResControlModeType (0, 1); CLResControlMode, CLResControlModeType (0, 1); DER_Dynamic_AC_CLResControlMode, DER_Dynamic_AC_CLResControlModeType (0, 1); DER_Scheduled_AC_CLResControlMode, DER_Scheduled_AC_CLResControlModeType (0, 1); Dynamic_AC_CLResControlMode, Dynamic_AC_CLResControlModeType (0, 1); Scheduled_AC_CLResControlMode, Scheduled_AC_CLResControlModeType (0, 1);
struct iso20_ac_der_sae_AC_ChargeLoopResType {
    // Header, MessageHeaderType
    struct iso20_ac_der_sae_MessageHeaderType Header;
    // ResponseCode, responseCodeType (base: string)
    iso20_ac_der_sae_responseCodeType ResponseCode;
    // EVSEStatus, EVSEStatusType
    struct iso20_ac_der_sae_EVSEStatusType EVSEStatus;
    unsigned int EVSEStatus_isUsed:1;
    // MeterInfo, MeterInfoType
    struct iso20_ac_der_sae_MeterInfoType MeterInfo;
    unsigned int MeterInfo_isUsed:1;
    // Receipt, ReceiptType
    struct iso20_ac_der_sae_ReceiptType Receipt;
    unsigned int Receipt_isUsed:1;
    // EVSETargetFrequency, RationalNumberType
    struct iso20_ac_der_sae_RationalNumberType EVSETargetFrequency;
    unsigned int EVSETargetFrequency_isUsed:1;
    // BPT_Dynamic_AC_CLResControlMode, BPT_Dynamic_AC_CLResControlModeType (base: Dynamic_AC_CLResControlModeType)
    struct iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType BPT_Dynamic_AC_CLResControlMode;
    unsigned int BPT_Dynamic_AC_CLResControlMode_isUsed:1;
    // BPT_Scheduled_AC_CLResControlMode, BPT_Scheduled_AC_CLResControlModeType (base: Scheduled_AC_CLResControlModeType)
    struct iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType BPT_Scheduled_AC_CLResControlMode;
    unsigned int BPT_Scheduled_AC_CLResControlMode_isUsed:1;
    // CLResControlMode, CLResControlModeType
    struct iso20_ac_der_sae_CLResControlModeType CLResControlMode;
    unsigned int CLResControlMode_isUsed:1;
    // DER_Dynamic_AC_CLResControlMode, DER_Dynamic_AC_CLResControlModeType (base: Dynamic_AC_CLResControlModeType)
    struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType DER_Dynamic_AC_CLResControlMode;
    unsigned int DER_Dynamic_AC_CLResControlMode_isUsed:1;
    // DER_Scheduled_AC_CLResControlMode, DER_Scheduled_AC_CLResControlModeType (base: Scheduled_AC_CLResControlModeType)
    struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType DER_Scheduled_AC_CLResControlMode;
    unsigned int DER_Scheduled_AC_CLResControlMode_isUsed:1;
    // Dynamic_AC_CLResControlMode, Dynamic_AC_CLResControlModeType (base: Dynamic_CLResControlModeType)
    struct iso20_ac_der_sae_Dynamic_AC_CLResControlModeType Dynamic_AC_CLResControlMode;
    unsigned int Dynamic_AC_CLResControlMode_isUsed:1;
    // Scheduled_AC_CLResControlMode, Scheduled_AC_CLResControlModeType (base: Scheduled_CLResControlModeType)
    struct iso20_ac_der_sae_Scheduled_AC_CLResControlModeType Scheduled_AC_CLResControlMode;
    unsigned int Scheduled_AC_CLResControlMode_isUsed:1;

};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}Manifest; type={http://www.w3.org/2000/09/xmldsig#}ManifestType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Id, ID (0, 1); Reference, ReferenceType (1, 4) (original max unbounded);
struct iso20_ac_der_sae_ManifestType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // Reference, ReferenceType
    struct {
        struct iso20_ac_der_sae_ReferenceType array[iso20_ac_der_sae_ReferenceType_4_ARRAY_SIZE];
        uint16_t arrayLen;
    } Reference;
};

// Element: definition=complex; name={http://www.w3.org/2000/09/xmldsig#}SignatureProperties; type={http://www.w3.org/2000/09/xmldsig#}SignaturePropertiesType; base type=; content type=ELEMENT-ONLY;
//          abstract=False; final=False;
// Particle: Id, ID (0, 1); SignatureProperty, SignaturePropertyType (1, 1) (original max unbounded);
struct iso20_ac_der_sae_SignaturePropertiesType {
    // Attribute: Id, ID (base: NCName)
    struct {
        char characters[iso20_ac_der_sae_Id_CHARACTER_SIZE];
        uint16_t charactersLen;
    } Id;
    unsigned int Id_isUsed:1;
    // SignatureProperty, SignaturePropertyType
    struct iso20_ac_der_sae_SignaturePropertyType SignatureProperty;

};



// root elements of EXI doc
struct iso20_ac_der_sae_exiDocument {
    union {
        struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType AC_ChargeParameterDiscoveryReq;
        struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType AC_ChargeParameterDiscoveryRes;
        struct iso20_ac_der_sae_AC_ChargeLoopReqType AC_ChargeLoopReq;
        struct iso20_ac_der_sae_AC_ChargeLoopResType AC_ChargeLoopRes;
        struct iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType AC_CPDReqEnergyTransferMode;
        struct iso20_ac_der_sae_AC_CPDResEnergyTransferModeType AC_CPDResEnergyTransferMode;
        struct iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType BPT_AC_CPDReqEnergyTransferMode;
        struct iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType BPT_AC_CPDResEnergyTransferMode;
        struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType DER_AC_CPDReqEnergyTransferMode;
        struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType DER_AC_CPDResEnergyTransferMode;
        struct iso20_ac_der_sae_CLReqControlModeType CLReqControlMode;
        struct iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType Scheduled_AC_CLReqControlMode;
        struct iso20_ac_der_sae_CLResControlModeType CLResControlMode;
        struct iso20_ac_der_sae_Scheduled_AC_CLResControlModeType Scheduled_AC_CLResControlMode;
        struct iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType BPT_Scheduled_AC_CLReqControlMode;
        struct iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType BPT_Scheduled_AC_CLResControlMode;
        struct iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType Dynamic_AC_CLReqControlMode;
        struct iso20_ac_der_sae_Dynamic_AC_CLResControlModeType Dynamic_AC_CLResControlMode;
        struct iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType BPT_Dynamic_AC_CLReqControlMode;
        struct iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType BPT_Dynamic_AC_CLResControlMode;
        struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType DER_Scheduled_AC_CLReqControlMode;
        struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType DER_Scheduled_AC_CLResControlMode;
        struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType DER_Dynamic_AC_CLReqControlMode;
        struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType DER_Dynamic_AC_CLResControlMode;
        struct iso20_ac_der_sae_FrequencyDroopType FrequencyDroop;
        struct iso20_ac_der_sae_SignatureType Signature;
        struct iso20_ac_der_sae_SignatureValueType SignatureValue;
        struct iso20_ac_der_sae_SignedInfoType SignedInfo;
        struct iso20_ac_der_sae_CanonicalizationMethodType CanonicalizationMethod;
        struct iso20_ac_der_sae_SignatureMethodType SignatureMethod;
        struct iso20_ac_der_sae_ReferenceType Reference;
        struct iso20_ac_der_sae_TransformsType Transforms;
        struct iso20_ac_der_sae_TransformType Transform;
        struct iso20_ac_der_sae_DigestMethodType DigestMethod;
        struct iso20_ac_der_sae_KeyInfoType KeyInfo;
        struct iso20_ac_der_sae_KeyValueType KeyValue;
        struct iso20_ac_der_sae_RetrievalMethodType RetrievalMethod;
        struct iso20_ac_der_sae_X509DataType X509Data;
        struct iso20_ac_der_sae_PGPDataType PGPData;
        struct iso20_ac_der_sae_SPKIDataType SPKIData;
        struct iso20_ac_der_sae_ObjectType Object;
        struct iso20_ac_der_sae_ManifestType Manifest;
        struct iso20_ac_der_sae_SignaturePropertiesType SignatureProperties;
        struct iso20_ac_der_sae_SignaturePropertyType SignatureProperty;
        struct iso20_ac_der_sae_DSAKeyValueType DSAKeyValue;
        struct iso20_ac_der_sae_RSAKeyValueType RSAKeyValue;
    };
    unsigned int AC_ChargeParameterDiscoveryReq_isUsed:1;
    unsigned int AC_ChargeParameterDiscoveryRes_isUsed:1;
    unsigned int AC_ChargeLoopReq_isUsed:1;
    unsigned int AC_ChargeLoopRes_isUsed:1;
    unsigned int AC_CPDReqEnergyTransferMode_isUsed:1;
    unsigned int AC_CPDResEnergyTransferMode_isUsed:1;
    unsigned int BPT_AC_CPDReqEnergyTransferMode_isUsed:1;
    unsigned int BPT_AC_CPDResEnergyTransferMode_isUsed:1;
    unsigned int DER_AC_CPDReqEnergyTransferMode_isUsed:1;
    unsigned int DER_AC_CPDResEnergyTransferMode_isUsed:1;
    unsigned int CLReqControlMode_isUsed:1;
    unsigned int Scheduled_AC_CLReqControlMode_isUsed:1;
    unsigned int CLResControlMode_isUsed:1;
    unsigned int Scheduled_AC_CLResControlMode_isUsed:1;
    unsigned int BPT_Scheduled_AC_CLReqControlMode_isUsed:1;
    unsigned int BPT_Scheduled_AC_CLResControlMode_isUsed:1;
    unsigned int Dynamic_AC_CLReqControlMode_isUsed:1;
    unsigned int Dynamic_AC_CLResControlMode_isUsed:1;
    unsigned int BPT_Dynamic_AC_CLReqControlMode_isUsed:1;
    unsigned int BPT_Dynamic_AC_CLResControlMode_isUsed:1;
    unsigned int DER_Scheduled_AC_CLReqControlMode_isUsed:1;
    unsigned int DER_Scheduled_AC_CLResControlMode_isUsed:1;
    unsigned int DER_Dynamic_AC_CLReqControlMode_isUsed:1;
    unsigned int DER_Dynamic_AC_CLResControlMode_isUsed:1;
    unsigned int FrequencyDroop_isUsed:1;
    unsigned int Signature_isUsed:1;
    unsigned int SignatureValue_isUsed:1;
    unsigned int SignedInfo_isUsed:1;
    unsigned int CanonicalizationMethod_isUsed:1;
    unsigned int SignatureMethod_isUsed:1;
    unsigned int Reference_isUsed:1;
    unsigned int Transforms_isUsed:1;
    unsigned int Transform_isUsed:1;
    unsigned int DigestMethod_isUsed:1;
    unsigned int KeyInfo_isUsed:1;
    unsigned int KeyValue_isUsed:1;
    unsigned int RetrievalMethod_isUsed:1;
    unsigned int X509Data_isUsed:1;
    unsigned int PGPData_isUsed:1;
    unsigned int SPKIData_isUsed:1;
    unsigned int Object_isUsed:1;
    unsigned int Manifest_isUsed:1;
    unsigned int SignatureProperties_isUsed:1;
    unsigned int SignatureProperty_isUsed:1;
    unsigned int DSAKeyValue_isUsed:1;
    unsigned int RSAKeyValue_isUsed:1;
};

// elements of EXI fragment
struct iso20_ac_der_sae_exiFragment {
    union {
        struct iso20_ac_der_sae_SignedInfoType SignedInfo;
    };
    unsigned int SignedInfo_isUsed:1;
};

// elements of xmldsig fragment
struct iso20_ac_der_sae_xmldsigFragment {
    union {
        struct iso20_ac_der_sae_CanonicalizationMethodType CanonicalizationMethod;
        struct iso20_ac_der_sae_DSAKeyValueType DSAKeyValue;
        struct iso20_ac_der_sae_DigestMethodType DigestMethod;
        struct iso20_ac_der_sae_KeyInfoType KeyInfo;
        struct iso20_ac_der_sae_KeyValueType KeyValue;
        struct iso20_ac_der_sae_ManifestType Manifest;
        struct iso20_ac_der_sae_ObjectType Object;
        struct iso20_ac_der_sae_PGPDataType PGPData;
        struct iso20_ac_der_sae_RSAKeyValueType RSAKeyValue;
        struct iso20_ac_der_sae_ReferenceType Reference;
        struct iso20_ac_der_sae_RetrievalMethodType RetrievalMethod;
        struct iso20_ac_der_sae_SPKIDataType SPKIData;
        struct iso20_ac_der_sae_SignatureType Signature;
        struct iso20_ac_der_sae_SignatureMethodType SignatureMethod;
        struct iso20_ac_der_sae_SignaturePropertiesType SignatureProperties;
        struct iso20_ac_der_sae_SignaturePropertyType SignatureProperty;
        struct iso20_ac_der_sae_SignatureValueType SignatureValue;
        struct iso20_ac_der_sae_SignedInfoType SignedInfo;
        struct iso20_ac_der_sae_TransformType Transform;
        struct iso20_ac_der_sae_TransformsType Transforms;
        struct iso20_ac_der_sae_X509DataType X509Data;
        struct iso20_ac_der_sae_X509IssuerSerialType X509IssuerSerial;
    };
    unsigned int CanonicalizationMethod_isUsed:1;
    unsigned int DSAKeyValue_isUsed:1;
    unsigned int DigestMethod_isUsed:1;
    unsigned int KeyInfo_isUsed:1;
    unsigned int KeyValue_isUsed:1;
    unsigned int Manifest_isUsed:1;
    unsigned int Object_isUsed:1;
    unsigned int PGPData_isUsed:1;
    unsigned int RSAKeyValue_isUsed:1;
    unsigned int Reference_isUsed:1;
    unsigned int RetrievalMethod_isUsed:1;
    unsigned int SPKIData_isUsed:1;
    unsigned int Signature_isUsed:1;
    unsigned int SignatureMethod_isUsed:1;
    unsigned int SignatureProperties_isUsed:1;
    unsigned int SignatureProperty_isUsed:1;
    unsigned int SignatureValue_isUsed:1;
    unsigned int SignedInfo_isUsed:1;
    unsigned int Transform_isUsed:1;
    unsigned int Transforms_isUsed:1;
    unsigned int X509Data_isUsed:1;
    unsigned int X509IssuerSerial_isUsed:1;
};

// init for structs
void init_iso20_ac_der_sae_exiDocument(struct iso20_ac_der_sae_exiDocument* exiDoc);
void init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType(struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryReqType* AC_ChargeParameterDiscoveryReq);
void init_iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType(struct iso20_ac_der_sae_AC_ChargeParameterDiscoveryResType* AC_ChargeParameterDiscoveryRes);
void init_iso20_ac_der_sae_AC_ChargeLoopReqType(struct iso20_ac_der_sae_AC_ChargeLoopReqType* AC_ChargeLoopReq);
void init_iso20_ac_der_sae_AC_ChargeLoopResType(struct iso20_ac_der_sae_AC_ChargeLoopResType* AC_ChargeLoopRes);
void init_iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_AC_CPDReqEnergyTransferModeType* AC_CPDReqEnergyTransferMode);
void init_iso20_ac_der_sae_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_AC_CPDResEnergyTransferModeType* AC_CPDResEnergyTransferMode);
void init_iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_BPT_AC_CPDReqEnergyTransferModeType* BPT_AC_CPDReqEnergyTransferMode);
void init_iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_BPT_AC_CPDResEnergyTransferModeType* BPT_AC_CPDResEnergyTransferMode);
void init_iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType(struct iso20_ac_der_sae_DER_AC_CPDReqEnergyTransferModeType* DER_AC_CPDReqEnergyTransferMode);
void init_iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType(struct iso20_ac_der_sae_DER_AC_CPDResEnergyTransferModeType* DER_AC_CPDResEnergyTransferMode);
void init_iso20_ac_der_sae_CLReqControlModeType(struct iso20_ac_der_sae_CLReqControlModeType* CLReqControlMode);
void init_iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_Scheduled_AC_CLReqControlModeType* Scheduled_AC_CLReqControlMode);
void init_iso20_ac_der_sae_CLResControlModeType(struct iso20_ac_der_sae_CLResControlModeType* CLResControlMode);
void init_iso20_ac_der_sae_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_Scheduled_AC_CLResControlModeType* Scheduled_AC_CLResControlMode);
void init_iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_BPT_Scheduled_AC_CLReqControlModeType* BPT_Scheduled_AC_CLReqControlMode);
void init_iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_BPT_Scheduled_AC_CLResControlModeType* BPT_Scheduled_AC_CLResControlMode);
void init_iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_Dynamic_AC_CLReqControlModeType* Dynamic_AC_CLReqControlMode);
void init_iso20_ac_der_sae_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_Dynamic_AC_CLResControlModeType* Dynamic_AC_CLResControlMode);
void init_iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_BPT_Dynamic_AC_CLReqControlModeType* BPT_Dynamic_AC_CLReqControlMode);
void init_iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_BPT_Dynamic_AC_CLResControlModeType* BPT_Dynamic_AC_CLResControlMode);
void init_iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType(struct iso20_ac_der_sae_DER_Scheduled_AC_CLReqControlModeType* DER_Scheduled_AC_CLReqControlMode);
void init_iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType(struct iso20_ac_der_sae_DER_Scheduled_AC_CLResControlModeType* DER_Scheduled_AC_CLResControlMode);
void init_iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType(struct iso20_ac_der_sae_DER_Dynamic_AC_CLReqControlModeType* DER_Dynamic_AC_CLReqControlMode);
void init_iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType(struct iso20_ac_der_sae_DER_Dynamic_AC_CLResControlModeType* DER_Dynamic_AC_CLResControlMode);
void init_iso20_ac_der_sae_FrequencyDroopType(struct iso20_ac_der_sae_FrequencyDroopType* FrequencyDroop);
void init_iso20_ac_der_sae_SignatureType(struct iso20_ac_der_sae_SignatureType* Signature);
void init_iso20_ac_der_sae_SignatureValueType(struct iso20_ac_der_sae_SignatureValueType* SignatureValue);
void init_iso20_ac_der_sae_SignedInfoType(struct iso20_ac_der_sae_SignedInfoType* SignedInfo);
void init_iso20_ac_der_sae_CanonicalizationMethodType(struct iso20_ac_der_sae_CanonicalizationMethodType* CanonicalizationMethod);
void init_iso20_ac_der_sae_SignatureMethodType(struct iso20_ac_der_sae_SignatureMethodType* SignatureMethod);
void init_iso20_ac_der_sae_ReferenceType(struct iso20_ac_der_sae_ReferenceType* Reference);
void init_iso20_ac_der_sae_TransformsType(struct iso20_ac_der_sae_TransformsType* Transforms);
void init_iso20_ac_der_sae_TransformType(struct iso20_ac_der_sae_TransformType* Transform);
void init_iso20_ac_der_sae_DigestMethodType(struct iso20_ac_der_sae_DigestMethodType* DigestMethod);
void init_iso20_ac_der_sae_KeyInfoType(struct iso20_ac_der_sae_KeyInfoType* KeyInfo);
void init_iso20_ac_der_sae_KeyValueType(struct iso20_ac_der_sae_KeyValueType* KeyValue);
void init_iso20_ac_der_sae_RetrievalMethodType(struct iso20_ac_der_sae_RetrievalMethodType* RetrievalMethod);
void init_iso20_ac_der_sae_X509DataType(struct iso20_ac_der_sae_X509DataType* X509Data);
void init_iso20_ac_der_sae_PGPDataType(struct iso20_ac_der_sae_PGPDataType* PGPData);
void init_iso20_ac_der_sae_SPKIDataType(struct iso20_ac_der_sae_SPKIDataType* SPKIData);
void init_iso20_ac_der_sae_ObjectType(struct iso20_ac_der_sae_ObjectType* Object);
void init_iso20_ac_der_sae_ManifestType(struct iso20_ac_der_sae_ManifestType* Manifest);
void init_iso20_ac_der_sae_SignaturePropertiesType(struct iso20_ac_der_sae_SignaturePropertiesType* SignatureProperties);
void init_iso20_ac_der_sae_SignaturePropertyType(struct iso20_ac_der_sae_SignaturePropertyType* SignatureProperty);
void init_iso20_ac_der_sae_DSAKeyValueType(struct iso20_ac_der_sae_DSAKeyValueType* DSAKeyValue);
void init_iso20_ac_der_sae_RSAKeyValueType(struct iso20_ac_der_sae_RSAKeyValueType* RSAKeyValue);
void init_iso20_ac_der_sae_X509IssuerSerialType(struct iso20_ac_der_sae_X509IssuerSerialType* X509IssuerSerialType);
void init_iso20_ac_der_sae_DataTupleType(struct iso20_ac_der_sae_DataTupleType* DataTupleType);
void init_iso20_ac_der_sae_FrequencyDroopSettingsType(struct iso20_ac_der_sae_FrequencyDroopSettingsType* FrequencyDroopSettingsType);
void init_iso20_ac_der_sae_CurveDataPointsListType(struct iso20_ac_der_sae_CurveDataPointsListType* CurveDataPointsListType);
void init_iso20_ac_der_sae_DERCurveType(struct iso20_ac_der_sae_DERCurveType* DERCurveType);
void init_iso20_ac_der_sae_ConstantPowerFactorType(struct iso20_ac_der_sae_ConstantPowerFactorType* ConstantPowerFactorType);
void init_iso20_ac_der_sae_VoltVarType(struct iso20_ac_der_sae_VoltVarType* VoltVarType);
void init_iso20_ac_der_sae_VoltWattType(struct iso20_ac_der_sae_VoltWattType* VoltWattType);
void init_iso20_ac_der_sae_WattVarType(struct iso20_ac_der_sae_WattVarType* WattVarType);
void init_iso20_ac_der_sae_ConstantWattType(struct iso20_ac_der_sae_ConstantWattType* ConstantWattType);
void init_iso20_ac_der_sae_ConstantVarType(struct iso20_ac_der_sae_ConstantVarType* ConstantVarType);
void init_iso20_ac_der_sae_LimitMaxDischargePowerType(struct iso20_ac_der_sae_LimitMaxDischargePowerType* LimitMaxDischargePowerType);
void init_iso20_ac_der_sae_RationalNumberType(struct iso20_ac_der_sae_RationalNumberType* RationalNumberType);
void init_iso20_ac_der_sae_VoltageTripType(struct iso20_ac_der_sae_VoltageTripType* VoltageTripType);
void init_iso20_ac_der_sae_DetailedCostType(struct iso20_ac_der_sae_DetailedCostType* DetailedCostType);
void init_iso20_ac_der_sae_FrequencyTripType(struct iso20_ac_der_sae_FrequencyTripType* FrequencyTripType);
void init_iso20_ac_der_sae_EnterServiceCPDResType(struct iso20_ac_der_sae_EnterServiceCPDResType* EnterServiceCPDResType);
void init_iso20_ac_der_sae_EnterServiceCLResType(struct iso20_ac_der_sae_EnterServiceCLResType* EnterServiceCLResType);
void init_iso20_ac_der_sae_ReactivePowerSupportCPDResType(struct iso20_ac_der_sae_ReactivePowerSupportCPDResType* ReactivePowerSupportCPDResType);
void init_iso20_ac_der_sae_ReactivePowerSupportCLResType(struct iso20_ac_der_sae_ReactivePowerSupportCLResType* ReactivePowerSupportCLResType);
void init_iso20_ac_der_sae_ActivePowerSupportCPDResType(struct iso20_ac_der_sae_ActivePowerSupportCPDResType* ActivePowerSupportCPDResType);
void init_iso20_ac_der_sae_ActivePowerSupportCLResType(struct iso20_ac_der_sae_ActivePowerSupportCLResType* ActivePowerSupportCLResType);
void init_iso20_ac_der_sae_DetailedTaxType(struct iso20_ac_der_sae_DetailedTaxType* DetailedTaxType);
void init_iso20_ac_der_sae_MessageHeaderType(struct iso20_ac_der_sae_MessageHeaderType* MessageHeaderType);
void init_iso20_ac_der_sae_DisplayParametersType(struct iso20_ac_der_sae_DisplayParametersType* DisplayParametersType);
void init_iso20_ac_der_sae_EVSEStatusType(struct iso20_ac_der_sae_EVSEStatusType* EVSEStatusType);
void init_iso20_ac_der_sae_MeterInfoType(struct iso20_ac_der_sae_MeterInfoType* MeterInfoType);
void init_iso20_ac_der_sae_ReceiptType(struct iso20_ac_der_sae_ReceiptType* ReceiptType);
void init_iso20_ac_der_sae_DERControlCLResType(struct iso20_ac_der_sae_DERControlCLResType* DERControlCLResType);
void init_iso20_ac_der_sae_EVApparentPowerLimitsType(struct iso20_ac_der_sae_EVApparentPowerLimitsType* EVApparentPowerLimitsType);
void init_iso20_ac_der_sae_DERControlCPDResType(struct iso20_ac_der_sae_DERControlCPDResType* DERControlCPDResType);
void init_iso20_ac_der_sae_EVReactivePowerLimitsType(struct iso20_ac_der_sae_EVReactivePowerLimitsType* EVReactivePowerLimitsType);
void init_iso20_ac_der_sae_EVExcitationLimitsType(struct iso20_ac_der_sae_EVExcitationLimitsType* EVExcitationLimitsType);
void init_iso20_ac_der_sae_EVInverterDetailsType(struct iso20_ac_der_sae_EVInverterDetailsType* EVInverterDetailsType);
void init_iso20_ac_der_sae_EVSEReactivePowerLimitsType(struct iso20_ac_der_sae_EVSEReactivePowerLimitsType* EVSEReactivePowerLimitsType);
void init_iso20_ac_der_sae_GridLimitsType(struct iso20_ac_der_sae_GridLimitsType* GridLimitsType);
void init_iso20_ac_der_sae_EVApparentPowerType(struct iso20_ac_der_sae_EVApparentPowerType* EVApparentPowerType);
void init_iso20_ac_der_sae_EVReactivePowerType(struct iso20_ac_der_sae_EVReactivePowerType* EVReactivePowerType);
void init_iso20_ac_der_sae_EVExcitationType(struct iso20_ac_der_sae_EVExcitationType* EVExcitationType);
void init_iso20_ac_der_sae_exiFragment(struct iso20_ac_der_sae_exiFragment* exiFrag);
void init_iso20_ac_der_sae_xmldsigFragment(struct iso20_ac_der_sae_xmldsigFragment* xmldsigFrag);


#ifdef __cplusplus
}
#endif

#endif /* ISO20_AC_DER_SAE_DATATYPES_H */

