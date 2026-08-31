// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/protocol/homeplug_message.hpp>
#include <everest/slac/protocol/types.hpp>

// Frame predicates, kept free so they can be tested without constructing a state machine.
namespace everest::slac::ev {

/// CM_SLAC_PARM.CNF carrying our run id.
bool is_slac_parm_cnf(messages::HomeplugMessage const& frame, RunId const& run_id);

/// Any well formed CM_ATTEN_CHAR.IND.
bool is_atten_char_ind(messages::HomeplugMessage const& frame);

/// CM_ATTEN_CHAR.IND carrying our run id.
bool is_atten_char_ind_for_run(messages::HomeplugMessage const& frame, RunId const& run_id);

/// CM_SLAC_MATCH.CNF carrying our run id, from the EVSE we are talking to. A frame without a
/// source MAC is accepted.
bool is_slac_match_cnf(messages::HomeplugMessage const& frame, RunId const& run_id, MacAddress const& evse_mac);

/// The result byte is deliberately not inspected.
bool is_set_key_cnf(messages::HomeplugMessage const& frame);

} // namespace everest::slac::ev
