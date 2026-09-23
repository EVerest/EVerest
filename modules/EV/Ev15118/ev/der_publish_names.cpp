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
using iso15118::sae::DerBitMapFunctions;
using iso15118::sae::SAE_BITMAP_BITS;

template <typename Enum> constexpr std::uint32_t bit_of(Enum function) {
    return 1U << static_cast<std::uint32_t>(function);
}

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
    return names_of<DerBitMapFunctions, SAE_BITMAP_BITS>(iso15118::sae::sae_function_name, bitmap);
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
