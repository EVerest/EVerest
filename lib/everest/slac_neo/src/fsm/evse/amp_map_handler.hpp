// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/timer.hpp>

namespace everest::lib::slac::fsm::evse {

// ISO 15118-3 A.9.6 transmit-power limitation, SECC-initiated direction (PICS InitiateCmAmpMap),
// owned by the Matched sub-machine.
//
// Once the AVLN is up the SECC sends the operator-configured amplitude map to the peer with
// CM_AMP_MAP.REQ (CmAmpMap_002..004). It then retransmits the REQ every TT_match_response until a
// CM_AMP_MAP.CNF with result=0x00 arrives, limited to C_EV_match_retry retransmissions
// (CmAmpMap_003/004). A CNF with any other result is ignored ([V2G3-A09-114]; CmAmpMap_004 keeps
// retransmitting on an invalid CNF). Disabled by default; the map is provided via the amp_map_file
// config, and an empty map (amp_map_len == 0) disables the transmit direction even if the flag is
// set.
//
// The EV-initiated direction (answering a CM_AMP_MAP.REQ with a CNF) is stateless and stays a plain
// row action of the Matched sub-machine.
//
// Driven by the Matched sub-machine: start() on entering Matched, retransmit() on the update tick
// whenever retransmit_due(), and acknowledge_cnf() for a CM_AMP_MAP.CNF that is_awaited_cnf().
class AmpMapHandler {
public:
    // Forget any exchange in progress.
    void reset();

    // Kick off the exchange: send the first CM_AMP_MAP.REQ to ctx.status.ev_mac if the SECC is
    // configured to initiate it, and arm the retransmission interval. A no-op otherwise.
    void start(Context& ctx);

    // Whether the retransmission interval has elapsed while a CNF is still awaited.
    bool retransmit_due() const;

    // Service the elapsed interval: retransmit the REQ, or stop once the retry limit is reached.
    void retransmit(Context& ctx);

    // Whether \p msg is the CM_AMP_MAP.CNF(result=0x00) that confirms the exchange in progress.
    bool is_awaited_cnf(messages::HomeplugMessage const& msg) const;

    // The awaited CNF arrived: stop retransmitting.
    void acknowledge_cnf();

    // Introspection (tests, diagnostics)
    bool awaiting_cnf() const {
        return awaiting_cnf_;
    }
    int retries() const {
        return retries_;
    }

private:
    bool awaiting_cnf_{false}; // REQ sent, CNF(result=0x00) not yet received
    int retries_{0};           // retransmissions so far (<= C_EV_match_retry)
    timer timer_;              // TT_match_response retransmission interval
};

} // namespace everest::lib::slac::fsm::evse
