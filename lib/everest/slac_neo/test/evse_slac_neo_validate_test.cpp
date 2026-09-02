// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// Unit tests for the CM_VALIDATE BCB-toggle validation handler (src/fsm/evse/validate_handler.hpp),
// driven directly instead of through the whole Matching sub-machine. The end-to-end path is covered
// by evse_slac_neo_matching_test.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

#include "../src/fsm/evse/validate_handler.hpp"

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;

namespace {

bool assert_true(bool cond, char const* test_name, char const* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

struct SentCnf {
    MacAddress destination;
    messages::cm_validate_cnf cnf;
};

// A Context whose only callback records the CM_VALIDATE.CNF frames the handler sends.
struct Harness {
    ContextCallbacks callbacks{};
    std::vector<SentCnf> sent;
    Context ctx{callbacks};

    Harness() {
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& hp_message) {
            if (hp_message.get_mmtype() != (defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_CNF)) {
                return true;
            }
            auto const* raw = hp_message.get_raw_message_ptr();
            SentCnf entry{};
            std::copy(std::begin(raw->ethernet_header.ether_dhost), std::end(raw->ethernet_header.ether_dhost),
                      entry.destination.begin());
            entry.cnf = hp_message.get_payload<messages::cm_validate_cnf>();
            sent.push_back(entry);
            return true;
        };
    }
};

messages::cm_validate_req make_req(std::uint8_t pilot_timer, std::uint8_t result = defs::CM_VALIDATE_REQ_RESULT_READY,
                                   std::uint8_t signal_type = defs::CM_VALIDATE_REQ_SIGNAL_TYPE) {
    messages::cm_validate_req req{};
    req.signal_type = signal_type;
    req.timer = pilot_timer;
    req.result = result;
    return req;
}

const MacAddress ev_a{{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}};
const MacAddress ev_b{{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02}};

void sleep_ms(long long ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

bool test_step1_answers_ready_and_arms() {
    const char* test_name = "test_step1_answers_ready_and_arms";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);

    return assert_true(h.sent.size() == 1, test_name, "step 1 must produce exactly one CNF") and
           assert_true(h.sent[0].destination == ev_a, test_name, "step-1 CNF must go to the requesting EV") and
           assert_true(h.sent[0].cnf.result == defs::CM_VALIDATE_REQ_RESULT_READY, test_name,
                       "step-1 CNF result must be READY") and
           assert_true(h.sent[0].cnf.toggle_num == 0, test_name, "step-1 CNF toggle_num must be 0") and
           assert_true(h.sent[0].cnf.signal_type == defs::CM_VALIDATE_REQ_SIGNAL_TYPE, test_name,
                       "step-1 CNF signal_type must be 0x00") and
           assert_true(handler.armed() and not handler.step2_pending(), test_name, "handler must be armed") and
           assert_true(not handler.needs_service(), test_name, "no service needed right after arming");
}

bool test_step2_waits_out_window_then_reports_toggle_count() {
    const char* test_name = "test_step2_waits_out_window_then_reports_toggle_count";
    Harness h;
    ValidateHandler handler;

    // Edges counted before validation started must not be reported.
    h.ctx.bc_transition_count.store(7);
    handler.handle_req(make_req(0), ev_a.data(), h.ctx);

    // Three BCB toggles during the window = three B->C edges on top of the baseline.
    h.ctx.bc_transition_count.store(7 + 3);

    // pilotTimer 1 = 200 ms observation window; no CNF until it elapsed.
    handler.handle_req(make_req(1), ev_a.data(), h.ctx);
    if (not assert_true(h.sent.size() == 1, test_name, "step 2 must not be answered immediately") or
        not assert_true(handler.step2_pending(), test_name, "step 2 must open the toggle window") or
        not assert_true(not handler.needs_service(), test_name, "window must not have elapsed yet")) {
        return false;
    }

    sleep_ms(250);
    if (not assert_true(handler.needs_service(), test_name, "window must have elapsed after 250 ms")) {
        return false;
    }
    handler.tick(h.ctx);

    return assert_true(h.sent.size() == 2, test_name, "closing the window must send exactly one CNF") and
           assert_true(h.sent[1].destination == ev_a, test_name, "SUCCESS CNF must go to the owner") and
           assert_true(h.sent[1].cnf.result == defs::CM_VALIDATE_REQ_RESULT_SUCCESS, test_name,
                       "CNF result must be SUCCESS") and
           assert_true(h.sent[1].cnf.toggle_num == 3, test_name, "toggle_num must be the edges since step 1") and
           assert_true(not handler.armed() and not handler.step2_pending(), test_name,
                       "validation must be finished") and
           assert_true(h.ctx.validation_done, test_name, "validation_done must be set for the match window") and
           assert_true(not h.ctx.validation_match_window.timeout(), test_name,
                       "the post-validation match window must have been (re)started");
}

bool test_foreign_ev_gets_not_ready_while_validation_in_progress() {
    const char* test_name = "test_foreign_ev_gets_not_ready_while_validation_in_progress";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);
    handler.handle_req(make_req(0), ev_b.data(), h.ctx);

    if (not assert_true(h.sent.size() == 2, test_name, "the foreign step-1 REQ must be answered") or
        not assert_true(h.sent[1].destination == ev_b, test_name, "NOT_READY must go to the foreign EV") or
        not assert_true(h.sent[1].cnf.result == defs::CM_VALIDATE_REQ_RESULT_NOT_READY, test_name,
                        "foreign EV must get NOT_READY") or
        not assert_true(handler.owner_mac() == ev_a, test_name, "the owner must not change")) {
        return false;
    }

    // The owner's step 2 is still accepted; the foreign EV's step 2 is ignored.
    handler.handle_req(make_req(1), ev_b.data(), h.ctx);
    if (not assert_true(not handler.step2_pending(), test_name, "foreign step 2 must be ignored")) {
        return false;
    }
    handler.handle_req(make_req(1), ev_a.data(), h.ctx);
    return assert_true(handler.step2_pending(), test_name, "owner step 2 must open the window") and
           assert_true(h.sent.size() == 2, test_name, "step 2 must not produce an immediate CNF");
}

bool test_step1_is_repeated_up_to_retry_limit_then_fails_silently() {
    const char* test_name = "test_step1_is_repeated_up_to_retry_limit_then_fails_silently";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);

    // Each TT_match_sequence without a step 2 repeats the READY CNF, C_EV_match_retry times.
    for (int repetition = 1; repetition <= defs::C_EV_MATCH_RETRY; ++repetition) {
        sleep_ms(defs::TT_MATCH_SEQUENCE_MS + 50);
        if (not assert_true(handler.needs_service(), test_name, "repetition interval must have elapsed")) {
            return false;
        }
        handler.tick(h.ctx);
        if (not assert_true(static_cast<int>(h.sent.size()) == 1 + repetition, test_name,
                            "each tick must repeat the step-1 CNF once") or
            not assert_true(h.sent.back().cnf.result == defs::CM_VALIDATE_REQ_RESULT_READY, test_name,
                            "repeated CNF must be READY") or
            not assert_true(handler.armed(), test_name, "handler must stay armed while retrying")) {
            return false;
        }
    }

    // One more interval: the limit is reached, the validation fails silently.
    sleep_ms(defs::TT_MATCH_SEQUENCE_MS + 50);
    if (not assert_true(handler.needs_service(), test_name, "final interval must have elapsed")) {
        return false;
    }
    handler.tick(h.ctx);
    return assert_true(static_cast<int>(h.sent.size()) == 1 + defs::C_EV_MATCH_RETRY, test_name,
                       "no CNF may be sent once the retry limit is reached") and
           assert_true(not handler.armed(), test_name, "handler must disarm after the retry limit") and
           assert_true(not handler.needs_service(), test_name, "nothing left to service");
}

bool test_non_ready_step2_ends_validation_without_cnf() {
    const char* test_name = "test_non_ready_step2_ends_validation_without_cnf";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);
    handler.handle_req(make_req(1, defs::CM_VALIDATE_REQ_RESULT_FAILURE), ev_a.data(), h.ctx);

    return assert_true(h.sent.size() == 1, test_name, "a non-READY step 2 must not be answered") and
           assert_true(not handler.armed() and not handler.step2_pending(), test_name,
                       "a non-READY step 2 must end the validation");
}

bool test_invalid_signal_type_is_ignored() {
    const char* test_name = "test_invalid_signal_type_is_ignored";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);
    handler.handle_req(make_req(0, defs::CM_VALIDATE_REQ_RESULT_READY, 0xFF), ev_a.data(), h.ctx);

    return assert_true(h.sent.size() == 1, test_name, "an unsupported signalType must not be answered") and
           assert_true(handler.armed(), test_name, "an unsupported signalType must not disturb the validation");
}

bool test_reset_forgets_in_progress_validation() {
    const char* test_name = "test_reset_forgets_in_progress_validation";
    Harness h;
    ValidateHandler handler;

    handler.handle_req(make_req(0), ev_a.data(), h.ctx);
    handler.reset();

    // After reset a step 1 from another EV is a fresh validation, not a foreign one.
    handler.handle_req(make_req(0), ev_b.data(), h.ctx);
    return assert_true(h.sent.size() == 2, test_name, "step 1 after reset must be answered") and
           assert_true(h.sent[1].cnf.result == defs::CM_VALIDATE_REQ_RESULT_READY, test_name,
                       "step 1 after reset must get READY") and
           assert_true(handler.owner_mac() == ev_b, test_name, "reset must clear the previous owner");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 7>{
        std::make_pair("test_step1_answers_ready_and_arms", test_step1_answers_ready_and_arms),
        std::make_pair("test_step2_waits_out_window_then_reports_toggle_count",
                       test_step2_waits_out_window_then_reports_toggle_count),
        std::make_pair("test_foreign_ev_gets_not_ready_while_validation_in_progress",
                       test_foreign_ev_gets_not_ready_while_validation_in_progress),
        std::make_pair("test_step1_is_repeated_up_to_retry_limit_then_fails_silently",
                       test_step1_is_repeated_up_to_retry_limit_then_fails_silently),
        std::make_pair("test_non_ready_step2_ends_validation_without_cnf",
                       test_non_ready_step2_ends_validation_without_cnf),
        std::make_pair("test_invalid_signal_type_is_ignored", test_invalid_signal_type_is_ignored),
        std::make_pair("test_reset_forgets_in_progress_validation", test_reset_forgets_in_progress_validation),
    };

    int failed_count = 0;
    for (auto const& test : tests) {
        if (not test.second()) {
            std::printf("[FAIL] %s\n", test.first);
            ++failed_count;
        } else {
            std::printf("[PASS] %s\n", test.first);
        }
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return 1;
    }
    std::printf("ALL PASSED\n");
    return 0;
}
