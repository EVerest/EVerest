// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/d2/vas.hpp>

#include <algorithm>
#include <variant>

#include <iso15118/detail/helper.hpp>
#include <iso15118/message/common_types.hpp>

namespace iso15118::d2 {

bool is_offered_vas(const m2dt::ServiceList& offered, uint16_t service_id) {
    return std::any_of(offered.begin(), offered.end(),
                       [service_id](const m2dt::Service& s) { return s.service_id == service_id; });
}

namespace {

m2dt::Parameter to_iso2_parameter(const m20dt::Parameter& in) {
    m2dt::Parameter out;
    out.name = in.name;
    std::visit(
        [&out](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, bool>) {
                out.bool_value = value;
            } else if constexpr (std::is_same_v<T, int8_t>) {
                out.byte_value = value;
            } else if constexpr (std::is_same_v<T, int16_t>) {
                out.short_value = value;
            } else if constexpr (std::is_same_v<T, int32_t>) {
                out.int_value = value;
            } else if constexpr (std::is_same_v<T, m20dt::Name>) {
                out.string_value = value;
            } else if constexpr (std::is_same_v<T, m20dt::RationalNumber>) {
                // The VAS interface has no unit for a rational number; W mirrors EvseV2G.
                out.physical_value = m2dt::to_physical_value(m20dt::from_RationalNumber(value), m2dt::Unit::W);
            }
        },
        in.value);
    return out;
}

} // namespace

std::optional<m2dt::ServiceParameterList> to_iso2_parameter_list(const m20dt::ServiceParameterList& in) {
    if (in.empty()) {
        return std::nullopt;
    }

    m2dt::ServiceParameterList out;
    for (const auto& set : in) {
        auto* out_set = out.try_emplace_back();
        if (out_set == nullptr) {
            logf_warning("VAS provider returned %zu parameter sets; ISO 15118-2 carries at most %zu, dropping the rest",
                         in.size(), out.max_size());
            break;
        }
        out_set->parameter_set_id = static_cast<int16_t>(set.id);
        for (const auto& parameter : set.parameter) {
            if (out_set->parameter.try_emplace_back(to_iso2_parameter(parameter)) == nullptr) {
                logf_warning("VAS parameter set %u has %zu parameters; ISO 15118-2 carries at most %zu, dropping "
                             "the rest",
                             set.id, set.parameter.size(), out_set->parameter.max_size());
                break;
            }
        }
    }
    return out;
}

m20dt::VasSelectedServiceList selected_vas_services(const m2dt::SelectedServiceList& selected,
                                                    const m2dt::ServiceList& offered) {
    m20dt::VasSelectedServiceList out;
    for (const auto& s : selected) {
        if (not is_offered_vas(offered, s.service_id)) {
            continue;
        }
        const auto parameter_set_id = static_cast<uint16_t>(s.parameter_set_id.value_or(0));
        if (out.try_emplace_back(m20dt::VasSelectedService{s.service_id, parameter_set_id}) == nullptr) {
            break;
        }
    }
    return out;
}

} // namespace iso15118::d2
