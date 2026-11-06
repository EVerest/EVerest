// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/d2/pnc_material.hpp>

#include <iso15118/detail/d2/crypto.hpp>

namespace iso15118::ev::d2::pnc_material {

std::vector<std::vector<uint8_t>> pem_chain_to_der(const std::string& pem) {
    return iso15118::d2::crypto::pem_chain_to_der(pem);
}

std::string emaid_from_contract_der(const std::vector<uint8_t>& leaf_der) {
    return iso15118::d2::crypto::emaid_from_contract_der(leaf_der);
}

bool contract_key_matches(const std::vector<uint8_t>& leaf_der, const std::string& key_pem) {
    return iso15118::d2::crypto::contract_key_matches(leaf_der, {key_pem});
}

} // namespace iso15118::ev::d2::pnc_material
