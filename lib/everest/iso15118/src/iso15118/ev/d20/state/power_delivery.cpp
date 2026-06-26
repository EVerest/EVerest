// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/power_delivery.hpp>

#include <optional>

namespace iso15118::ev::d20::state {

namespace {

using ResponseCode = message_20::datatypes::ResponseCode;

bool check_response_code(ResponseCode response_code) {
    switch (response_code) {
    case ResponseCode::OK:
    case ResponseCode::OK_PowerToleranceConfirmed:
        return true;
    case ResponseCode::WARNING_PowerToleranceNotConfirmed:
    case ResponseCode::WARNING_StandbyNotAllowed:
    case ResponseCode::WARNING_WPT:
        // TODO: spec-defined recoveries (fall back to agreed profile / renegotiate /
        // WPT_FinePositioningSetupReq). Accept for now; recovery flows tracked separately.
        logf_warning("PowerDeliveryResponse warning code: %d", static_cast<int>(response_code));
        return true;
    case ResponseCode::FAILED:
    case ResponseCode::FAILED_SequenceError:
    case ResponseCode::FAILED_UnknownSession:
    case ResponseCode::FAILED_SignatureError:
    case ResponseCode::FAILED_ContactorError:
    case ResponseCode::FAILED_EVPowerProfileInvalid:
    case ResponseCode::FAILED_ScheduleSelectionInvalid:
    case ResponseCode::FAILED_PowerDeliveryNotApplied:
    case ResponseCode::FAILED_PowerToleranceNotConfirmed:
        return false;
    default:
        logf_warning("Unexpected response code received: %d", static_cast<int>(response_code));
        return iso15118::ev::d20::check_response_code(response_code);
    }
}

message_20::PowerDeliveryRequest
make_request(const SessionId& session, message_20::datatypes::Progress charge_progress,
             std::optional<message_20::datatypes::Processing> processing = std::nullopt) {
    message_20::PowerDeliveryRequest req;
    setup_header(req.header, session);
    req.processing = processing.value_or(message_20::datatypes::Processing::Finished);
    req.charge_progress = charge_progress;
    // power_profile + channel_selection deliberately left nullopt
    return req;
}

} // namespace

void PowerDelivery::enter() {
    m_ctx.respond(make_request(m_ctx.get_session(), m_charge_progress));
}

Result PowerDelivery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto res = variant->get_if<message_20::PowerDeliveryResponse>();
    if (res == nullptr) {
        logf_error("Expected PowerDeliveryResponse, got code type id: %d", static_cast<int>(variant->get_type()));
        m_ctx.stop_session(true);
        return {};
    }

    if (res->header.session_id != m_ctx.get_session().get_id()) {
        logf_error("PowerDeliveryResponse session_id does not match current session");
        m_ctx.stop_session(true);
        return {};
    }

    if (not check_response_code(res->response_code)) {
        logf_error("PowerDeliveryResponse rejected with response_code: %d", static_cast<int>(res->response_code));
        m_ctx.stop_session(true);
        return {};
    }

    using Progress = message_20::datatypes::Progress;
    switch (m_charge_progress) {
    case Progress::Start:
        // Next state: DC_ChargeLoop (not yet implemented)
        return {};
    case Progress::Stop:
        // Next state: DC_WeldingDetection or SessionStop, decided by the caller's energy-service selection
        return {};
    case Progress::Standby:
        // Stay in PowerDelivery; see evse-side state for re-entry pattern
        return {};
    case Progress::ScheduleRenegotiation:
        // Next state: ScheduleExchange (not yet implemented)
        return {};
    }
    return {};
}

} // namespace iso15118::ev::d20::state
