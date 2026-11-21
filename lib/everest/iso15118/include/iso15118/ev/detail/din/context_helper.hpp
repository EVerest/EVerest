// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/context_helper.hpp>
#include <iso15118/ev/din/context.hpp>
#include <iso15118/ev/din/state/session_stop.hpp>
#include <iso15118/ev/message_din/response_variant.hpp>
#include <iso15118/ev/session/stop_reason.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::ev::din {

// Validate a response: expected variant, session id of the active session, response code below FAILED.
// On failure, logs it unless the decoder already did, stops the session and returns nullptr; the
// caller then returns Result::stopping().
template <typename Res> const Res* expect_response(Context& ctx, const message_din::ResponseVariant& variant) {
    const auto* res = expect_response_type<Res>(ctx, variant);
    if (res == nullptr) {
        return nullptr;
    }
    if (res->header.session_id != ctx.get_session_id()) {
        logf_error("Response session_id does not match the active session");
        ctx.stop_session();
        return nullptr;
    }
    if (res->response_code >= message_din::datatypes::ResponseCode::FAILED) {
        logf_error("Response rejected with response_code: %d", static_cast<int>(res->response_code));
        ctx.stop_session();
        return nullptr;
    }
    return res;
}

// [V2G-DC-650]: EVSE_Shutdown, EVSE_EmergencyShutdown and EVSENotification StopCharging stop charging.
// [V2G-DC-637]: other status codes are informational; EVSE_IsolationMonitoringActive and
// EVSE_Malfunction stay set in real SECCs.
inline bool evse_requests_stop(const message_din::datatypes::DcEvseStatus& status) {
    namespace dt = message_din::datatypes;
    if (status.evse_notification == dt::EvseNotification::StopCharging) {
        return true;
    }
    return status.evse_status_code == dt::DcEvseStatusCode::EVSE_Shutdown or
           status.evse_status_code == dt::DcEvseStatusCode::EVSE_EmergencyShutdown;
}

// Before PowerDelivery(Start): an EV stop or pause goes straight to SessionStop.
inline std::optional<Result> stop_before_start(Context& ctx) {
    if (const auto reason = session::requested_by_ev(ctx)) {
        return Result{state::to_session_stop(ctx, *reason)};
    }
    return std::nullopt;
}

// As above, and an EVSE stop terminates the session, even over an EV pause. SessionStopReq is in
// sequence after CableCheckRes [V2G-DC-965] and PreChargeRes [V2G-DC-458].
inline std::optional<Result> stop_before_start(Context& ctx, const char* state_name,
                                               const message_din::datatypes::DcEvseStatus& status) {
    if (evse_requests_stop(status)) {
        const auto level = status.evse_status_code == message_din::datatypes::DcEvseStatusCode::EVSE_EmergencyShutdown
                               ? LogLevel::Error
                               : LogLevel::Warning;
        logf(level, "%s: SECC requested a stop (EVSEStatusCode %d, EVSENotification %d), transitioning to SessionStop",
             state_name, static_cast<int>(status.evse_status_code), static_cast<int>(status.evse_notification));
        return Result{state::to_session_stop(ctx, session::StopReason::EvseTerminate)};
    }
    return stop_before_start(ctx);
}

} // namespace iso15118::ev::din
