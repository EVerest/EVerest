// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include "amp_map_handler.hpp"

#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::fsm::evse {

void AmpMapHandler::reset() {
    awaiting_cnf_ = false;
    retries_ = 0;
}

void AmpMapHandler::start(Context& ctx) {
    reset();
    if (not ctx.slac_config.initiate_amp_map or ctx.slac_config.amp_map_len == 0) {
        return;
    }
    if (not ctx.send_amp_map_req(ctx.status.ev_mac, ctx.slac_config.amp_map_len, ctx.slac_config.amp_map_data)) {
        ctx.log_warn("Failed to send CM_AMP_MAP.REQ");
    }
    // Await the CM_AMP_MAP.CNF; retransmit every TT_match_response until it arrives, limited
    // to C_EV_match_retry retransmissions (serviced by retransmit() on the update tick).
    awaiting_cnf_ = true;
    timer_.setDurationMilliSeconds(defs::TT_MATCH_RESPONSE_MS);
    timer_.reset();
}

bool AmpMapHandler::retransmit_due() const {
    return awaiting_cnf_ and timer_.timeout();
}

void AmpMapHandler::retransmit(Context& ctx) {
    if (retries_ < defs::C_EV_MATCH_RETRY) {
        retries_++;
        ctx.send_amp_map_req(ctx.status.ev_mac, ctx.slac_config.amp_map_len, ctx.slac_config.amp_map_data);
        timer_.reset();
    } else {
        awaiting_cnf_ = false; // retry limit reached, stop
    }
}

bool AmpMapHandler::is_awaited_cnf(messages::HomeplugMessage const& msg) const {
    if (not awaiting_cnf_ or msg.get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_CNF)) {
        return false;
    }
    auto const cnf = msg.payload_as<messages::cm_amp_map_cnf>();
    return cnf.has_value() and cnf->result == defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
}

void AmpMapHandler::acknowledge_cnf() {
    awaiting_cnf_ = false;
}

} // namespace everest::lib::slac::fsm::evse
