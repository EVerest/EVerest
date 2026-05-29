// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/state/authorization.hpp>
#include <iso15118/d2/state/charge_parameter_discovery.hpp>
#include <iso15118/message/d2/authorization.hpp>
#include <iso15118/message/d2/session_stop.hpp>

#include <iso15118/detail/d2/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <optional>
#include <vector>

// TODO(kd): Change this after files are moved to a common place
#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
typedef ::iso15118::d20::AuthorizationResponse AuthorizationResponse;
typedef ::iso15118::d20::TIMEOUT_ONGOING TIMEOUT_ONGOING;

namespace iso15118::d2::state {

namespace dt = d2::msg::data_types;

d2::msg::AuthorizationResponse handle_request([[maybe_unused]] const d2::msg::AuthorizationRequest& req,
                                              d2::Session& session, bool authorization_pending, bool authorized,
                                              bool timeout_ongoing_reached) {
    d2::msg::AuthorizationResponse res;
    setup_header(res.header, session);

    if (timeout_ongoing_reached) {
        // [V2G2-713]
        return response_with_code(res, dt::ResponseCode::FAILED);
    }

    if (authorization_pending) {
        // [V2G2-687]
        res.evse_processing = dt::EvseProcessing::Ongoing;
        return response_with_code(res, dt::ResponseCode::OK);
    }

    if (authorized) {
        // [V2G2-563]
        res.evse_processing = dt::EvseProcessing::Finished;
        return response_with_code(res, dt::ResponseCode::OK);
    } else {
        // [V2G2-563]
        res.evse_processing = dt::EvseProcessing::Finished;
        return response_with_code(res, dt::ResponseCode::FAILED);
    }
}

// TODO(kd): Move to session_stop.cpp
d2::msg::SessionStopResponse handle_request([[maybe_unused]] const d2::msg::SessionStopRequest& req) {
    d2::msg::SessionStopResponse res;
    return response_with_code(res, dt::ResponseCode::OK);
}

void Authorization::enter() {
    // m_ctx.log.enter_state("Authorization");
}

Result Authorization::feed(Event ev) {
    if (ev == Event::CONTROL_MESSAGE) {
        const auto control_data = m_ctx.get_control_event<AuthorizationResponse>();

        if (not control_data) {
            // Ignore control message
            return {};
        }

        authorization_pending = false;
        authorized = *control_data;

        return {};
    }

    if (ev == Event::TIMEOUT) {
        const auto timeout = m_ctx.get_active_timeout();
        if (timeout and *timeout == d20::TimeoutType::ONGOING) {
            timeout_ongoing_reached = true;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto req = variant->get_if<msg::AuthorizationRequest>()) {

        if (first_req_msg) {
            // [V2G2-712] Start V2G_SECC_Ongoing_Timer
            m_ctx.start_timeout(d20::TimeoutType::ONGOING, TIMEOUT_ONGOING);
            first_req_msg = false;
        }

        const auto res =
            handle_request(*req, m_ctx.session, authorization_pending, authorized, timeout_ongoing_reached);

        m_ctx.respond(res);

        if (res.response_code >= dt::ResponseCode::FAILED) {
            // [V2G2-539] Terminate session after ResponseCode::FAILED
            m_ctx.session_stopped = true;
            return {};
        }

        if (authorized) {
            authorized = false; // reset
            m_ctx.stop_timeout(d20::TimeoutType::ONGOING);
            return m_ctx.create_state<ChargeParameterDiscovery>();
        } else {
            return {};
        }
    } else if (const auto req = variant->get_if<msg::SessionStopRequest>()) {
        const auto res = handle_request(*req);
        m_ctx.respond(res);

        m_ctx.session_stopped = true;
        return {};
    } else {
        // m_ctx.log("expected AuthorizationReq! But code type id: %d", variant->get_type());

        // Sequence Error [V2G2-538]
        const msg::Type req_type = variant->get_type();
        send_sequence_error(req_type, m_ctx);

        m_ctx.session_stopped = true;
        return {};
    }
}

} // namespace iso15118::d2::state
