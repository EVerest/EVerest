// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest
#include "validate_handler.hpp"

#include <algorithm>
#include <string>

#include <everest/slac/slac_defs.hpp>

namespace everest::lib::slac::fsm::evse {

void ValidateHandler::reset() {
    armed_ = false;
    step2_pending_ = false;
    step1_retries_ = 0;
    owner_mac_ = MacAddress{};
}

void ValidateHandler::handle_req(messages::cm_validate_req const& req, std::uint8_t const* src_mac, Context& ctx) {
    // Invalid/unsupported signalType: ignore it and leave any in-progress step-1 repetition
    // running (ISO 15118-5 CmValidate_004 sends signalType 0xFF between step-1 CNFs).
    if (req.signal_type != defs::CM_VALIDATE_REQ_SIGNAL_TYPE) {
        return;
    }

    if (req.timer == 0x00) {
        // Step 1. A different EV mid-validation is blocked (0x00).
        if (armed_ and src_mac != nullptr and not wire_pointer_equal(src_mac, owner_mac_)) {
            send_cnf(ctx, byte_array_from_wire<MacAddress>(src_mac), defs::CM_VALIDATE_REQ_RESULT_NOT_READY, 0);
            return;
        }
        // (Re-)arm: answer READY and (re)start the step-1 repetition interval. A repeated step-1
        // REQ resets the retry counter (CmValidate_002).
        if (src_mac != nullptr) {
            owner_mac_ = byte_array_from_wire<MacAddress>(src_mac);
        }
        armed_ = true;
        step2_pending_ = false;
        step1_retries_ = 0;
        // Snapshot the edge counter now, at step 1: no BCB toggle has happened yet (the EV
        // toggles only during the step-2 window), so any edges counted from here to the end of
        // that window are exactly the toggles. Snapshotting at step 2 instead would race the
        // CP path (MQTT) against the slower SLAC-frame path and miss the first toggle's edges.
        baseline_bc_ = ctx.bc_transition_count.load();
        timer_.setDurationMilliSeconds(defs::TT_MATCH_SEQUENCE_MS);
        timer_.reset();
        send_cnf(ctx, owner_mac_, defs::CM_VALIDATE_REQ_RESULT_READY, 0);
        return;
    }

    // Step 2 (pilotTimer != 0): only the owner of the armed validation may proceed.
    if (not armed_ or src_mac == nullptr or not wire_pointer_equal(src_mac, owner_mac_)) {
        return;
    }
    // A non-READY result is the EV's decision to terminate/skip: no CNF, end the validation
    // (CmValidate_005/006/007/008).
    if (req.result != defs::CM_VALIDATE_REQ_RESULT_READY) {
        armed_ = false;
        step2_pending_ = false;
        return;
    }
    // Start the toggle-observation window: wait out TP_EV_vald_toggle (pilotTimer in 100 ms
    // units) before counting. The baseline was snapshotted at step 1 (before any toggle); the
    // CNF is sent by tick() when the window elapses.
    step2_pending_ = true;
    timer_.setDurationMilliSeconds((static_cast<long long>(req.timer) + 1) * 100);
    timer_.reset();
}

bool ValidateHandler::needs_service() const {
    return (armed_ or step2_pending_) and timer_.timeout();
}

void ValidateHandler::tick(Context& ctx) {
    if (step2_pending_) {
        // EvseManager counts one B->C edge per BCB toggle, so the delta since the baseline IS
        // the number of toggles the EV performed during the observation window.
        const int toggles_seen = ctx.bc_transition_count.load() - baseline_bc_;
        const int toggles = std::max(0, std::min(toggles_seen, 255));
        send_cnf(ctx, owner_mac_, defs::CM_VALIDATE_REQ_RESULT_SUCCESS, static_cast<std::uint8_t>(toggles));
        armed_ = false;
        step2_pending_ = false;
        ctx.log_info("CM_VALIDATE: detected " + std::to_string(toggles) + " BCB toggle(s) during the toggle window");
        // Validation done: the CM_SLAC_MATCH.REQ must now arrive within TT_match_sequence
        // (ISO 15118-5 CmSlacMatch_003/004 cmValidate variant), not the full match session.
        ctx.validation_done = true;
        ctx.validation_match_window.setDurationMilliSeconds(defs::TT_MATCH_SEQUENCE_MS);
        ctx.validation_match_window.reset();
    } else if (armed_) {
        if (step1_retries_ < defs::C_EV_MATCH_RETRY) {
            step1_retries_++;
            send_cnf(ctx, owner_mac_, defs::CM_VALIDATE_REQ_RESULT_READY, 0);
            timer_.reset();
        } else {
            // Retry limit reached with no step 2: the validation (matching) has FAILED; stop
            // answering (CmValidate_003/004).
            armed_ = false;
        }
    }
}

void ValidateHandler::send_cnf(Context& ctx, MacAddress const& mac, std::uint8_t result,
                               std::uint8_t toggle_num) const {
    messages::cm_validate_cnf reply{};
    reply.signal_type = defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
    reply.toggle_num = toggle_num;
    reply.result = result;
    if (not ctx.send_slac_message(mac, reply)) {
        ctx.log_warn("Failed to send CM_VALIDATE.CNF");
    }
}

} // namespace everest::lib::slac::fsm::evse
