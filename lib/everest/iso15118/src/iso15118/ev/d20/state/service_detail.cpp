// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <bitset>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/enum_names.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/service_detail.hpp>
#include <iso15118/ev/d20/state/service_selection.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/der_control_functions.hpp>
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

std::optional<message_20::datatypes::AcConnector> get_connector(const message_20::datatypes::ParameterSet& set) {
    const auto value = get_int_parameter(set, "Connector");
    if (not value.has_value()) {
        return std::nullopt;
    }
    switch (*value) {
    case message_20::to_underlying_value(message_20::datatypes::AcConnector::SinglePhase):
        return message_20::datatypes::AcConnector::SinglePhase;
    case message_20::to_underlying_value(message_20::datatypes::AcConnector::ThreePhase):
        return message_20::datatypes::AcConnector::ThreePhase;
    default:
        return std::nullopt;
    }
}

const char* connector_name(message_20::datatypes::AcConnector connector) {
    return (connector == message_20::datatypes::AcConnector::ThreePhase) ? "ThreePhase" : "SinglePhase";
}

// The AC connector the EV would rather have, given its own line count. DC sets carry a
// "Connector" parameter too, with DcConnector values, so there is no preference outside AC.
std::optional<message_20::datatypes::AcConnector> preferred_connector(const Context& ctx) {
    if (not ctx.is_ac_family()) {
        return std::nullopt;
    }
    return (ctx.get_ac_params().phase_count == 3) ? message_20::datatypes::AcConnector::ThreePhase
                                                  : message_20::datatypes::AcConnector::SinglePhase;
}

// One offered parameter set, reduced to what the selection turns on.
struct SetChoice {
    uint16_t id{};
    message_20::datatypes::ControlMode control_mode{};
    // Unset when no AC connector was looked for (DC).
    std::optional<message_20::datatypes::AcConnector> connector;
};

// An AC set without a readable Connector cannot be matched; treat it as single-phase, the
// reading under which the base element is never a sum.
message_20::datatypes::AcConnector ac_connector_of(const message_20::datatypes::ParameterSet& set) {
    return get_connector(set).value_or(message_20::datatypes::AcConnector::SinglePhase);
}

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

// Ranking: preferred control mode, then matching connector, then offer order. The EVSE lists
// SinglePhase first even on three-phase hardware. \p acceptable rejects sets the service cannot
// use at all, so a rejected set never wins on connector or on offer order.
template <typename Acceptable>
std::optional<SetChoice> find_parameter_set(const message_20::datatypes::ServiceParameterList& sets,
                                            message_20::datatypes::ControlMode preferred_mode,
                                            std::optional<message_20::datatypes::AcConnector> preferred_connector,
                                            Acceptable acceptable) {
    std::optional<SetChoice> preferred_matched;
    std::optional<SetChoice> preferred_first;
    std::optional<SetChoice> other_matched;
    std::optional<SetChoice> other_first;

    for (const auto& set : sets) {
        const auto control_mode = get_control_mode(set);
        if (not control_mode.has_value()) {
            continue;
        }
        if (not acceptable(set)) {
            continue;
        }
        std::optional<message_20::datatypes::AcConnector> connector;
        if (preferred_connector.has_value()) {
            connector = ac_connector_of(set);
        }
        const SetChoice choice{set.id, *control_mode, connector};
        auto& matched = (*control_mode == preferred_mode) ? preferred_matched : other_matched;
        auto& first = (*control_mode == preferred_mode) ? preferred_first : other_first;
        if (not first.has_value()) {
            first = choice;
        }
        if (connector.has_value() and connector == preferred_connector and not matched.has_value()) {
            matched = choice;
        }
    }

    if (preferred_matched.has_value()) {
        return preferred_matched;
    }
    if (preferred_first.has_value()) {
        return preferred_first;
    }
    if (other_matched.has_value()) {
        return other_matched;
    }
    return other_first;
}

struct DerControlFunctionsDemand {
    std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> mask;
    // Set when the demand names functions the EV cannot model: bits at or above
    // DER_CONTROL_FUNCTION_COUNT (dropped by the bitset constructor) or a
    // DERControlFunctions value that is not an integer.
    bool has_unknown_functions{false};
};

// [V2G20-3190]: the SECC states the functions it demands the EV support, not an opening offer.
DerControlFunctionsDemand get_der_control_functions(const message_20::datatypes::ParameterSet& set) {
    const auto present = std::any_of(set.parameter.begin(), set.parameter.end(),
                                     [](const auto& parameter) { return parameter.name == "DERControlFunctions"; });
    if (not present) {
        return {};
    }
    const auto value = get_int_parameter(set, "DERControlFunctions");
    if (not value.has_value()) {
        // Present but not an integer: the demand names functions the EV cannot read.
        logf_warning("AC_DER_IEC parameter set %u carries a non-integer DERControlFunctions value", set.id);
        return DerControlFunctionsDemand{{}, true};
    }
    const auto raw = static_cast<unsigned long long>(static_cast<uint32_t>(value.value()));
    constexpr auto known_functions = (1ULL << ev::DER_CONTROL_FUNCTION_COUNT) - 1ULL;
    return {std::bitset<ev::DER_CONTROL_FUNCTION_COUNT>(raw), (raw & ~known_functions) != 0ULL};
}

DerControlFunctionsDemand demand_of(const message_20::datatypes::ServiceParameterList& sets, uint16_t id) {
    for (const auto& set : sets) {
        if (set.id == id) {
            return get_der_control_functions(set);
        }
    }
    return {};
}

std::string describe_functions(const std::bitset<ev::DER_CONTROL_FUNCTION_COUNT>& bits) {
    std::string out;
    for_each_enum_value<iec::DERControlName, ev::DER_CONTROL_FUNCTION_COUNT>(
        iec::der_control_name, [&out, &bits](iec::DERControlName function) {
            if (not bits.test(static_cast<std::size_t>(function))) {
                return;
            }
            if (not out.empty()) {
                out += ", ";
            }
            out += iec::der_control_name(function);
        });
    return out;
}

} // namespace

void ServiceDetail::enter() {
    logf_debug("Enter state: ServiceDetail");

    message_20::ServiceDetailRequest req;
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

    const auto preferred = preferred_connector(m_ctx);
    const bool der_iec = (m_ctx.selected_service() == message_20::datatypes::ServiceCategory::AC_DER_IEC);
    const bool der_sae = (m_ctx.selected_service() == message_20::datatypes::ServiceCategory::AC_DER_SAE);

    // The AC_DER_SAE ChargeLoop drives Dynamic only.
    const auto preferred_mode = der_sae ? message_20::datatypes::ControlMode::Dynamic : m_ctx.preferred_control_mode();
    if (der_sae and m_ctx.preferred_control_mode() != preferred_mode) {
        logf_warning("AC_DER_SAE supports only the Dynamic control mode; ignoring the preferred %s control mode",
                     control_mode_name(m_ctx.preferred_control_mode()));
    }

    const auto accept_any = [](const message_20::datatypes::ParameterSet&) { return true; };

    std::optional<SetChoice> selected_set;
    if (der_sae) {
        // AMD1 Table M.54 [V2G20-3280]: the plain AC parameters, no DER ones. The SAE modes are
        // negotiated later, in DERControlCPDRes.
        const auto dynamic_only = [](const message_20::datatypes::ParameterSet& set) {
            return get_control_mode(set) == message_20::datatypes::ControlMode::Dynamic;
        };
        selected_set = find_parameter_set(res->service_parameter_list, preferred_mode, preferred, dynamic_only);
        if (not selected_set.has_value()) {
            logf_error("AC_DER_SAE needs a Dynamic parameter set, but ServiceDetailResponse offers none; stopping");
            m_ctx.stop_session();
            return Result::stopping();
        }
    } else if (der_iec) {
        // [V2G20-3191]: the EV may only select AC_DER_IEC when it supports every demanded
        // function, so an unsupported set is not a candidate at all.
        const auto supported = m_ctx.der_supported_functions();
        const auto functions_supported = [&supported](const message_20::datatypes::ParameterSet& set) {
            const auto demand = get_der_control_functions(set);
            return (demand.mask & ~supported).none() and not demand.has_unknown_functions;
        };

        selected_set = find_parameter_set(res->service_parameter_list, preferred_mode, preferred, functions_supported);
        if (not selected_set.has_value()) {
            const auto fallback =
                find_parameter_set(res->service_parameter_list, preferred_mode, preferred, accept_any);
            if (not fallback.has_value()) {
                logf_error("ServiceDetailResponse offers no parameter set with a readable control mode");
                m_ctx.stop_session();
                return Result::stopping();
            }

            const auto demand = demand_of(res->service_parameter_list, fallback->id);
            auto unsupported = describe_functions(demand.mask & ~supported);
            if (demand.has_unknown_functions) {
                if (not unsupported.empty()) {
                    unsupported += ", ";
                }
                unsupported += "unknown function bits";
            }
            if (m_ctx.der_stop_on_unsupported_functions()) {
                logf_error("AC_DER_IEC demands DER functions the EV does not support (%s); stopping",
                           unsupported.c_str());
                m_ctx.stop_session();
                return Result::stopping();
            }

            logf_warning("AC_DER_IEC demands DER functions the EV does not support (%s); selecting set %u anyway, "
                         "which deviates from [V2G20-3191]",
                         unsupported.c_str(), fallback->id);
            selected_set = fallback;
        }
        const auto negotiated = demand_of(res->service_parameter_list, selected_set->id).mask;
        m_ctx.set_der_demanded_functions(negotiated);
        m_ctx.feedback.der_enabled_modes(static_cast<std::uint32_t>(negotiated.to_ulong()));
    } else {
        selected_set = find_parameter_set(res->service_parameter_list, preferred_mode, preferred, accept_any);
    }

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

    if (preferred.has_value()) {
        if (selected_set->connector != preferred) {
            logf_warning("No %s parameter set offers the preferred %s connector; selecting %s, and the "
                         "advertised limits are split for that connector",
                         control_mode_name(selected_set->control_mode), connector_name(*preferred),
                         connector_name(selected_set->connector.value()));
        }
        m_ctx.set_selected_ac_connector(selected_set->connector.value());
    }
    return m_ctx.create_state<ServiceSelection>(selected_set->id);
}

} // namespace iso15118::ev::d20::state
