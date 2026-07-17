// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/ev/state/service_detail.hpp>

#include <optional>

#include <iso15118/d20/ev/state/service_selection.hpp>
#include <iso15118/d20/ev/timeouts.hpp>

#include <iso15118/detail/d20/ev/state/service_detail.hpp>
#include <iso15118/detail/d20/ev/state/state_helper.hpp>
#include <iso15118/detail/helper.hpp>

namespace iso15118::d20::ev::state {

namespace {

std::optional<int32_t> parameter_as_int(const dt::Parameter& parameter) {
    if (const auto* v = std::get_if<int32_t>(&parameter.value)) {
        return *v;
    }
    if (const auto* v = std::get_if<int16_t>(&parameter.value)) {
        return *v;
    }
    if (const auto* v = std::get_if<int8_t>(&parameter.value)) {
        return *v;
    }
    return std::nullopt;
}

struct ParsedParameterSet {
    uint16_t id{0};
    std::optional<dt::ControlMode> control_mode;
    dt::MobilityNeedsMode mobility_needs_mode{dt::MobilityNeedsMode::ProvidedByEvcc};
};

ParsedParameterSet parse_parameter_set(const dt::ParameterSet& set) {
    ParsedParameterSet parsed;
    parsed.id = set.id;
    for (const auto& parameter : set.parameter) {
        const auto value = parameter_as_int(parameter);
        if (not value.has_value()) {
            continue;
        }
        if (parameter.name == "ControlMode") {
            parsed.control_mode = static_cast<dt::ControlMode>(*value);
        } else if (parameter.name == "MobilityNeedsMode") {
            parsed.mobility_needs_mode = static_cast<dt::MobilityNeedsMode>(*value);
        }
    }
    return parsed;
}

} // namespace

namespace service_detail {

message_20::ServiceDetailRequest create_request(uint16_t service_id) {
    message_20::ServiceDetailRequest req;
    req.service = service_id;
    return req;
}

Result handle_response(const message_20::ServiceDetailResponse& res, dt::ControlMode preferred_mode) {
    Result result;
    result.valid = (res.response_code < dt::ResponseCode::FAILED);
    if (not result.valid) {
        return result;
    }

    std::optional<ParsedParameterSet> fallback;

    for (const auto& set : res.service_parameter_list) {
        const auto parsed = parse_parameter_set(set);
        if (not parsed.control_mode.has_value()) {
            continue;
        }
        if (*parsed.control_mode == preferred_mode) {
            result.control_mode_found = true;
            result.parameter_set_id = parsed.id;
            result.control_mode = *parsed.control_mode;
            result.mobility_needs_mode = parsed.mobility_needs_mode;
            return result;
        }
        if (not fallback.has_value()) {
            fallback = parsed;
        }
    }

    if (fallback.has_value()) {
        result.control_mode_found = true;
        result.parameter_set_id = fallback->id;
        result.control_mode = fallback->control_mode.value();
        result.mobility_needs_mode = fallback->mobility_needs_mode;
    }

    return result;
}

} // namespace service_detail

using namespace service_detail;

void ServiceDetail::enter() {
    m_ctx.log.enter_state("ServiceDetail");
}

d20::ev::Result ServiceDetail::feed(Event ev) {
    if (ev == Event::SEND_REQUEST) {
        auto req = create_request(message_20::to_underlying_value(m_ctx.evse_info.selected_energy_service));
        m_ctx.setup_header(req.header);
        m_ctx.send_request(req);
        m_ctx.start_timeout(d20::TimeoutType::SEQUENCE, MESSAGE_TIMEOUT_SERVICE_DETAIL_MS);
        return {};
    }

    if (ev == Event::CONTROL_MESSAGE) {
        handle_stop_control_event(m_ctx);
        return {};
    }

    if (ev == Event::TIMEOUT) {
        m_ctx.log("ServiceDetail message timeout reached, terminating session");
        m_ctx.session_stopped = true;
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    m_ctx.stop_timeout(d20::TimeoutType::SEQUENCE);

    const auto variant = m_ctx.pull_response();

    if (const auto res = variant->get_if<message_20::ServiceDetailResponse>()) {
        const auto result = handle_response(*res, m_ctx.session_config.preferred_control_mode);

        if (not result.valid) {
            m_ctx.log("ServiceDetail failed, terminating session");
            m_ctx.session_stopped = true;
            return {};
        }

        m_ctx.evse_info.offered_parameter_sets[res->service] = res->service_parameter_list;

        if (not result.control_mode_found) {
            m_ctx.log("ServiceDetail response has no usable control mode, stopping session");
            m_ctx.pending_stop = dt::ChargingSession::Terminate;
            return m_ctx.create_state<SessionStop>();
        }

        m_ctx.evse_info.selected_parameter_set_id = result.parameter_set_id;
        m_ctx.evse_info.selected_control_mode = result.control_mode;
        m_ctx.evse_info.selected_mobility_needs_mode = result.mobility_needs_mode;

        if (auto stop = stop_if_pending(m_ctx)) {
            return stop;
        }
        return m_ctx.create_state<ServiceSelection>();
    }

    m_ctx.log("expected ServiceDetailRes! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;
    return {};
}

} // namespace iso15118::d20::ev::state
