// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iso15118::ev::d2::pnc_material {

// Decode PEM certificates in order; empty when no certificate can be decoded.
std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem);

// Contract subject common name without dashes; empty for an invalid certificate.
std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der);

// True when the unencrypted PEM key is the private key of the contract leaf.
bool contract_key_matches(const std::vector<uint8_t>& leaf_der, const std::string& key_pem);

} // namespace iso15118::ev::d2::pnc_material
