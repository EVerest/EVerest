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

struct DynamicSetChoice {
    uint16_t id{};
    // Unset when no AC connector was looked for (DC).
    std::optional<message_20::datatypes::AcConnector> connector;
    std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> mask;
};

// An AC set without a readable Connector cannot be matched; treat it as single-phase, the
// reading under which the base element is never a sum.
message_20::datatypes::AcConnector ac_connector_of(const message_20::datatypes::ParameterSet& set) {
    return get_connector(set).value_or(message_20::datatypes::AcConnector::SinglePhase);
}

// First offered Dynamic set whose Connector matches \p preferred, else the first Dynamic set;
// nullopt if none is Dynamic. The EVSE lists SinglePhase first even on three-phase hardware, so
// taking the first Dynamic set unconditionally would put a three-phase EV on one line. Without a
// preference the first Dynamic set wins and no connector is recorded.
std::optional<DynamicSetChoice>
find_dynamic_parameter_set(const message_20::datatypes::ServiceParameterList& sets,
                           std::optional<message_20::datatypes::AcConnector> preferred) {
    std::optional<DynamicSetChoice> fallback;
    for (const auto& set : sets) {
        const auto control_mode = get_int_parameter(set, "ControlMode");
        if (control_mode != message_20::to_underlying_value(message_20::datatypes::ControlMode::Dynamic)) {
            continue;
        }
        if (not preferred.has_value()) {
            return DynamicSetChoice{set.id, std::nullopt, {}};
        }
        const auto connector = ac_connector_of(set);
        if (connector == *preferred) {
            return DynamicSetChoice{set.id, connector, {}};
        }
        if (not fallback.has_value()) {
            fallback = DynamicSetChoice{set.id, connector, {}};
        }
    }
    return fallback;
}

bool is_dynamic(const message_20::datatypes::ParameterSet& set) {
    return get_int_parameter(set, "ControlMode") ==
           message_20::to_underlying_value(message_20::datatypes::ControlMode::Dynamic);
}

struct DerControlFunctionsOffer {
    std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> mask;
    // Set when the offer names functions the EV cannot model: bits at or above
    // DER_CONTROL_FUNCTION_COUNT (dropped by the bitset constructor) or a
    // DERControlFunctions value that is not an integer.
    bool has_unknown_functions{false};
};

DerControlFunctionsOffer get_der_control_functions(const message_20::datatypes::ParameterSet& set) {
    const auto present = std::any_of(set.parameter.begin(), set.parameter.end(),
                                     [](const auto& parameter) { return parameter.name == "DERControlFunctions"; });
    if (not present) {
        return {};
    }
    const auto value = get_int_parameter(set, "DERControlFunctions");
    if (not value.has_value()) {
        // Present but not an integer: the offer names functions the EV cannot read.
        logf_warning("AC_DER_IEC parameter set %u carries a non-integer DERControlFunctions value", set.id);
        return DerControlFunctionsOffer{{}, true};
    }
    const auto raw = static_cast<unsigned long long>(static_cast<uint32_t>(value.value()));
    constexpr auto known_functions = (1ULL << ev::DER_CONTROL_FUNCTION_COUNT) - 1ULL;
    return {std::bitset<ev::DER_CONTROL_FUNCTION_COUNT>(raw), (raw & ~known_functions) != 0ULL};
}

std::string_view der_function_name(std::size_t index) {
    using iso15118::iec::DERControlName;
    switch (static_cast<DERControlName>(index)) {
    case DERControlName::OverFrequencyWattMode:
        return "OverFrequencyWattMode";
    case DERControlName::UnderFrequencyWattMode:
        return "UnderFrequencyWattMode";
    case DERControlName::VoltWattMode:
        return "VoltWattMode";
    case DERControlName::VoltVarMode:
        return "VoltVarMode";
    case DERControlName::WattVarMode:
        return "WattVarMode";
    case DERControlName::WattCosPhiMode:
        return "WattCosPhiMode";
    case DERControlName::DSOQSetpointProvision:
        return "DSOQSetpointProvision";
    case DERControlName::DSOCosPhiSetpointProvision:
        return "DSOCosPhiSetpointProvision";
    case DERControlName::DCInjectionRestriction:
        return "DCInjectionRestriction";
    case DERControlName::ZeroCurrentMode:
        return "ZeroCurrentMode";
    case DERControlName::OverVoltageFaultRideThroughMode:
        return "OverVoltageFaultRideThroughMode";
    case DERControlName::UnderVoltageFaultRideThroughMode:
        return "UnderVoltageFaultRideThroughMode";
    }
    return "Unknown";
}

std::string describe_functions(const std::bitset<ev::DER_CONTROL_FUNCTION_COUNT>& bits) {
    std::string out;
    for (std::size_t index = 0; index < bits.size(); ++index) {
        if (bits.test(index)) {
            if (not out.empty()) {
                out += ", ";
            }
            out += der_function_name(index);
        }
    }
    return out;
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

    // AC_DER_IEC negotiates control functions: prefer the first Dynamic set whose
    // DERControlFunctions are a subset of what the EV supports.
    if (m_ctx.selected_service() == message_20::datatypes::ServiceCategory::AC_DER_IEC) {
        const auto supported = m_ctx.der_supported_functions();
        // AC_DER_IEC is an AC service, so a preference always exists here.
        const auto der_preferred = preferred_connector(m_ctx).value();
        std::optional<DynamicSetChoice> first_dynamic;
        DerControlFunctionsOffer first_dynamic_offer;
        // DER function support is the primary key; the connector only breaks ties between sets
        // that are already acceptable, so a matching connector never costs a supported function.
        std::optional<DynamicSetChoice> acceptable;

        // Records the negotiated mask and connector of the one set the EV goes on with.
        const auto select = [this](const DynamicSetChoice& choice) {
            m_ctx.set_der_negotiated_functions(choice.mask);
            m_ctx.set_selected_ac_connector(choice.connector.value());
            return m_ctx.create_state<ServiceSelection>(choice.id);
        };

        for (const auto& set : res->service_parameter_list) {
            if (not is_dynamic(set)) {
                continue;
            }
            const auto offer = get_der_control_functions(set);
            const DynamicSetChoice choice{set.id, ac_connector_of(set), offer.mask & supported};
            if (not first_dynamic.has_value()) {
                first_dynamic = choice;
                first_dynamic_offer = offer;
            }
            if ((offer.mask & ~supported).none() and not offer.has_unknown_functions) {
                if (choice.connector == der_preferred) {
                    return select(choice);
                }
                if (not acceptable.has_value()) {
                    acceptable = choice;
                }
            }
        }

        if (acceptable.has_value()) {
            logf_warning("No AC_DER_IEC set offers both the supported DER functions and the preferred %s connector; "
                         "selecting the %s set",
                         connector_name(der_preferred), connector_name(acceptable->connector.value()));
            return select(*acceptable);
        }

        if (not first_dynamic.has_value()) {
            logf_error("AC_DER_IEC ServiceDetailResponse offers no Dynamic control-mode parameter set");
            m_ctx.stop_session();
            return Result::stopping();
        }

        auto unsupported = describe_functions(first_dynamic_offer.mask & ~supported);
        if (first_dynamic_offer.has_unknown_functions) {
            if (not unsupported.empty()) {
                unsupported += ", ";
            }
            unsupported += "unknown function bits";
        }
        if (m_ctx.der_stop_on_unsupported_functions()) {
            logf_error("AC_DER_IEC offers no set within the supported DER functions (unsupported: %s); stopping",
                       unsupported.c_str());
            m_ctx.stop_session();
            return Result::stopping();
        }

        logf_warning("AC_DER_IEC offers no set within the supported DER functions (unsupported: %s); "
                     "selecting the first Dynamic set anyway",
                     unsupported.c_str());
        return select(*first_dynamic);
    }

    const auto preferred = preferred_connector(m_ctx);
    const auto dynamic_set = find_dynamic_parameter_set(res->service_parameter_list, preferred);
    if (not dynamic_set.has_value()) {
        logf_error("ServiceDetailResponse offers no Dynamic control-mode parameter set");
        m_ctx.stop_session();
        return Result::stopping();
    }

    if (preferred.has_value()) {
        if (dynamic_set->connector != preferred) {
            logf_warning("No Dynamic parameter set offers the preferred %s connector; selecting %s, and the "
                         "advertised limits are split for that connector",
                         connector_name(*preferred), connector_name(dynamic_set->connector.value()));
        }
        m_ctx.set_selected_ac_connector(dynamic_set->connector.value());
    }
    return m_ctx.create_state<ServiceSelection>(dynamic_set->id);
}

} // namespace iso15118::ev::d20::state
