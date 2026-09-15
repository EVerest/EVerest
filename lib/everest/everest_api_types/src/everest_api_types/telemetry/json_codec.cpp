// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "telemetry/json_codec.hpp"
#include "nlohmann/json.hpp"
#include <stdexcept>
#include <string>

namespace everest::lib::API::V1_0::types::telemetry {

void to_json(json& j, V2gDin70121CommunicationState const& k) noexcept {
    switch (k) {
    case V2gDin70121CommunicationState::WaitForSessionSetup:
        j = "WaitForSessionSetup";
        return;
    case V2gDin70121CommunicationState::WaitForServiceDiscovery:
        j = "WaitForServiceDiscovery";
        return;
    case V2gDin70121CommunicationState::WaitForPaymentServiceSelection:
        j = "WaitForPaymentServiceSelection";
        return;
    case V2gDin70121CommunicationState::WaitForAuthorization:
        j = "WaitForAuthorization";
        return;
    case V2gDin70121CommunicationState::WaitForChargeParameterDiscovery:
        j = "WaitForChargeParameterDiscovery";
        return;
    case V2gDin70121CommunicationState::WaitForCableCheck:
        j = "WaitForCableCheck";
        return;
    case V2gDin70121CommunicationState::WaitForPreCharge:
        j = "WaitForPreCharge";
        return;
    case V2gDin70121CommunicationState::WaitForPreChargePowerDelivery:
        j = "WaitForPreChargePowerDelivery";
        return;
    case V2gDin70121CommunicationState::WaitForCurrentDemand:
        j = "WaitForCurrentDemand";
        return;
    case V2gDin70121CommunicationState::WaitForCurrentDemandPowerDelivery:
        j = "WaitForCurrentDemandPowerDelivery";
        return;
    case V2gDin70121CommunicationState::WaitForWeldingDetectionSessionStop:
        j = "WaitForWeldingDetectionSessionStop";
        return;
    case V2gDin70121CommunicationState::WaitForSessionStop:
        j = "WaitForSessionStop";
        return;
    case V2gDin70121CommunicationState::WaitForTerminatedSession:
        j = "WaitForTerminatedSession";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gDin70121CommunicationState";
}

void from_json(json const& j, V2gDin70121CommunicationState& k) {
    std::string s = j;
    if (s == "WaitForSessionSetup") {
        k = V2gDin70121CommunicationState::WaitForSessionSetup;
        return;
    }
    if (s == "WaitForServiceDiscovery") {
        k = V2gDin70121CommunicationState::WaitForServiceDiscovery;
        return;
    }
    if (s == "WaitForPaymentServiceSelection") {
        k = V2gDin70121CommunicationState::WaitForPaymentServiceSelection;
        return;
    }
    if (s == "WaitForAuthorization") {
        k = V2gDin70121CommunicationState::WaitForAuthorization;
        return;
    }
    if (s == "WaitForChargeParameterDiscovery") {
        k = V2gDin70121CommunicationState::WaitForChargeParameterDiscovery;
        return;
    }
    if (s == "WaitForCableCheck") {
        k = V2gDin70121CommunicationState::WaitForCableCheck;
        return;
    }
    if (s == "WaitForPreCharge") {
        k = V2gDin70121CommunicationState::WaitForPreCharge;
        return;
    }
    if (s == "WaitForPreChargePowerDelivery") {
        k = V2gDin70121CommunicationState::WaitForPreChargePowerDelivery;
        return;
    }
    if (s == "WaitForCurrentDemand") {
        k = V2gDin70121CommunicationState::WaitForCurrentDemand;
        return;
    }
    if (s == "WaitForCurrentDemandPowerDelivery") {
        k = V2gDin70121CommunicationState::WaitForCurrentDemandPowerDelivery;
        return;
    }
    if (s == "WaitForWeldingDetectionSessionStop") {
        k = V2gDin70121CommunicationState::WaitForWeldingDetectionSessionStop;
        return;
    }
    if (s == "WaitForSessionStop") {
        k = V2gDin70121CommunicationState::WaitForSessionStop;
        return;
    }
    if (s == "WaitForTerminatedSession") {
        k = V2gDin70121CommunicationState::WaitForTerminatedSession;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gDin70121CommunicationState");
}

void to_json(json& j, V2gIso15118AcCommunicationState const& k) noexcept {
    switch (k) {
    case V2gIso15118AcCommunicationState::WaitForSessionSetup:
        j = "WaitForSessionSetup";
        return;
    case V2gIso15118AcCommunicationState::WaitForServiceDiscovery:
        j = "WaitForServiceDiscovery";
        return;
    case V2gIso15118AcCommunicationState::WaitForServiceDetailPaymentServiceSelection:
        j = "WaitForServiceDetailPaymentServiceSelection";
        return;
    case V2gIso15118AcCommunicationState::WaitForPaymentDetailsCertificateInstallCertificateUpdate:
        j = "WaitForPaymentDetailsCertificateInstallCertificateUpdate";
        return;
    case V2gIso15118AcCommunicationState::WaitForPaymentDetails:
        j = "WaitForPaymentDetails";
        return;
    case V2gIso15118AcCommunicationState::WaitForAuthorization:
        j = "WaitForAuthorization";
        return;
    case V2gIso15118AcCommunicationState::WaitForChargeParameterDiscovery:
        j = "WaitForChargeParameterDiscovery";
        return;
    case V2gIso15118AcCommunicationState::WaitForPowerDelivery:
        j = "WaitForPowerDelivery";
        return;
    case V2gIso15118AcCommunicationState::WaitForChargingStatus:
        j = "WaitForChargingStatus";
        return;
    case V2gIso15118AcCommunicationState::WaitForChargingStatusPowerDelivery:
        j = "WaitForChargingStatusPowerDelivery";
        return;
    case V2gIso15118AcCommunicationState::WaitForMeteringReceipt:
        j = "WaitForMeteringReceipt";
        return;
    case V2gIso15118AcCommunicationState::WaitForSessionStop:
        j = "WaitForSessionStop";
        return;
    case V2gIso15118AcCommunicationState::WaitForTerminatedSession:
        j = "WaitForTerminatedSession";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gIso15118AcCommunicationState";
}

void from_json(json const& j, V2gIso15118AcCommunicationState& k) {
    std::string s = j;
    if (s == "WaitForSessionSetup") {
        k = V2gIso15118AcCommunicationState::WaitForSessionSetup;
        return;
    }
    if (s == "WaitForServiceDiscovery") {
        k = V2gIso15118AcCommunicationState::WaitForServiceDiscovery;
        return;
    }
    if (s == "WaitForServiceDetailPaymentServiceSelection") {
        k = V2gIso15118AcCommunicationState::WaitForServiceDetailPaymentServiceSelection;
        return;
    }
    if (s == "WaitForPaymentDetailsCertificateInstallCertificateUpdate") {
        k = V2gIso15118AcCommunicationState::WaitForPaymentDetailsCertificateInstallCertificateUpdate;
        return;
    }
    if (s == "WaitForPaymentDetails") {
        k = V2gIso15118AcCommunicationState::WaitForPaymentDetails;
        return;
    }
    if (s == "WaitForAuthorization") {
        k = V2gIso15118AcCommunicationState::WaitForAuthorization;
        return;
    }
    if (s == "WaitForChargeParameterDiscovery") {
        k = V2gIso15118AcCommunicationState::WaitForChargeParameterDiscovery;
        return;
    }
    if (s == "WaitForPowerDelivery") {
        k = V2gIso15118AcCommunicationState::WaitForPowerDelivery;
        return;
    }
    if (s == "WaitForChargingStatus") {
        k = V2gIso15118AcCommunicationState::WaitForChargingStatus;
        return;
    }
    if (s == "WaitForChargingStatusPowerDelivery") {
        k = V2gIso15118AcCommunicationState::WaitForChargingStatusPowerDelivery;
        return;
    }
    if (s == "WaitForMeteringReceipt") {
        k = V2gIso15118AcCommunicationState::WaitForMeteringReceipt;
        return;
    }
    if (s == "WaitForSessionStop") {
        k = V2gIso15118AcCommunicationState::WaitForSessionStop;
        return;
    }
    if (s == "WaitForTerminatedSession") {
        k = V2gIso15118AcCommunicationState::WaitForTerminatedSession;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gIso15118AcCommunicationState");
}

void to_json(json& j, V2gIso15118DcCommunicationState const& k) noexcept {
    switch (k) {
    case V2gIso15118DcCommunicationState::WaitForSessionSetup:
        j = "WaitForSessionSetup";
        return;
    case V2gIso15118DcCommunicationState::WaitForServiceDiscovery:
        j = "WaitForServiceDiscovery";
        return;
    case V2gIso15118DcCommunicationState::WaitForServiceDetailPaymentServiceSelection:
        j = "WaitForServiceDetailPaymentServiceSelection";
        return;
    case V2gIso15118DcCommunicationState::WaitForPaymentDetailsCertificateInstallCertificateUpdate:
        j = "WaitForPaymentDetailsCertificateInstallCertificateUpdate";
        return;
    case V2gIso15118DcCommunicationState::WaitForPaymentDetails:
        j = "WaitForPaymentDetails";
        return;
    case V2gIso15118DcCommunicationState::WaitForAuthorization:
        j = "WaitForAuthorization";
        return;
    case V2gIso15118DcCommunicationState::WaitForChargeParameterDiscovery:
        j = "WaitForChargeParameterDiscovery";
        return;
    case V2gIso15118DcCommunicationState::WaitForCableCheck:
        j = "WaitForCableCheck";
        return;
    case V2gIso15118DcCommunicationState::WaitForPreCharge:
        j = "WaitForPreCharge";
        return;
    case V2gIso15118DcCommunicationState::WaitForPreChargePowerDelivery:
        j = "WaitForPreChargePowerDelivery";
        return;
    case V2gIso15118DcCommunicationState::WaitForCurrentDemandPowerDelivery:
        j = "WaitForCurrentDemandPowerDelivery";
        return;
    case V2gIso15118DcCommunicationState::WaitForCurrentDemand:
        j = "WaitForCurrentDemand";
        return;
    case V2gIso15118DcCommunicationState::WaitForMeteringReceipt:
        j = "WaitForMeteringReceipt";
        return;
    case V2gIso15118DcCommunicationState::WaitForWeldingDetectionSessionStop:
        j = "WaitForWeldingDetectionSessionStop";
        return;
    case V2gIso15118DcCommunicationState::WaitForTerminatedSession:
        j = "WaitForTerminatedSession";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gIso15118DcCommunicationState";
}

void from_json(json const& j, V2gIso15118DcCommunicationState& k) {
    std::string s = j;
    if (s == "WaitForSessionSetup") {
        k = V2gIso15118DcCommunicationState::WaitForSessionSetup;
        return;
    }
    if (s == "WaitForServiceDiscovery") {
        k = V2gIso15118DcCommunicationState::WaitForServiceDiscovery;
        return;
    }
    if (s == "WaitForServiceDetailPaymentServiceSelection") {
        k = V2gIso15118DcCommunicationState::WaitForServiceDetailPaymentServiceSelection;
        return;
    }
    if (s == "WaitForPaymentDetailsCertificateInstallCertificateUpdate") {
        k = V2gIso15118DcCommunicationState::WaitForPaymentDetailsCertificateInstallCertificateUpdate;
        return;
    }
    if (s == "WaitForPaymentDetails") {
        k = V2gIso15118DcCommunicationState::WaitForPaymentDetails;
        return;
    }
    if (s == "WaitForAuthorization") {
        k = V2gIso15118DcCommunicationState::WaitForAuthorization;
        return;
    }
    if (s == "WaitForChargeParameterDiscovery") {
        k = V2gIso15118DcCommunicationState::WaitForChargeParameterDiscovery;
        return;
    }
    if (s == "WaitForCableCheck") {
        k = V2gIso15118DcCommunicationState::WaitForCableCheck;
        return;
    }
    if (s == "WaitForPreCharge") {
        k = V2gIso15118DcCommunicationState::WaitForPreCharge;
        return;
    }
    if (s == "WaitForPreChargePowerDelivery") {
        k = V2gIso15118DcCommunicationState::WaitForPreChargePowerDelivery;
        return;
    }
    if (s == "WaitForCurrentDemandPowerDelivery") {
        k = V2gIso15118DcCommunicationState::WaitForCurrentDemandPowerDelivery;
        return;
    }
    if (s == "WaitForCurrentDemand") {
        k = V2gIso15118DcCommunicationState::WaitForCurrentDemand;
        return;
    }
    if (s == "WaitForMeteringReceipt") {
        k = V2gIso15118DcCommunicationState::WaitForMeteringReceipt;
        return;
    }
    if (s == "WaitForWeldingDetectionSessionStop") {
        k = V2gIso15118DcCommunicationState::WaitForWeldingDetectionSessionStop;
        return;
    }
    if (s == "WaitForTerminatedSession") {
        k = V2gIso15118DcCommunicationState::WaitForTerminatedSession;
        return;
    }
    throw std::out_of_range(
        "Provided string " + s +
        " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gIso15118DcCommunicationState");
}

void to_json(json& j, V2gCommunicationState const& k) noexcept {
    json state = json::object();
    if (k.din70121.has_value()) {
        state["din70121"] = k.din70121.value();
    }
    if (k.iso15118_ac.has_value()) {
        state["iso15118_ac"] = k.iso15118_ac.value();
    }
    if (k.iso15118_dc.has_value()) {
        state["iso15118_dc"] = k.iso15118_dc.value();
    }
    j = state;
}

void from_json(json const& j, V2gCommunicationState& k) {
    k = {};
    if (j.contains("din70121")) {
        k.din70121 = j.at("din70121").get<V2gDin70121CommunicationState>();
    }
    if (j.contains("iso15118_ac")) {
        k.iso15118_ac = j.at("iso15118_ac").get<V2gIso15118AcCommunicationState>();
    }
    if (j.contains("iso15118_dc")) {
        k.iso15118_dc = j.at("iso15118_dc").get<V2gIso15118DcCommunicationState>();
    }
}

void to_json(json& j, V2gMessageState const& k) noexcept {
    switch (k) {
    case V2gMessageState::SupportedAppProtocol:
        j = "SupportedAppProtocol";
        return;
    case V2gMessageState::SessionSetup:
        j = "SessionSetup";
        return;
    case V2gMessageState::ServiceDiscovery:
        j = "ServiceDiscovery";
        return;
    case V2gMessageState::ServiceDetail:
        j = "ServiceDetail";
        return;
    case V2gMessageState::PaymentServiceSelection:
        j = "PaymentServiceSelection";
        return;
    case V2gMessageState::PaymentDetails:
        j = "PaymentDetails";
        return;
    case V2gMessageState::Authorization:
        j = "Authorization";
        return;
    case V2gMessageState::ChargeParameterDiscovery:
        j = "ChargeParameterDiscovery";
        return;
    case V2gMessageState::MeteringReceipt:
        j = "MeteringReceipt";
        return;
    case V2gMessageState::CertificateUpdate:
        j = "CertificateUpdate";
        return;
    case V2gMessageState::CertificateInstallation:
        j = "CertificateInstallation";
        return;
    case V2gMessageState::ChargingStatus:
        j = "ChargingStatus";
        return;
    case V2gMessageState::CableCheck:
        j = "CableCheck";
        return;
    case V2gMessageState::PreCharge:
        j = "PreCharge";
        return;
    case V2gMessageState::PowerDelivery:
        j = "PowerDelivery";
        return;
    case V2gMessageState::CurrentDemand:
        j = "CurrentDemand";
        return;
    case V2gMessageState::WeldingDetection:
        j = "WeldingDetection";
        return;
    case V2gMessageState::SessionStop:
        j = "SessionStop";
        return;
    case V2gMessageState::Unknown:
        j = "Unknown";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gMessageState";
}

void from_json(json const& j, V2gMessageState& k) {
    std::string s = j;
    if (s == "SupportedAppProtocol") {
        k = V2gMessageState::SupportedAppProtocol;
        return;
    }
    if (s == "SessionSetup") {
        k = V2gMessageState::SessionSetup;
        return;
    }
    if (s == "ServiceDiscovery") {
        k = V2gMessageState::ServiceDiscovery;
        return;
    }
    if (s == "ServiceDetail") {
        k = V2gMessageState::ServiceDetail;
        return;
    }
    if (s == "PaymentServiceSelection") {
        k = V2gMessageState::PaymentServiceSelection;
        return;
    }
    if (s == "PaymentDetails") {
        k = V2gMessageState::PaymentDetails;
        return;
    }
    if (s == "Authorization") {
        k = V2gMessageState::Authorization;
        return;
    }
    if (s == "ChargeParameterDiscovery") {
        k = V2gMessageState::ChargeParameterDiscovery;
        return;
    }
    if (s == "MeteringReceipt") {
        k = V2gMessageState::MeteringReceipt;
        return;
    }
    if (s == "CertificateUpdate") {
        k = V2gMessageState::CertificateUpdate;
        return;
    }
    if (s == "CertificateInstallation") {
        k = V2gMessageState::CertificateInstallation;
        return;
    }
    if (s == "ChargingStatus") {
        k = V2gMessageState::ChargingStatus;
        return;
    }
    if (s == "CableCheck") {
        k = V2gMessageState::CableCheck;
        return;
    }
    if (s == "PreCharge") {
        k = V2gMessageState::PreCharge;
        return;
    }
    if (s == "PowerDelivery") {
        k = V2gMessageState::PowerDelivery;
        return;
    }
    if (s == "CurrentDemand") {
        k = V2gMessageState::CurrentDemand;
        return;
    }
    if (s == "WeldingDetection") {
        k = V2gMessageState::WeldingDetection;
        return;
    }
    if (s == "SessionStop") {
        k = V2gMessageState::SessionStop;
        return;
    }
    if (s == "Unknown") {
        k = V2gMessageState::Unknown;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gMessageState");
}

void to_json(json& j, V2gServerStatus const& k) noexcept {
    switch (k) {
    case V2gServerStatus::Inactive:
        j = "Inactive";
        return;
    case V2gServerStatus::Active:
        j = "Active";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gServerStatus";
}

void from_json(json const& j, V2gServerStatus& k) {
    std::string s = j;
    if (s == "Inactive") {
        k = V2gServerStatus::Inactive;
        return;
    }
    if (s == "Active") {
        k = V2gServerStatus::Active;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gServerStatus");
}

void to_json(json& j, V2gEvErrorCode const& k) noexcept {
    switch (k) {
    case V2gEvErrorCode::NO_ERROR:
        j = "NO_ERROR";
        return;
    case V2gEvErrorCode::FAILED_RESSTemperatureInhibit:
        j = "FAILED_RESSTemperatureInhibit";
        return;
    case V2gEvErrorCode::FAILED_EVShiftPosition:
        j = "FAILED_EVShiftPosition";
        return;
    case V2gEvErrorCode::FAILED_ChargerConnectorLockFault:
        j = "FAILED_ChargerConnectorLockFault";
        return;
    case V2gEvErrorCode::FAILED_EVRESSMalfunction:
        j = "FAILED_EVRESSMalfunction";
        return;
    case V2gEvErrorCode::FAILED_ChargingCurrentdifferential:
        j = "FAILED_ChargingCurrentdifferential";
        return;
    case V2gEvErrorCode::FAILED_ChargingVoltageOutOfRange:
        j = "FAILED_ChargingVoltageOutOfRange";
        return;
    case V2gEvErrorCode::Reserved_A:
        j = "Reserved_A";
        return;
    case V2gEvErrorCode::Reserved_B:
        j = "Reserved_B";
        return;
    case V2gEvErrorCode::Reserved_C:
        j = "Reserved_C";
        return;
    case V2gEvErrorCode::FAILED_ChargingSystemIncompatibility:
        j = "FAILED_ChargingSystemIncompatibility";
        return;
    case V2gEvErrorCode::NoData:
        j = "NoData";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::V2gEvErrorCode";
}

void from_json(json const& j, V2gEvErrorCode& k) {
    std::string s = j;
    if (s == "NO_ERROR") {
        k = V2gEvErrorCode::NO_ERROR;
        return;
    }
    if (s == "FAILED_RESSTemperatureInhibit") {
        k = V2gEvErrorCode::FAILED_RESSTemperatureInhibit;
        return;
    }
    if (s == "FAILED_EVShiftPosition") {
        k = V2gEvErrorCode::FAILED_EVShiftPosition;
        return;
    }
    if (s == "FAILED_ChargerConnectorLockFault") {
        k = V2gEvErrorCode::FAILED_ChargerConnectorLockFault;
        return;
    }
    if (s == "FAILED_EVRESSMalfunction") {
        k = V2gEvErrorCode::FAILED_EVRESSMalfunction;
        return;
    }
    if (s == "FAILED_ChargingCurrentdifferential") {
        k = V2gEvErrorCode::FAILED_ChargingCurrentdifferential;
        return;
    }
    if (s == "FAILED_ChargingVoltageOutOfRange") {
        k = V2gEvErrorCode::FAILED_ChargingVoltageOutOfRange;
        return;
    }
    if (s == "Reserved_A") {
        k = V2gEvErrorCode::Reserved_A;
        return;
    }
    if (s == "Reserved_B") {
        k = V2gEvErrorCode::Reserved_B;
        return;
    }
    if (s == "Reserved_C") {
        k = V2gEvErrorCode::Reserved_C;
        return;
    }
    if (s == "FAILED_ChargingSystemIncompatibility") {
        k = V2gEvErrorCode::FAILED_ChargingSystemIncompatibility;
        return;
    }
    if (s == "NoData") {
        k = V2gEvErrorCode::NoData;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_V2gEvErrorCode");
}

void to_json(json& j, ChargeProgress const& k) noexcept {
    switch (k) {
    case ChargeProgress::Start:
        j = "Start";
        return;
    case ChargeProgress::Stop:
        j = "Stop";
        return;
    case ChargeProgress::Renegotiate:
        j = "Renegotiate";
        return;
    case ChargeProgress::Pause:
        j = "Pause";
        return;
    case ChargeProgress::Terminate:
        j = "Terminate";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::ChargeProgress";
}

void from_json(json const& j, ChargeProgress& k) {
    std::string s = j;
    if (s == "Start") {
        k = ChargeProgress::Start;
        return;
    }
    if (s == "Stop") {
        k = ChargeProgress::Stop;
        return;
    }
    if (s == "Renegotiate") {
        k = ChargeProgress::Renegotiate;
        return;
    }
    if (s == "Pause") {
        k = ChargeProgress::Pause;
        return;
    }
    if (s == "Terminate") {
        k = ChargeProgress::Terminate;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type API_V1_0_TYPES_TELEMETRY_ChargeProgress");
}

void to_json(json& j, SlacState const& k) noexcept {
    switch (k) {
    case SlacState::Init:
        j = "Init";
        return;
    case SlacState::Reset:
        j = "Reset";
        return;
    case SlacState::ResetChip:
        j = "ResetChip";
        return;
    case SlacState::Idle:
        j = "Idle";
        return;
    case SlacState::Failed:
        j = "Failed";
        return;
    case SlacState::Unmatched:
        j = "Unmatched";
        return;
    case SlacState::Matching:
        j = "Matching";
        return;
    case SlacState::WaitForLink:
        j = "WaitForLink";
        return;
    case SlacState::Validate:
        j = "Validate";
        return;
    case SlacState::Matched:
        j = "Matched";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::SlacState";
}

void from_json(json const& j, SlacState& k) {
    std::string s = j;
    if (s == "Init") {
        k = SlacState::Init;
        return;
    }
    if (s == "Reset") {
        k = SlacState::Reset;
        return;
    }
    if (s == "ResetChip") {
        k = SlacState::ResetChip;
        return;
    }
    if (s == "Idle") {
        k = SlacState::Idle;
        return;
    }
    if (s == "Failed") {
        k = SlacState::Failed;
        return;
    }
    if (s == "Unmatched") {
        k = SlacState::Unmatched;
        return;
    }
    if (s == "Matching") {
        k = SlacState::Matching;
        return;
    }
    if (s == "WaitForLink") {
        k = SlacState::WaitForLink;
        return;
    }
    if (s == "Validate") {
        k = SlacState::Validate;
        return;
    }
    if (s == "Matched") {
        k = SlacState::Matched;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::telemetry::SlacState");
}

void to_json(json& j, SlacD3State const& k) noexcept {
    switch (k) {
    case SlacD3State::Unmatched:
        j = "Unmatched";
        return;
    case SlacD3State::Matching:
        j = "Matching";
        return;
    case SlacD3State::Matched:
        j = "Matched";
        return;
    }
    j = "INVALID_VALUE__everest::lib::API::V1_0::types::telemetry::SlacD3State";
}

void from_json(json const& j, SlacD3State& k) {
    std::string s = j;
    if (s == "Unmatched") {
        k = SlacD3State::Unmatched;
        return;
    }
    if (s == "Matching") {
        k = SlacD3State::Matching;
        return;
    }
    if (s == "Matched") {
        k = SlacD3State::Matched;
        return;
    }
    throw std::out_of_range("Provided string " + s +
                            " could not be converted to enum of type "
                            "everest::lib::API::V1_0::types::telemetry::SlacD3State");
}

void to_json(json& j, CertChainState const& k) noexcept {
    j = json{
        {"configured", k.configured},
        {"synced", k.synced},
        {"num_files", k.num_files},
        {"num_useful_files", k.num_useful_files},
    };
}

void from_json(json const& j, CertChainState& k) {
    k.configured = j.at("configured");
    k.synced = j.at("synced");
    k.num_files = j.at("num_files");
    k.num_useful_files = j.at("num_useful_files");
}

void to_json(json& j, CertTelemetry const& k) noexcept {
    j = json{
        {"config_complete", k.config_complete},
        {"sync_complete", k.sync_complete},
        {"secc_chain", k.secc_chain},
        {"mo_root", k.mo_root},
    };
}

void from_json(json const& j, CertTelemetry& k) {
    k.config_complete = j.at("config_complete");
    k.sync_complete = j.at("sync_complete");
    k.secc_chain = j.at("secc_chain");
    k.mo_root = j.at("mo_root");
}

void to_json(json& j, EvseControlStatus const& k) noexcept {
    j = json{
        {"authorisation_finished", k.authorisation_finished},
        {"emergency_stop", k.emergency_stop},
        {"error_stop", k.error_stop},
        {"normal_stop", k.normal_stop},
        {"contract_payment_enabled", k.contract_payment_enabled},
        {"free_charging_enabled", k.free_charging_enabled},
    };
}

void from_json(json const& j, EvseControlStatus& k) {
    k.authorisation_finished = j.at("authorisation_finished");
    k.emergency_stop = j.at("emergency_stop");
    k.error_stop = j.at("error_stop");
    k.normal_stop = j.at("normal_stop");
    k.contract_payment_enabled = j.at("contract_payment_enabled");
    k.free_charging_enabled = j.at("free_charging_enabled");
}

void to_json(json& j, V2gTransport const& k) noexcept {
    j = json{
        {"comm_state", k.comm_state},
        {"message_state", k.message_state},
        {"udp_server_status", k.udp_server_status},
        {"tcp_listener_status", k.tcp_listener_status},
        {"tcp_server_status", k.tcp_server_status},
        {"tcp_connection_established", k.tcp_connection_established},
        {"tcp_discovery_enable", k.tcp_discovery_enable},
        {"tcp_security_enable", k.tcp_security_enable},
        {"tcp_security_required", k.tcp_security_required},
        {"tcp_port_number", k.tcp_port_number},
        {"session_setup_requested", k.session_setup_requested},
        {"authorization_requested", k.authorization_requested},
        {"charge_parameter_discovery_requested", k.charge_parameter_discovery_requested},
        {"cable_check_requested", k.cable_check_requested},
        {"pre_charge_requested", k.pre_charge_requested},
        {"current_demand_requested", k.current_demand_requested},
        {"welding_detection_requested", k.welding_detection_requested},
    };
}

void from_json(json const& j, V2gTransport& k) {
    k.comm_state = j.at("comm_state");
    k.message_state = j.at("message_state");
    k.udp_server_status = j.at("udp_server_status");
    k.tcp_listener_status = j.at("tcp_listener_status");
    k.tcp_server_status = j.at("tcp_server_status");
    k.tcp_connection_established = j.at("tcp_connection_established");
    k.tcp_discovery_enable = j.at("tcp_discovery_enable");
    k.tcp_security_enable = j.at("tcp_security_enable");
    k.tcp_security_required = j.at("tcp_security_required");
    k.tcp_port_number = j.at("tcp_port_number");
    k.session_setup_requested = j.at("session_setup_requested");
    k.authorization_requested = j.at("authorization_requested");
    k.charge_parameter_discovery_requested = j.at("charge_parameter_discovery_requested");
    k.cable_check_requested = j.at("cable_check_requested");
    k.pre_charge_requested = j.at("pre_charge_requested");
    k.current_demand_requested = j.at("current_demand_requested");
    k.welding_detection_requested = j.at("welding_detection_requested");
}

void to_json(json& j, V2gEvElectrical const& k) noexcept {
    j = json{
        {"charge_progress", k.charge_progress},
        {"battery_soc_percent", k.battery_soc_percent},
        {"error_code", k.error_code},
        {"maximum_current_A", k.maximum_current_A},
        {"maximum_power_W", k.maximum_power_W},
        {"maximum_voltage_V", k.maximum_voltage_V},
        {"maximum_rated_current_A", k.maximum_rated_current_A},
        {"maximum_rated_power_W", k.maximum_rated_power_W},
        {"maximum_rated_voltage_V", k.maximum_rated_voltage_V},
        {"target_current_A", k.target_current_A},
        {"target_voltage_V", k.target_voltage_V},
        {"remaining_time_bulk_min", k.remaining_time_bulk_min},
        {"remaining_time_full_min", k.remaining_time_full_min},
        {"energy_request_Wh", k.energy_request_Wh},
        {"energy_capacity_Wh", k.energy_capacity_Wh},
    };
}

void from_json(json const& j, V2gEvElectrical& k) {
    k.charge_progress = j.at("charge_progress");
    k.battery_soc_percent = j.at("battery_soc_percent");
    k.error_code = j.at("error_code");
    k.maximum_current_A = j.at("maximum_current_A");
    k.maximum_power_W = j.at("maximum_power_W");
    k.maximum_voltage_V = j.at("maximum_voltage_V");
    k.maximum_rated_current_A = j.at("maximum_rated_current_A");
    k.maximum_rated_power_W = j.at("maximum_rated_power_W");
    k.maximum_rated_voltage_V = j.at("maximum_rated_voltage_V");
    k.target_current_A = j.at("target_current_A");
    k.target_voltage_V = j.at("target_voltage_V");
    k.remaining_time_bulk_min = j.at("remaining_time_bulk_min");
    k.remaining_time_full_min = j.at("remaining_time_full_min");
    k.energy_request_Wh = j.at("energy_request_Wh");
    k.energy_capacity_Wh = j.at("energy_capacity_Wh");
}

void to_json(json& j, V2gPaymentService const& k) noexcept {
    j = json{
        {"certificate_install_requested", k.certificate_install_requested},
        {"certificate_update_requested", k.certificate_update_requested},
        {"charging_service_requested", k.charging_service_requested},
        {"external_payment_requested", k.external_payment_requested},
        {"contract_payment_requested", k.contract_payment_requested},
        {"contract_payment_approved", k.contract_payment_approved},
        {"contract_payment_error", k.contract_payment_error},
        {"internet_ftp20_requested", k.internet_ftp20_requested},
        {"internet_ftp21_requested", k.internet_ftp21_requested},
        {"internet_http80_requested", k.internet_http80_requested},
        {"internet_https443_requested", k.internet_https443_requested},
    };
}

void from_json(json const& j, V2gPaymentService& k) {
    k.certificate_install_requested = j.at("certificate_install_requested");
    k.certificate_update_requested = j.at("certificate_update_requested");
    k.charging_service_requested = j.at("charging_service_requested");
    k.external_payment_requested = j.at("external_payment_requested");
    k.contract_payment_requested = j.at("contract_payment_requested");
    k.contract_payment_approved = j.at("contract_payment_approved");
    k.contract_payment_error = j.at("contract_payment_error");
    k.internet_ftp20_requested = j.at("internet_ftp20_requested");
    k.internet_ftp21_requested = j.at("internet_ftp21_requested");
    k.internet_http80_requested = j.at("internet_http80_requested");
    k.internet_https443_requested = j.at("internet_https443_requested");
}

void to_json(json& j, V2gChargerStatus const& k) noexcept {
    j = json{
        {"evcc_id", k.evcc_id},
        {"selected_protocol", k.selected_protocol},
        {"cable_check_status", k.cable_check_status},
        {"param_discovery_finished", k.param_discovery_finished},
        {"isolation_status", k.isolation_status},
        {"dynamic_max_current_A", k.dynamic_max_current_A},
        {"dynamic_max_power_W", k.dynamic_max_power_W},
        {"dynamic_max_voltage_V", k.dynamic_max_voltage_V},
    };
}

void from_json(json const& j, V2gChargerStatus& k) {
    k.evcc_id = j.at("evcc_id");
    k.selected_protocol = j.at("selected_protocol");
    k.cable_check_status = j.at("cable_check_status");
    k.param_discovery_finished = j.at("param_discovery_finished");
    k.isolation_status = j.at("isolation_status");
    k.dynamic_max_current_A = j.at("dynamic_max_current_A");
    k.dynamic_max_power_W = j.at("dynamic_max_power_W");
    k.dynamic_max_voltage_V = j.at("dynamic_max_voltage_V");
}

void to_json(json& j, V2gEvseElectrical const& k) noexcept {
    j = json{
        {"present_current_A", k.present_current_A},
        {"present_voltage_V", k.present_voltage_V},
        {"maximum_rated_current_A", k.maximum_rated_current_A},
        {"maximum_rated_power_W", k.maximum_rated_power_W},
        {"maximum_rated_voltage_V", k.maximum_rated_voltage_V},
        {"minimum_rated_current_A", k.minimum_rated_current_A},
        {"minimum_rated_voltage_V", k.minimum_rated_voltage_V},
        {"current_ripple_A", k.current_ripple_A},
        {"current_tolerance_A", k.current_tolerance_A},
    };
}

void from_json(json const& j, V2gEvseElectrical& k) {
    k.present_current_A = j.at("present_current_A");
    k.present_voltage_V = j.at("present_voltage_V");
    k.maximum_rated_current_A = j.at("maximum_rated_current_A");
    k.maximum_rated_power_W = j.at("maximum_rated_power_W");
    k.maximum_rated_voltage_V = j.at("maximum_rated_voltage_V");
    k.minimum_rated_current_A = j.at("minimum_rated_current_A");
    k.minimum_rated_voltage_V = j.at("minimum_rated_voltage_V");
    k.current_ripple_A = j.at("current_ripple_A");
    k.current_tolerance_A = j.at("current_tolerance_A");
}

void to_json(json& j, SlacStatus const& k) noexcept {
    j = json{
        {"matching_requested", k.matching_requested},
        {"modem_PIB", k.modem_PIB},
        {"modem_NMK", k.modem_NMK},
        {"modem_link_ready", k.modem_link_ready},
        {"session_count", k.session_count},
        {"average_attenuation", k.average_attenuation},
        {"ev_mac", k.ev_mac},
        {"match_state", k.match_state},
        {"d3_state", k.d3_state},
    };
}

void from_json(json const& j, SlacStatus& k) {
    if (j.contains("matching_requested")) {
        k.matching_requested = j.at("matching_requested");
    }
    if (j.contains("modem_PIB")) {
        k.modem_PIB = j.at("modem_PIB");
    }
    if (j.contains("modem_NMK")) {
        k.modem_NMK = j.at("modem_NMK");
    }
    if (j.contains("modem_link_ready")) {
        k.modem_link_ready = j.at("modem_link_ready");
    }
    if (j.contains("session_count")) {
        k.session_count = j.at("session_count");
    }
    if (j.contains("average_attenuation")) {
        k.average_attenuation = j.at("average_attenuation");
    }
    if (j.contains("ev_mac")) {
        k.ev_mac = j.at("ev_mac");
    }
    if (j.contains("match_state")) {
        k.match_state = j.at("match_state");
    }
    if (j.contains("d3_state")) {
        k.d3_state = j.at("d3_state");
    }
}

void to_json(json& j, SlacFsmState const& k) noexcept {
    j = json::object();

    if (!k.states.empty()) {
        j["states"] = k.states;
    }
    if (!k.submachines.empty()) {
        j["submachines"] = k.submachines;
    }
    if (!k.sessions.empty()) {
        j["sessions"] = k.sessions;
    }
}

void from_json(json const& j, SlacFsmState& k) {
    if (j.contains("states")) {
        k.states = j.at("states").get<std::vector<std::string>>();
    }
    if (j.contains("submachines")) {
        k.submachines = j.at("submachines").get<std::map<std::string, SlacFsmState>>();
    }
    if (j.contains("sessions")) {
        k.sessions = j.at("sessions").get<std::vector<SlacFsmState>>();
    }
}

} // namespace everest::lib::API::V1_0::types::telemetry
