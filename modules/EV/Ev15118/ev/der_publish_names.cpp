// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "der_publish_names.hpp"

#include <cstddef>

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/enum_names.hpp>
#include <iso15118/ev/der_control_functions.hpp>
#include <iso15118/sae_modes.hpp>

namespace module {

namespace {

using iso15118::ev::DER_CONTROL_FUNCTION_COUNT;
using iso15118::iec::DERControlName;

template <typename Enum> constexpr std::uint32_t bit_of(Enum function) {
    return 1U << static_cast<std::uint32_t>(function);
}

// sae_der_function_names walks the table in its own order and promises ascending bit order.
constexpr bool sae_function_names_ascending() {
    const auto& table = iso15118::sae::SAE_FUNCTION_NAMES;
    for (std::size_t index = 1; index < table.size(); ++index) {
        if (bit_of(table[index - 1].function) >= bit_of(table[index].function)) {
            return false;
        }
    }
    return true;
}
static_assert(sae_function_names_ascending(), "SAE_FUNCTION_NAMES must list the functions in ascending bit order");

template <typename Enum, std::size_t Count, typename NameOf>
std::vector<std::string> names_of(NameOf name_of, std::uint32_t bitmap) {
    std::vector<std::string> names;
    iso15118::for_each_enum_value<Enum, Count>(name_of, [&names, name_of, bitmap](Enum function) {
        if ((bitmap & bit_of(function)) != 0) {
            names.emplace_back(name_of(function));
        }
    });
    return names;
}

} // namespace

std::vector<std::string> sae_der_function_names(std::uint32_t bitmap) {
    std::vector<std::string> names;
    for (const auto& entry : iso15118::sae::SAE_FUNCTION_NAMES) {
        if ((bitmap & bit_of(entry.function)) != 0) {
            names.emplace_back(entry.name);
        }
    }
    return names;
}

std::vector<std::string> iec_der_function_names(std::uint32_t bitmap) {
    return names_of<DERControlName, DER_CONTROL_FUNCTION_COUNT>(iso15118::iec::der_control_name, bitmap);
}

std::optional<types::iso15118::DerFlavor> der_flavor_for(iso15118::message_20::datatypes::ServiceCategory service) {
    using iso15118::message_20::datatypes::ServiceCategory;
    if (service == ServiceCategory::AC_DER_IEC) {
        return types::iso15118::DerFlavor::AC_DER_IEC;
    }
    if (service == ServiceCategory::AC_DER_SAE) {
        return types::iso15118::DerFlavor::AC_DER_SAE;
    }
    return std::nullopt;
}

types::iso15118::DerNegotiatedFunctions der_negotiated_functions(types::iso15118::DerFlavor flavor,
                                                                 std::uint32_t bitmap) {
    const bool sae = flavor == types::iso15118::DerFlavor::AC_DER_SAE;
    types::iso15118::DerNegotiatedFunctions negotiated;
    negotiated.flavor = flavor;
    negotiated.functions = sae ? sae_der_function_names(bitmap) : iec_der_function_names(bitmap);
    return negotiated;
}

types::iso15118::DerControlReceived sae_der_control_received(types::iso15118::DerControlSource source,
                                                             bool permit_service, std::uint32_t enabled,
                                                             const iso15118::ev::DerControlProblems& problems) {
    types::iso15118::DerControlReceived received;
    received.flavor = types::iso15118::DerFlavor::AC_DER_SAE;
    received.source = source;
    received.permit_service = permit_service;
    received.enabled_functions = sae_der_function_names(enabled);
    if (not problems.empty()) {
        received.problems = problems;
    }
    return received;
}

} // namespace module
