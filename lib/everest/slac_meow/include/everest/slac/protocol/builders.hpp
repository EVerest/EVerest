// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/types.hpp>

/// Message builders that depend on nothing but their arguments.
namespace everest::slac::protocol {

/// The NID is derived from `nmk`: V2G3-A09-93.
messages::cm_set_key_req make_set_key_req(Nmk const& nmk);

/// Everything it needs is in the request; it reads nothing about the session.
///
/// Note (aw): this one doesn't return by value in order to optimize for fewer copies
void make_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf, messages::cm_slac_match_req const& match_req,
                         Nmk const& session_nmk);

} // namespace everest::slac::protocol
