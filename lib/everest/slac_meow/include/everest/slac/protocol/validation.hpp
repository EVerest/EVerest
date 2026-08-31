// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/protocol/messages.hpp>

/// Message checks that depend on nothing but the message itself. A check that has to know which
/// matching session a message belongs to lives with that session instead.
namespace everest::slac::protocol {

/// The application and security type ISO 15118-3 requires of a CM_SLAC_PARM.REQ.
bool validate_message(messages::cm_slac_parm_req const& msg);

} // namespace everest::slac::protocol
