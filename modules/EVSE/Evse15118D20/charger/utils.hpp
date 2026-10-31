// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <generated/types/evse_manager.hpp>
#include <generated/types/iso15118.hpp>
#include <generated/types/iso15118_vas.hpp>

#include <iso15118/d20/ev_information.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/ac_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_iec_charge_loop.hpp>
#include <iso15118/message/ac_der_iec_charge_parameter_discovery.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/service_detail.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

#include <everest/util/enum/EnumFlags.hpp>
#include <everest/util/vector/fixed_vector.hpp>

#include "conversions.hpp"

namespace module::charger {

namespace dt = iso15118::message_20::datatypes;

// The setup steps the module has to report before the controller may be started. `last` is required by
// util::EnumFlags and keeps the flag width in sync with the enum.
enum class SetupStep : std::uint8_t {
    SETUP,
    ENERGY_SERVICE,
    AUTH_SETUP,
    MAX_LIMITS,
    MIN_LIMITS,
    last = MIN_LIMITS,
};

using SetupStepsDone = everest::lib::util::EnumFlags<SetupStep>;

// Maps the last V2G message handled before a session tore down to a protocol-agnostic
// HlcSessionFailedReasonEnum, keyed on the phase that message belongs to (mirrors EvseV2G's
// map_v2g_msg_to_hlc_failed_reason). Returns std::nullopt for messages that represent a clean end
// (SessionStop, WeldingDetection) or no session activity (None): no failure is reported for those.
constexpr std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
map_v2g_message_to_hlc_failed_reason(iso15118::message_20::Type type) {
    using Type = iso15118::message_20::Type;
    using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
    switch (type) {
    case Type::SupportedAppProtocolReq:
    case Type::SupportedAppProtocolRes:
    case Type::SessionSetupReq:
    case Type::SessionSetupRes:
        return Reason::ProtocolNegotiationFailed;
    case Type::AuthorizationSetupReq:
    case Type::AuthorizationSetupRes:
    case Type::AuthorizationReq:
    case Type::AuthorizationRes:
    case Type::CertificateInstallationReq:
    case Type::CertificateInstallationRes:
        return Reason::AuthorizationFailed;
    case Type::ServiceDiscoveryReq:
    case Type::ServiceDiscoveryRes:
    case Type::ServiceDetailReq:
    case Type::ServiceDetailRes:
    case Type::ServiceSelectionReq:
    case Type::ServiceSelectionRes:
    case Type::DC_ChargeParameterDiscoveryReq:
    case Type::DC_ChargeParameterDiscoveryRes:
    case Type::AC_ChargeParameterDiscoveryReq:
    case Type::AC_ChargeParameterDiscoveryRes:
    case Type::DER_AC_ChargeParameterDiscoveryReq:
    case Type::DER_AC_ChargeParameterDiscoveryRes:
    case Type::DER_SAE_AC_ChargeParameterDiscoveryReq:
    case Type::DER_SAE_AC_ChargeParameterDiscoveryRes:
    case Type::ScheduleExchangeReq:
    case Type::ScheduleExchangeRes:
        return Reason::ChargingParametersNotAccepted;
    case Type::DC_CableCheckReq:
    case Type::DC_CableCheckRes:
    case Type::DC_PreChargeReq:
    case Type::DC_PreChargeRes:
    case Type::PowerDeliveryReq:
    case Type::PowerDeliveryRes:
        return Reason::EnergyTransferSetupFailed;
    case Type::DC_ChargeLoopReq:
    case Type::DC_ChargeLoopRes:
    case Type::AC_ChargeLoopReq:
    case Type::AC_ChargeLoopRes:
    case Type::DER_AC_ChargeLoopReq:
    case Type::DER_AC_ChargeLoopRes:
    case Type::DER_SAE_AC_ChargeLoopReq:
    case Type::DER_SAE_AC_ChargeLoopRes:
        return Reason::ChargingInterrupted;
    case Type::DC_WeldingDetectionReq:
    case Type::DC_WeldingDetectionRes:
    case Type::SessionStopReq:
    case Type::SessionStopRes:
    case Type::None:
        return std::nullopt;
    }
    return Reason::UnexpectedSessionEnd;
}

constexpr std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
map_v2g_message_to_hlc_failed_reason(iso15118::message_2::Type type) {
    using Type = iso15118::message_2::Type;
    using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
    switch (type) {
    case Type::SessionSetupReq:
    case Type::SessionSetupRes:
        return Reason::ProtocolNegotiationFailed;
    case Type::ServiceDiscoveryReq:
    case Type::ServiceDiscoveryRes:
    case Type::ServiceDetailReq:
    case Type::ServiceDetailRes:
    case Type::ChargeParameterDiscoveryReq:
    case Type::ChargeParameterDiscoveryRes:
        return Reason::ChargingParametersNotAccepted;
    case Type::PaymentServiceSelectionReq:
    case Type::PaymentServiceSelectionRes:
    case Type::PaymentDetailsReq:
    case Type::PaymentDetailsRes:
    case Type::AuthorizationReq:
    case Type::AuthorizationRes:
    case Type::CertificateInstallationReq:
    case Type::CertificateInstallationRes:
    case Type::CertificateUpdateReq:
    case Type::CertificateUpdateRes:
        return Reason::AuthorizationFailed;
    case Type::CableCheckReq:
    case Type::CableCheckRes:
    case Type::PreChargeReq:
    case Type::PreChargeRes:
    case Type::PowerDeliveryReq:
    case Type::PowerDeliveryRes:
        return Reason::EnergyTransferSetupFailed;
    case Type::CurrentDemandReq:
    case Type::CurrentDemandRes:
    case Type::ChargingStatusReq:
    case Type::ChargingStatusRes:
    case Type::MeteringReceiptReq:
    case Type::MeteringReceiptRes:
        return Reason::ChargingInterrupted;
    case Type::WeldingDetectionReq:
    case Type::WeldingDetectionRes:
    case Type::SessionStopReq:
    case Type::SessionStopRes:
    case Type::None:
        return std::nullopt;
    }
    return Reason::UnexpectedSessionEnd;
}

constexpr std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
map_v2g_message_to_hlc_failed_reason(iso15118::message_din::Type type) {
    using Type = iso15118::message_din::Type;
    using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
    switch (type) {
    case Type::SessionSetupReq:
    case Type::SessionSetupRes:
        return Reason::ProtocolNegotiationFailed;
    case Type::ServiceDiscoveryReq:
    case Type::ServiceDiscoveryRes:
    case Type::ChargeParameterDiscoveryReq:
    case Type::ChargeParameterDiscoveryRes:
        return Reason::ChargingParametersNotAccepted;
    case Type::ServicePaymentSelectionReq:
    case Type::ServicePaymentSelectionRes:
    case Type::ContractAuthenticationReq:
    case Type::ContractAuthenticationRes:
        return Reason::AuthorizationFailed;
    case Type::CableCheckReq:
    case Type::CableCheckRes:
    case Type::PreChargeReq:
    case Type::PreChargeRes:
    case Type::PowerDeliveryReq:
    case Type::PowerDeliveryRes:
        return Reason::EnergyTransferSetupFailed;
    case Type::CurrentDemandReq:
    case Type::CurrentDemandRes:
        return Reason::ChargingInterrupted;
    case Type::WeldingDetectionReq:
    case Type::WeldingDetectionRes:
    case Type::SessionStopReq:
    case Type::SessionStopRes:
    case Type::None:
        return std::nullopt;
    }
    return Reason::UnexpectedSessionEnd;
}

// Dispatcher over the protocol-neutral V2gMessageType variant.
inline std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
map_v2g_message_to_hlc_failed_reason(const iso15118::V2gMessageType& type) {
    return std::visit([](auto&& concrete) { return map_v2g_message_to_hlc_failed_reason(concrete); }, type);
}

std::optional<float> convert_from_optional(const std::optional<dt::RationalNumber>& in);
std::optional<dt::RationalNumber> convert_from_optional(const std::optional<float>& in);
std::optional<float> convert_from_optional(const std::optional<uint32_t>& in);

types::iso15118::AppProtocol convert_app_protocol(const iso15118::message_20::SupportedAppProtocol& app_protocol);

// Debug rendering of a raw V2GTP frame for the v2g_messages var, in the two encodings EvseV2G
// publishes (v2g_server.cpp:275-288): lowercase hex without separators, and standard base64.
std::string to_hex_string(const iso15118::io::StreamInputView& frame);
std::string to_base64_string(const iso15118::io::StreamInputView& frame);
types::iso15118::EvInformation convert_ev_info(const iso15118::d20::EVInformation& ev_info);

types::iso15118::DcChargeDynamicModeValues convert_dynamic_values(const dt::Dynamic_DC_CLReqControlMode& in);
types::iso15118::DcChargeDynamicModeValues convert_dynamic_values(const dt::BPT_Dynamic_DC_CLReqControlMode& in);

template <typename EVSE, typename EV>
void fill_v2x_charging_parameters(types::iso15118::V2XChargingParameters& out_params, const EVSE& evse_limits,
                                  const EV& ev_limits);
template <typename In>
void fill_v2x_charging_parameters(types::iso15118::V2XChargingParameters& out_params, const In& data);

everest::lib::util::fixed_vector<dt::ParameterSet, 32>
convert_parameter_set_list(const std::vector<types::iso15118_vas::ParameterSet>& parameter_set_list);

types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::AC_CPDReqEnergyTransferMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::BPT_AC_CPDReqEnergyTransferMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::Dynamic_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::BPT_Dynamic_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::Scheduled_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::BPT_Scheduled_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::DER_AC_CPDReqEnergyTransferMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::DER_Scheduled_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::DER_Dynamic_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::sae::DER_SAE_AC_CPDReqEnergyTransferMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::sae::DER_Scheduled_AC_CLReqControlMode& mode);
types::iso15118::AcEvPowerLimits fill_ac_ev_power_limits(const dt::sae::DER_Dynamic_AC_CLReqControlMode& mode);

types::iso15118::AcEvPresentPowerValues fill_ac_ev_present_power_values(const dt::Dynamic_AC_CLReqControlMode& mode);
types::iso15118::AcEvPresentPowerValues fill_ac_ev_present_power_values(const dt::Scheduled_AC_CLReqControlMode& mode);

} // namespace module::charger
