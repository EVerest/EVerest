// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <generated/types/evse_manager.hpp>

#include "v2g.hpp"

// Failure reason for a session the EV abandoned while \p msg was the message in flight.
// SessionStop and WeldingDetection are not failures by themselves, so they map to no reason;
// hlc_session_failed_reason_at_disconnect decides what a SessionStop means.
inline std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
hlc_session_failed_reason_for_phase(V2gMsgTypeId msg) {
    using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
    switch (msg) {
    case V2G_SUPPORTED_APP_PROTOCOL_MSG:
    case V2G_SESSION_SETUP_MSG:
        return Reason::ProtocolNegotiationFailed;
    case V2G_SERVICE_DISCOVERY_MSG:
    case V2G_SERVICE_DETAIL_MSG:
    case V2G_CHARGE_PARAMETER_DISCOVERY_MSG:
        return Reason::ChargingParametersNotAccepted;
    case V2G_PAYMENT_SERVICE_SELECTION_MSG:
    case V2G_PAYMENT_DETAILS_MSG:
    case V2G_AUTHORIZATION_MSG:
    case V2G_CERTIFICATE_UPDATE_MSG:
    case V2G_CERTIFICATE_INSTALLATION_MSG:
        return Reason::AuthorizationFailed;
    case V2G_CABLE_CHECK_MSG:
    case V2G_PRE_CHARGE_MSG:
    case V2G_POWER_DELIVERY_MSG:
        return Reason::EnergyTransferSetupFailed;
    case V2G_CURRENT_DEMAND_MSG:
    case V2G_CHARGING_STATUS_MSG:
    case V2G_METERING_RECEIPT_MSG:
        return Reason::ChargingInterrupted;
    case V2G_SESSION_STOP_MSG:
    case V2G_WELDING_DETECTION_MSG:
        return std::nullopt;
    default:
        return Reason::UnexpectedSessionEnd;
    }
}

// Reason to publish as hlc_session_failed for a session the EV ended, from the last message handled before
// the connection closed and, if that was SessionStop, the message the EV sent before it. A SessionStopReq is
// the regular end only after PowerDelivery(Stop) or WeldingDetection; sent from any earlier phase it means
// the EV gave up there, e.g. because its V2G_EVCC_Ongoing_Timer expired while the EVSE was still answering
// AuthorizationReq with Ongoing.
inline std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
hlc_session_failed_reason_at_disconnect(V2gMsgTypeId last_msg, V2gMsgTypeId msg_before_session_stop,
                                        bool tls_handshake_failed) {
    if (tls_handshake_failed) {
        return types::evse_manager::HlcSessionFailedReasonEnum::FailedTLSHandshake;
    }
    if (last_msg != V2G_SESSION_STOP_MSG) {
        return hlc_session_failed_reason_for_phase(last_msg);
    }
    switch (msg_before_session_stop) {
    case V2G_POWER_DELIVERY_MSG:
    case V2G_WELDING_DETECTION_MSG:
    case V2G_UNKNOWN_MSG:
        return std::nullopt;
    default:
        return hlc_session_failed_reason_for_phase(msg_before_session_stop);
    }
}
