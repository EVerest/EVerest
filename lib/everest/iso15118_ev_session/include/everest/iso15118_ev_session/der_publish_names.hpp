// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <vector>

#include <generated/types/iso15118.hpp>

#include <iso15118/ev/d20/der/sae_validation.hpp>

namespace everest::lib::iso15118_ev {

// The functions of the set bits, in ascending bit order. SAE bits are sae_function_bit()
// positions, IEC bits iec::DERControlName positions. Bits that name no enumerator are skipped.
std::vector<types::iso15118::DerSaeFunction> sae_der_functions(std::uint32_t bitmap);
std::vector<types::iso15118::DerIecFunction> iec_der_functions(std::uint32_t bitmap);

types::iso15118::DerNegotiatedFunctions der_negotiated_functions(types::iso15118::DerFlavor flavor,
                                                                 std::uint32_t bitmap);

// An AC_DER_SAE block: its PermitService, the functions in `enabled` and its problems.
types::iso15118::DerControlReceived
sae_der_control_received(types::iso15118::DerControlSource source, bool permit_service, std::uint32_t enabled,
                         const iso15118::ev::d20::der::DerControlProblems& problems);

} // namespace everest::lib::iso15118_ev
