// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef everest_helpers_HPP
#define everest_helpers_HPP

#include <algorithm>
#include <cctype>
#include <string>

namespace types::authorization {
struct ProvidedIdToken;
struct IdToken;
} // namespace types::authorization

namespace types::powermeter {
struct Powermeter;
} // namespace types::powermeter

namespace everest::helpers {

/// \brief Redacts a provided \p token by hashing it
/// \returns a hashed version of the provided token
std::string redact(const std::string& token);

types::authorization::ProvidedIdToken redact(const types::authorization::ProvidedIdToken& token);

/// \brief Escapes various HTML characters
/// \returns an escaped version of the provided html
std::string escape_html(const std::string& html);

/// \brief Compares two strings case-insensitively
/// \returns true if the strings are equal, false otherwise
bool is_equal_case_insensitive(const std::string& str1, const std::string& str2);

/// \brief Compares two IdTokens case-insensitively
/// \returns true if the IdTokens are equal, false otherwise
/// \note This function compares the value and type of the IdTokens
bool is_equal_case_insensitive(const types::authorization::IdToken& token1,
                               const types::authorization::IdToken& token2);

/// \brief Compares two ProvidedIdTokens case-insensitively
/// \returns true if the ProvidedIdTokens are equal, false otherwise
/// \note This function compares the id_token and type of the ProvidedIdTokens
bool is_equal_case_insensitive(const types::authorization::ProvidedIdToken& token1,
                               const types::authorization::ProvidedIdToken& token2);

/// \brief Provide a UUID
/// \returns a UUID string. This UUID is 36 characters long
std::string get_uuid();

/// \brief Provide a base64 encoded UUID
/// \returns a base64 encoded UUID string. This UUID is 22 characters long
std::string get_base64_uuid();

/// \brief Provide a base64 encoded ID
/// \returns a base64 encoded ID string. This ID is 16 characters long
std::string get_base64_id();

/// \brief Remaps the per-phase (L1/L2/L3) members of \p powermeter according to \p phase_rotation , to correct
/// for a physical wiring rotation between the meter and the grid.
/// Uses OCPP-style notation: "RST" is the identity, "TRS" means the reported L1/L2/L3 are grid L3/L1/L2 and
/// "STR" means they are grid L2/L3/L1. Unknown values are treated as "RST".
/// Members that are invariant under a rotation (total, DC, N, frequency) are left untouched.
/// \returns the rotated powermeter reading
types::powermeter::Powermeter apply_phase_rotation(types::powermeter::Powermeter powermeter,
                                                   const std::string& phase_rotation);

} // namespace everest::helpers

#endif
