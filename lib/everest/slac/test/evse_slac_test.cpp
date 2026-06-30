// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>

#include <everest/slac/fsm/evse/fsm.hpp>
#include <everest/slac/fsm/evse/states/others.hpp>

#include <fmt/format.h>

static auto create_cm_set_key_cnf(uint8_t result = slac::defs::CM_SET_KEY_CNF_RESULT_SUCCESS) {
    // FIXME (aw): needs to be fully implemented!
    slac::messages::cm_set_key_cnf set_key_cnf;
    set_key_cnf.result = result;
    slac::messages::HomeplugMessage hp_message;
    hp_message.setup_payload(&set_key_cnf, sizeof(set_key_cnf),
                             (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_CNF), slac::defs::MMV::AV_1_1);
    return hp_message;
}

// Exercises the CM_SET_KEY.REQ retry logic in ResetState: every CM_SET_KEY.CNF that reports a
// non-success result must trigger a resend until set_key_max_attempts is reached, after which the
// FSM gives up (callback returns no value).
static void test_set_key_retries() {
    printf("== test_set_key_retries ==\n");

    constexpr int max_attempts = 3;
    constexpr int timeout_ms = 20;
    // standard says 0x00 is success, but our implementation treats only 0x01 as success, so 0x00
    // is interpreted as a failure here
    constexpr uint8_t failure_result = 0x00;

    int set_key_req_count = 0;
    std::string last_state;

    slac::fsm::evse::ContextCallbacks callbacks;
    const auto slac_log = [](const std::string& text) { fmt::print("SLAC LOG: {}\n", text); };
    callbacks.log_debug = slac_log;
    callbacks.log_info = slac_log;
    callbacks.log_warn = slac_log;
    callbacks.log_error = slac_log;
    callbacks.signal_state = [&last_state](const std::string& state) { last_state = state; };
    callbacks.send_raw_slac = [&set_key_req_count](slac::messages::HomeplugMessage& hp_message) {
        if (hp_message.get_mmtype() == (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_REQ)) {
            set_key_req_count++;
        }
    };

    auto ctx = slac::fsm::evse::Context(callbacks);
    ctx.slac_config.set_key_timeout_ms = timeout_ms;
    ctx.slac_config.set_key_max_attempts = max_attempts;
    ctx.slac_config.chip_reset.enabled = false;

    auto machine = slac::fsm::evse::FSM();
    machine.reset<slac::fsm::evse::ResetState>(ctx);

    // the first feed should send the initial CM_SET_KEY.REQ and request a timeout
    auto fr = machine.feed();
    if (set_key_req_count != 1) {
        printf("Expected the first CM_SET_KEY.REQ to be sent immediately (got %d)\n", set_key_req_count);
        exit(EXIT_FAILURE);
    }

    // drive the retry loop: reject every CM_SET_KEY.CNF and let the timeout elapse so the FSM
    // keeps resending until it gives up. On give-up it transitions to IdleState, so feed() stops
    // returning a timeout value (has_value() == false).
    using namespace std::chrono;
    while (fr.has_value()) {
        const auto timeout = *fr;
        if (timeout > 0) {
            // sleep slightly longer than the timeout to make sure the retry interval has elapsed
            std::this_thread::sleep_for(milliseconds(timeout) + milliseconds(5));
        }

        // simulate the modem rejecting the key
        ctx.slac_message_payload = create_cm_set_key_cnf(failure_result);
        machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);

        fr = machine.feed();

        // the FSM must never send more than max_attempts requests
        if (set_key_req_count > max_attempts) {
            printf("CM_SET_KEY.REQ sent more than max_attempts times (%d > %d)\n", set_key_req_count, max_attempts);
            exit(EXIT_FAILURE);
        }
    }

    if (set_key_req_count != max_attempts) {
        printf("Expected exactly %d CM_SET_KEY.REQ attempts before giving up, got %d\n", max_attempts,
               set_key_req_count);
        exit(EXIT_FAILURE);
    }

    // after giving up, the FSM must not be stuck in ResetState but move on to Idle (UNMATCHED)
    if (last_state != "UNMATCHED") {
        printf("Expected FSM to transition to Idle (UNMATCHED) after giving up, last state was '%s'\n",
               last_state.c_str());
        exit(EXIT_FAILURE);
    }

    printf("test_set_key_retries passed: gave up after %d attempts and moved to Idle\n", set_key_req_count);
}

// Verifies that the NMK advertised to the EV (session_nmk, sent in CM_SLAC_MATCH_CNF) is only
// updated once the modem confirms the key with a successful CM_SET_KEY.CNF. A failed key set must
// leave the previously confirmed key in place, otherwise we would hand the EV a key the modem is
// not actually running -> "Link could not be established".
static void test_nmk_only_committed_on_confirmed_key() {
    printf("== test_nmk_only_committed_on_confirmed_key ==\n");

    constexpr int max_attempts = 3;
    constexpr int timeout_ms = 20;
    // our implementation treats only 0x01 as success, so 0x00 is interpreted as a failure
    constexpr uint8_t failure_result = 0x00;

    std::array<uint8_t, slac::defs::NMK_LEN> last_req_nmk{};
    bool got_set_key_req = false;

    slac::fsm::evse::ContextCallbacks callbacks;
    const auto slac_log = [](const std::string& text) { fmt::print("SLAC LOG: {}\n", text); };
    callbacks.log_debug = slac_log;
    callbacks.log_info = slac_log;
    callbacks.log_warn = slac_log;
    callbacks.log_error = slac_log;
    callbacks.send_raw_slac = [&](slac::messages::HomeplugMessage& hp_message) {
        if (hp_message.get_mmtype() == (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_REQ)) {
            const auto req = hp_message.get_payload<slac::messages::cm_set_key_req>();
            std::copy(std::begin(req.new_key), std::end(req.new_key), last_req_nmk.begin());
            got_set_key_req = true;
        }
    };

    auto ctx = slac::fsm::evse::Context(callbacks);
    ctx.slac_config.set_key_timeout_ms = timeout_ms;
    ctx.slac_config.set_key_max_attempts = max_attempts;
    ctx.slac_config.chip_reset.enabled = false;

    auto machine = slac::fsm::evse::FSM();

    // --- cycle 1: a successful key set commits the new key as the active session key ---
    machine.reset<slac::fsm::evse::ResetState>(ctx);
    machine.feed();
    if (!got_set_key_req) {
        printf("Expected a CM_SET_KEY.REQ to be sent on reset\n");
        exit(EXIT_FAILURE);
    }
    const auto confirmed_nmk = last_req_nmk;

    ctx.slac_message_payload = create_cm_set_key_cnf();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    machine.feed();

    // after a successful CM_SET_KEY.CNF the advertised session key must equal the key we
    // programmed into the modem
    if (!std::equal(confirmed_nmk.begin(), confirmed_nmk.end(), ctx.slac_config.session_nmk)) {
        printf("session_nmk was not updated to the confirmed key after CM_SET_KEY.CNF success\n");
        exit(EXIT_FAILURE);
    }

    // --- cycle 2: a failing key set must NOT change the advertised key ---
    got_set_key_req = false;
    machine.handle_event(slac::fsm::evse::Event::RESET);
    auto fr = machine.feed();
    if (!got_set_key_req) {
        printf("Expected a CM_SET_KEY.REQ to be sent on the second reset\n");
        exit(EXIT_FAILURE);
    }

    // a fresh candidate key should have been generated, distinct from the confirmed one
    const auto candidate_nmk = last_req_nmk;
    if (std::equal(candidate_nmk.begin(), candidate_nmk.end(), confirmed_nmk.begin())) {
        printf("Expected a freshly generated candidate NMK on reset\n");
        exit(EXIT_FAILURE);
    }

    // reject every CM_SET_KEY.CNF until the FSM gives up
    using namespace std::chrono;
    while (fr.has_value()) {
        const auto timeout = *fr;
        if (timeout > 0) {
            std::this_thread::sleep_for(milliseconds(timeout) + milliseconds(5));
        }
        ctx.slac_message_payload = create_cm_set_key_cnf(failure_result);
        machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
        fr = machine.feed();
    }

    // the key set failed, so the advertised key must still be the previously confirmed one and
    // must NOT have changed to the unconfirmed candidate
    if (!std::equal(confirmed_nmk.begin(), confirmed_nmk.end(), ctx.slac_config.session_nmk)) {
        printf("session_nmk changed after a FAILED CM_SET_KEY - regression!\n");
        exit(EXIT_FAILURE);
    }
    if (std::equal(candidate_nmk.begin(), candidate_nmk.end(), ctx.slac_config.session_nmk)) {
        printf("session_nmk was set to the unconfirmed candidate key after a FAILED CM_SET_KEY - regression!\n");
        exit(EXIT_FAILURE);
    }

    printf("test_nmk_only_committed_on_confirmed_key passed: NMK only updated on confirmed key set\n");
}

static auto create_cm_validate_req() {
    slac::messages::cm_validate_req validate_req;
    validate_req.signal_type = slac::defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
    validate_req.timer = 0;
    validate_req.result = slac::defs::CM_VALIDATE_REQ_RESULT_READY;

    slac::messages::HomeplugMessage hp_message;
    hp_message.setup_payload(&validate_req, sizeof(validate_req),
                             (slac::defs::MMTYPE_CM_VALIDATE | slac::defs::MMTYPE_MODE_REQ), slac::defs::MMV::AV_1_1);

    return hp_message;
}

struct EVSession {
    EVSession(const std::array<uint8_t, 8>& run_id_, const std::array<uint8_t, 6>& mac_) : run_id(run_id_), mac(mac_){};

    // FIXME (aw): all these create_cm_* need to be fully implemented!
    auto create_cm_slac_parm_req() {
        slac::messages::cm_slac_parm_req parm_req{};
        std::copy(run_id.begin(), run_id.end(), parm_req.run_id);

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&parm_req, sizeof(parm_req),
                                 (slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

    auto create_cm_start_atten_char_ind() {
        slac::messages::cm_start_atten_char_ind atten_char_ind{};
        atten_char_ind.num_sounds = slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
        atten_char_ind.timeout = slac::defs::CM_SLAC_PARM_CNF_TIMEOUT;
        atten_char_ind.resp_type = slac::defs::CM_SLAC_PARM_CNF_RESP_TYPE;
        std::copy(run_id.begin(), run_id.end(), atten_char_ind.run_id);

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&atten_char_ind, sizeof(atten_char_ind),
                                 (slac::defs::MMTYPE_CM_START_ATTEN_CHAR | slac::defs::MMTYPE_MODE_IND),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

    auto create_cm_mnbc_sound_ind() {
        slac::messages::cm_mnbc_sound_ind sound_ind{};
        std::copy(run_id.begin(), run_id.end(), sound_ind.run_id);

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&sound_ind, sizeof(sound_ind),
                                 (slac::defs::MMTYPE_CM_MNBC_SOUND | slac::defs::MMTYPE_MODE_IND),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

    auto create_cm_atten_profile_ind() {
        slac::messages::cm_atten_profile_ind profile_ind;
        std::copy(mac.begin(), mac.end(), profile_ind.pev_mac);
        profile_ind.num_groups = slac::defs::AAG_LIST_LEN;

        for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
            profile_ind.aag[i] = i;
        }

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&profile_ind, sizeof(profile_ind),
                                 (slac::defs::MMTYPE_CM_ATTEN_PROFILE | slac::defs::MMTYPE_MODE_IND),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

    auto create_cm_atten_char_rsp() {
        slac::messages::cm_atten_char_rsp atten_char{};
        std::copy(mac.begin(), mac.end(), atten_char.source_address);
        std::copy(run_id.begin(), run_id.end(), atten_char.run_id);

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&atten_char, sizeof(atten_char),
                                 (slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

    auto create_cm_slac_match_req() {
        slac::messages::cm_slac_match_req match_req{};
        match_req.mvf_length = slac::defs::CM_SLAC_MATCH_REQ_MVF_LENGTH;
        std::copy(mac.begin(), mac.end(), match_req.pev_mac);
        std::copy(run_id.begin(), run_id.end(), match_req.run_id);

        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(mac.data(), mac.data());
        hp_message.setup_payload(&match_req, sizeof(match_req),
                                 (slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ),
                                 slac::defs::MMV::AV_1_1);

        return hp_message;
    }

private:
    std::array<uint8_t, 8> run_id;
    const std::array<uint8_t, ETH_ALEN>& mac;
};

void feed_machine_for(slac::fsm::evse::FSM& machine, int period_ms,
                      fsm::FeedResult<slac::fsm::evse::FSMReturnType> feed_result) {
    using namespace std::chrono;

    auto end_tp = steady_clock::now() + milliseconds(period_ms);

    while (true) {
        if (feed_result.transition()) {
            // need to call feed
        } else if (feed_result.unhandled_event()) {
            printf("DEBUG: got an unhandled event\n");
            break;
        } else if (feed_result.internal_error()) {
            printf("ERROR: internal FSM error\n");
            exit(EXIT_FAILURE);
        } else if (feed_result.has_value() == false) {
            // nothing to do
            break;
        } else if (feed_result.has_value() == true) {
            const auto timeout = *feed_result;
            if (timeout == 0) {
                // need to call feed directly
            } else {
                auto next_tp = steady_clock::now() + milliseconds(timeout);
                if (next_tp > end_tp) {
                    break;
                }

                std::this_thread::sleep_until(next_tp);
            }
        } else {
            printf("ERROR: unknown feed result case\n");
            exit(EXIT_FAILURE);
        }

        feed_result = machine.feed();
    }

    std::this_thread::sleep_until(end_tp);
}

int main(int argc, char* argv[]) {
    const auto ATTENUATION_ADJUSTMENT = 10;
    printf("Hi from SLAC!\n");

    test_set_key_retries();
    test_nmk_only_committed_on_confirmed_key();

    std::optional<slac::messages::HomeplugMessage> msg_in;

    slac::fsm::evse::ContextCallbacks callbacks;
    const auto slac_log = [](const std::string& text) { fmt::print("SLAC LOG: {}\n", text); };
    callbacks.log_debug = slac_log;
    callbacks.log_info = slac_log;
    callbacks.log_warn = slac_log;
    callbacks.log_error = slac_log;

    callbacks.send_raw_slac = [&msg_in](slac::messages::HomeplugMessage& hp_message) { msg_in = hp_message; };

    auto ctx = slac::fsm::evse::Context(callbacks);
    ctx.slac_config.sounding_atten_adjustment = ATTENUATION_ADJUSTMENT;
    ctx.slac_config.chip_reset.enabled = false;
    memset(ctx.evse_mac, 0, sizeof(ctx.evse_mac));

    auto machine = slac::fsm::evse::FSM();

    //
    // reset machine
    //
    machine.reset<slac::fsm::evse::ResetState>(ctx);
    auto fr = machine.feed();

    // assert that CM_SET_KEY_REQ gets set!
    if (!msg_in.has_value() || msg_in->get_mmtype() != (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_REQ)) {
        printf("Expected CM_SET_KEY_REQ!\n");
        exit(EXIT_FAILURE);
    } else {
        msg_in.reset();
    }

    feed_machine_for(machine, 230, fr);

    // feed in CM_SET_KEY_CNF
    ctx.slac_message_payload = create_cm_set_key_cnf();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    // should be idle state now, send ENTER_BCD, to enter MATCHING
    machine.handle_event(slac::fsm::evse::Event::ENTER_BCD);
    fr = machine.feed();

    feed_machine_for(machine, 300, fr);

    // create session 1 and inject CM_SLAC_PARM_REQ
    auto session_1 = EVSession({0, 1, 2, 3, 4, 5, 6, 7}, {0xca, 0xfe, 0xca, 0xfe, 0xca, 0xfe});
    ctx.slac_message_payload = session_1.create_cm_slac_parm_req();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    // assert that CM_SLAC_PARM_CNF gets set!
    if (!msg_in.has_value() ||
        msg_in->get_mmtype() != (slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_CNF)) {
        printf("Expected CM_SLAC_PARM_CNF!\n");
        exit(EXIT_FAILURE);
    } else {
        msg_in.reset();
    }

    feed_machine_for(machine, 233, fr);

    // inject CM_START_ATTEN_CHAR_IND
    ctx.slac_message_payload = session_1.create_cm_start_atten_char_ind();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    // inject all the soundings ...
    for (int i = 0; i < slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS - 1; i++) {
        ctx.slac_message_payload = session_1.create_cm_mnbc_sound_ind();
        machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
        fr = machine.feed();

        ctx.slac_message_payload = session_1.create_cm_atten_profile_ind();
        machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
        fr = machine.feed();
    }

    feed_machine_for(machine, 700 + 1 * 45, fr);

    // assert that CM_ATTEN_CHAR_IND gets set!
    if (!msg_in.has_value() ||
        msg_in->get_mmtype() != (slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_IND)) {
        printf("Expected CM_ATTEN_CHAR_IND!\n");
        exit(EXIT_FAILURE);
    } else {
        auto atten_char_ind = msg_in->get_payload<slac::messages::cm_atten_char_ind>();
        for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
            if (atten_char_ind.attenuation_profile.aag[i] != i + ATTENUATION_ADJUSTMENT) {
                printf("Averaging not correct in ATTEN_CHAR_IND\n");
                exit(EXIT_FAILURE);
            }
        }
        msg_in.reset();
    }

    // "async" insert an CM_VALIDATE.REQ
    ctx.slac_message_payload = create_cm_validate_req();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    // assert that CM_VALIDATE.CNF gets set!
    if (!msg_in.has_value() || msg_in->get_mmtype() != (slac::defs::MMTYPE_CM_VALIDATE | slac::defs::MMTYPE_MODE_CNF)) {
        printf("Expected CM_VALIDATE.CNF!\n");
        exit(EXIT_FAILURE);
    } else {
        // check for correct "failure" result
        auto validate_cnf = msg_in->get_payload<slac::messages::cm_validate_cnf>();
        if (validate_cnf.result != slac::defs::CM_VALIDATE_REQ_RESULT_FAILURE) {
            printf("Expected result field of CM_VALIDATE.CNF to be set to failure\n");
            exit(EXIT_FAILURE);
        }
    }

    // inject CM_ATTEN_CHAR_RSP
    ctx.slac_message_payload = session_1.create_cm_atten_char_rsp();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    feed_machine_for(machine, 1000, fr);

    // inject messages from a second session
    auto session_2 = EVSession({9, 1, 2, 3, 4, 5, 6, 7}, {0xbe, 0xaf, 0xbe, 0xaf, 0xbe, 0xaf});
    ctx.slac_message_payload = session_2.create_cm_slac_parm_req();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    feed_machine_for(machine, 1000, fr);

    // inject CM_SLAC_MATCH_REQ
    ctx.slac_message_payload = session_1.create_cm_slac_match_req();
    machine.handle_event(slac::fsm::evse::Event::SLAC_MESSAGE);
    fr = machine.feed();

    // assert that CM_SLAC_MATCH_CNF gets set!
    if (!msg_in.has_value() ||
        msg_in->get_mmtype() != (slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_CNF)) {
        printf("Expected CM_ATTEN_CHAR_IND!\n");
        exit(EXIT_FAILURE);
    } else {
        msg_in.reset();
    }

    return 0;
}
