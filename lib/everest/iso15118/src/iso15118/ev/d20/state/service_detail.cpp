// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <optional>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/message/service_detail.hpp>

namespace iso15118::ev::d20::state {

namespace {

std::optional<int32_t> get_int_parameter(const message_20::datatypes::ParameterSet& set, const char* name) {
    for (const auto& parameter : set.parameter) {
        if (parameter.name != name) {
            continue;
        }
        // The EXI decoder mirrors the peer's encoded integer width, so a narrowly
        // encoded ControlMode may arrive as int8_t or int16_t rather than int32_t.
        if (const auto* value = std::get_if<int8_t>(&parameter.value)) {
            return static_cast<int32_t>(*value);
        }
        if (const auto* value = std::get_if<int16_t>(&parameter.value)) {
            return static_cast<int32_t>(*value);
        }
        if (const auto* value = std::get_if<int32_t>(&parameter.value)) {
            return *value;
        }
    }
    return std::nullopt;
}

// One offered parameter set, reduced to what the selection turns on.
struct SetChoice {
    uint16_t id{};
    message_20::datatypes::ControlMode control_mode{};
};

std::optional<message_20::datatypes::ControlMode> get_control_mode(const message_20::datatypes::ParameterSet& set) {
    const auto value = get_int_parameter(set, "ControlMode");
    if (not value.has_value()) {
        return std::nullopt;
    }
    switch (*value) {
    case message_20::to_underlying_value(message_20::datatypes::ControlMode::Scheduled):
        return message_20::datatypes::ControlMode::Scheduled;
    case message_20::to_underlying_value(message_20::datatypes::ControlMode::Dynamic):
        return message_20::datatypes::ControlMode::Dynamic;
    default:
        return std::nullopt;
    }
}

const char* control_mode_name(message_20::datatypes::ControlMode mode) {
    return (mode == message_20::datatypes::ControlMode::Scheduled) ? "Scheduled" : "Dynamic";
}

// Ranking: preferred control mode, then offer order.
std::optional<SetChoice> find_parameter_set(const message_20::datatypes::ServiceParameterList& sets,
                                            message_20::datatypes::ControlMode preferred_mode) {
    std::optional<SetChoice> preferred_first;
    std::optional<SetChoice> other_first;

    for (const auto& set : sets) {
        const auto control_mode = get_control_mode(set);
        if (not control_mode.has_value()) {
            continue;
        }
        auto& first = (*control_mode == preferred_mode) ? preferred_first : other_first;
        if (not first.has_value()) {
            first = SetChoice{set.id, *control_mode};
        }
    }

    if (preferred_first.has_value()) {
        return preferred_first;
    }
    return other_first;
}

} // namespace

void ServiceDetail::enter() {
    logf_debug("Enter state: ServiceDetail");

    message_20::ServiceDetailRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.service = message_20::to_underlying_value(m_ctx.selected_service());
    m_ctx.send_request(req);
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ServiceDetailResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (res->service_parameter_list.empty()) {
        logf_error("ServiceDetailResponse carries no parameter sets");
        m_ctx.stop_session();
        return Result::stopping();
    }

    const auto preferred_mode = m_ctx.preferred_control_mode();
    const auto selected_set = find_parameter_set(res->service_parameter_list, preferred_mode);
    if (not selected_set.has_value()) {
        logf_error("ServiceDetailResponse offers no parameter set with a readable control mode");
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (selected_set->control_mode != preferred_mode) {
        logf_warning("No parameter set offers the preferred %s control mode; selecting a %s set",
                     control_mode_name(preferred_mode), control_mode_name(selected_set->control_mode));
    }
    m_ctx.set_selected_control_mode(selected_set->control_mode);

    return m_ctx.create_state<ServiceSelection>(selected_set->id);
}

} // namespace iso15118::ev::d20::state
