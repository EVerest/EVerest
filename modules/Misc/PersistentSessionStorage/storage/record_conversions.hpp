// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

/// \file Conversion helpers for building stored session records

#pragma once

#include <optional>
#include <string>

#include <generated/types/authorization.hpp>
#include <generated/types/session_cost.hpp>

namespace module::storage {

/// \brief Clears the id_tag member of \p cost, because the raw id token is not persisted
/// \param[in] cost - the session cost as reported
/// \returns the session cost without the id_tag member
types::session_cost::SessionCost strip_id_tag(types::session_cost::SessionCost cost);

/// \brief Calculates the hash of \p id_token as used by the OCPP authorization cache:
///        the SHA256 digest of the id token type string concatenated with the id token value
/// \param[in] id_token - the id token to hash
/// \returns the hash as lower case hex encoded string, empty on failure
std::string generate_id_token_hash(const types::authorization::IdToken& id_token);

/// \brief Calculates the hash of the id token contained in \p id_tag
/// \param[in] id_tag - the provided id token of the transaction
/// \returns the hash, or nullopt when the digest could not be calculated
std::optional<std::string> compute_id_token_hash(const types::authorization::ProvidedIdToken& id_tag);

} // namespace module::storage
