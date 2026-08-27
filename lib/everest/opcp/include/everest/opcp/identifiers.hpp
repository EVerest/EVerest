// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <string>

/// \file identifiers.hpp
/// Syntax checks for the identifiers used as SECC leaf common names (OPCP docs/05_handling-of-ids.md).
/// The ecosystem operator authorizes the EVSE Operator ID part of these names per account, so a name that
/// fails here would be refused by the PKI - but Hubject has been observed to accept a SECCID for an
/// ISO 15118-2 leaf, hence callers treat a mismatch as a warning only.
namespace opcp {

/// ISO 15118-2 Annex H EVSEID: `^[A-Za-z]{2}\*?\w{3}\*?E[\w\*]{1,30}$`, e.g. "DE*ICE*E45B*78C"
bool is_valid_evseid(const std::string& value);

/// ISO 15118-20 SECCID: `CC-OOO-S-<controller id>-<check digit>` with optional separators,
/// e.g. "DE-ICE-S-00003C4D557878675645330967543476-2"
bool is_valid_seccid(const std::string& value);

/// The 3 character EVSE Operator ID following the country code, or empty when not parseable
std::string operator_id_of(const std::string& value);

} // namespace opcp
