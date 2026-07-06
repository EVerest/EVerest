// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <cstdint>
#include <optional>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
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

// First offered set whose ControlMode parameter is Dynamic; nullopt if none.
std::optional<uint16_t> find_dynamic_parameter_set(const message_20::datatypes::ServiceParameterList& sets) {
    for (const auto& set : sets) {
        const auto control_mode = get_int_parameter(set, "ControlMode");
        if (control_mode == message_20::to_underlying_value(message_20::datatypes::ControlMode::Dynamic)) {
            return set.id;
        }
    }
    return std::nullopt;
}

} // namespace

void ServiceDetail::enter() {
    m_ctx.log.enter_state("ServiceDetail");

    message_20::ServiceDetailRequest req;
    setup_header(req.header, m_ctx.get_session());
    req.service = message_20::to_underlying_value(m_ctx.selected_service());
    m_ctx.respond(req);
}

Result ServiceDetail::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::ServiceDetailResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return {};
    }

    if (res->service_parameter_list.empty()) {
        logf_error("ServiceDetailResponse carries no parameter sets");
        m_ctx.stop_session();
        return {};
    }

    const auto dynamic_set = find_dynamic_parameter_set(res->service_parameter_list);
    if (not dynamic_set.has_value()) {
        logf_error("ServiceDetailResponse offers no Dynamic control-mode parameter set");
        m_ctx.stop_session();
        return {};
    }

    return m_ctx.create_state<ServiceSelection>(*dynamic_set);
}

} // namespace iso15118::ev::d20::state
