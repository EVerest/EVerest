// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_types.hpp>
#include <everest/slac/timer.hpp>

namespace everest::lib::slac::fsm::evse {

// ISO 15118-3 9.4 CM_VALIDATE BCB-toggle validation, owned by the Matching sub-machine.
//
// The EV proves it sits on the same Control-Pilot wire by switching its CP B->C->B (a "toggle") a
// number of times. The exchange has two steps, distinguished by the REQ's pilotTimer field:
//   step 1 (pilotTimer == 0): the EVSE answers CM_VALIDATE.CNF result=READY(0x01), toggle_num=0, and
//     arms the observation. If the EV sends no step 2, the EVSE autonomously repeats the step-1 CNF
//     every TT_match_sequence, limited to C_EV_match_retry repetitions, then FAILS silently.
//   step 2 (pilotTimer != 0): the EV sends the REQ carrying TP_EV_vald_toggle (pilotTimer, 100 ms
//     units) and then performs its BCB toggles during that window. Per ISO 15118-3 the EVSE waits out
//     the window and only THEN answers result=SUCCESS(0x02) with toggle_num = number of toggles seen.
// EvseManager counts one B->C edge per BCB toggle into ctx.bc_transition_count via the slac count_bc
// command, so the delta over the window is the toggle count. A step-2 REQ from the owner carrying a
// non-READY result is the EV's decision to terminate/skip -> no CNF. A step-1 REQ from a different EV
// while a validation is in progress is answered NOT_READY(0x00) (processing blocked, [V2G3-M09-13] /
// CmValidate_009).
//
// Driven by the Matching sub-machine: handle_req() for every CM_VALIDATE.REQ, and tick() on the
// update tick whenever needs_service() says the armed timer expired. reset() on entering Matching.
class ValidateHandler {
public:
    // Forget any in-progress validation.
    void reset();

    // Process one CM_VALIDATE.REQ received from \p src_mac (may be nullptr for a frame without a
    // usable source). Sends the step-1 CNF (READY / NOT_READY) itself; the step-2 SUCCESS CNF is sent
    // by tick() once the toggle-observation window has elapsed.
    void handle_req(messages::cm_validate_req const& req, std::uint8_t const* src_mac, Context& ctx);

    // Whether tick() has work to do: a validation is armed or a step-2 window is pending, and the
    // corresponding timer has expired.
    bool needs_service(timer::tp now) const;

    // Service the expired timer: either close the step-2 toggle window with a SUCCESS CNF, or
    // repeat/expire the step-1 CNF.
    void tick(Context& ctx);

    // Introspection (tests, diagnostics)
    bool armed() const {
        return armed_;
    }
    bool step2_pending() const {
        return step2_pending_;
    }
    MacAddress const& owner_mac() const {
        return owner_mac_;
    }

private:
    void send_cnf(Context& ctx, MacAddress const& mac, std::uint8_t result, std::uint8_t toggle_num) const;

    bool armed_{false};         // step-1 seen; awaiting step-2 or repeating the step-1 CNF
    bool step2_pending_{false}; // step-2 seen; waiting out the toggle-observation window
    int step1_retries_{0};      // autonomous step-1 CNF repetitions so far (<= C_EV_match_retry)
    int baseline_bc_{0};        // bc_transition_count at the start of the toggle window
    MacAddress owner_mac_{};    // EV that owns the in-progress validation
    timer timer_;               // step-1 repetition interval / step-2 toggle-observation window
};

} // namespace everest::lib::slac::fsm::evse
