// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <set>
#include <string>

#include <ocpp/v2/types.hpp>

namespace ocpp {
namespace v2 {

/// \brief Parses an OCPPCommCtrlr[<connection>].UpstreamMsgs or .DownstreamMsgs value.
///
/// The value is a comma separated list of OCPP message names without the Request or Response
/// suffix, which is the same spelling as the Action field of a CALL (OCA application note
/// "Secondary OCPP Connection on a Charging Station", section 3.5). For example:
/// "SetChargingProfile,ClearChargingProfile,GetCompositeSchedule".
///
/// Lenient by design: a name that is not a known OCPP message is logged and skipped rather than
/// making the whole list unusable. These lists are written by a CSMS or by a local configuration
/// tool, and a single typo must not silently grant or revoke everything else in the list.
///
/// \return The message types named; empty for an empty or absent value, which means the connection
///         is granted nothing.
std::set<MessageType> parse_message_permissions(const std::string& csv);

} // namespace v2
} // namespace ocpp
