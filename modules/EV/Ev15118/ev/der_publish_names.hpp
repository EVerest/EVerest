// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <generated/types/iso15118.hpp>

#include <iso15118/ev/der_sae_control_validation.hpp>
#include <iso15118/message/common_types.hpp>

namespace module {

// Names of the set bits, in ascending bit order. SAE bits are sae_function_bit() positions named
// by sae::sae_function_name, IEC bits iec::DERControlName positions named by
// iec::der_control_name. Bits that name no enumerator are skipped.
std::vector<std::string> sae_der_function_names(std::uint32_t bitmap);
std::vector<std::string> iec_der_function_names(std::uint32_t bitmap);

// The DER annex a session's energy service negotiates; empty for a service without DER.
std::optional<types::iso15118::DerFlavor> der_flavor_for(iso15118::message_20::datatypes::ServiceCategory service);

types::iso15118::DerNegotiatedFunctions der_negotiated_functions(types::iso15118::DerFlavor flavor,
                                                                 std::uint32_t bitmap);

// An AC_DER_SAE block: its PermitService, the functions it enables and its validation problems.
types::iso15118::DerControlReceived sae_der_control_received(types::iso15118::DerControlSource source,
                                                             bool permit_service, std::uint32_t enabled,
                                                             const iso15118::ev::DerControlProblems& problems);

} // namespace module
