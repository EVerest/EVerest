// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <generated/types/evse_manager.hpp>

#include "v2g.hpp"

// Failure reason for a connection that closed while \p msg was the message in flight.
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

// The message that names the phase a closing connection was in: the request in flight, or the last one the
// state machine admitted if the request in flight was answered with SequenceError. A SessionStopReq counts as
// a SessionStop whether or not the state machine admitted it.
inline V2gMsgTypeId v2g_msg_at_disconnect(const v2g_context& ctx) {
    if (ctx.current_v2g_msg_out_of_sequence and ctx.current_v2g_msg != V2G_SESSION_STOP_MSG) {
        return ctx.last_in_sequence_v2g_msg;
    }
    return ctx.current_v2g_msg;
}

// Reason to publish as hlc_session_failed, from the last message handled before the connection closed and,
// if that was SessionStop, the message the EV sent before it. A SessionStopReq is reported when it follows
// protocol negotiation, authorization, cable check or pre-charge, e.g. when the V2G_EVCC_Ongoing_Timer
// expired while AuthorizationReq was still answered with Ongoing. Who caused the stop is not considered: an
// EV that follows the EVSE's shutdown status during cable check or pre-charge is reported alike.
// Elsewhere it is not reported. During service and charge parameter negotiation the preceding message alone
// cannot tell an EV that gave up from a regular end, since ChargeParameterDiscoveryRes carries the EVSE's own
// stop notifications. After any PowerDelivery, the charge loop or WeldingDetection it is the regular end.
inline std::optional<types::evse_manager::HlcSessionFailedReasonEnum>
hlc_session_failed_reason_at_disconnect(V2gMsgTypeId last_msg, V2gMsgTypeId msg_before_session_stop,
                                        bool tls_handshake_failed) {
    using Reason = types::evse_manager::HlcSessionFailedReasonEnum;
    if (tls_handshake_failed) {
        return Reason::FailedTLSHandshake;
    }
    if (last_msg != V2G_SESSION_STOP_MSG) {
        return hlc_session_failed_reason_for_phase(last_msg);
    }
    if (msg_before_session_stop == V2G_POWER_DELIVERY_MSG) {
        return std::nullopt;
    }
    const auto reason = hlc_session_failed_reason_for_phase(msg_before_session_stop);
    if (reason == Reason::ProtocolNegotiationFailed or reason == Reason::AuthorizationFailed or
        reason == Reason::EnergyTransferSetupFailed) {
        return reason;
    }
    return std::nullopt;
}
