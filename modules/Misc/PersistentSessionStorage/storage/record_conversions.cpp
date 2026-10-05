// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "record_conversions.hpp"

#include <array>

#include <everest/logging.hpp>
#include <everest/tls/openssl_util.hpp>

namespace module::storage {

namespace {

constexpr std::array<char, 16> HEX_DIGITS{'0', '1', '2', '3', '4', '5', '6', '7',
                                          '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

/// \brief Encodes \p digest as lower case hex string
std::string to_hex(const openssl::sha_256_digest_t& digest) {
    std::string result{};
    result.reserve(digest.size() * 2);
    for (const auto byte : digest) {
        result.push_back(HEX_DIGITS.at(byte >> 4U));
        result.push_back(HEX_DIGITS.at(byte & 0x0FU));
    }
    return result;
}

} // namespace

types::session_cost::SessionCost strip_id_tag(types::session_cost::SessionCost cost) {
    cost.id_tag = std::nullopt;
    return cost;
}

std::string generate_id_token_hash(const types::authorization::IdToken& id_token) {
    const auto input = types::authorization::id_token_type_to_string(id_token.type) + id_token.value;
    openssl::sha_256_digest_t digest{};
    if (not openssl::sha_256(input.data(), input.size(), digest)) {
        EVLOG_error << "Could not calculate the SHA256 digest of the id token";
        return {};
    }
    return to_hex(digest);
}

std::optional<std::string> compute_id_token_hash(const types::authorization::ProvidedIdToken& id_tag) {
    auto hash = generate_id_token_hash(id_tag.id_token);
    if (hash.empty()) {
        return std::nullopt;
    }
    return hash;
}

} // namespace module::storage
