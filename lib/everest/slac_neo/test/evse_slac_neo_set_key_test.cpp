// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <net/ethernet.h>
#include <thread>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/context.hpp>
#include <everest/slac/slac_fsm.hpp>

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;

namespace {

struct SetKeySentMessage {
    std::size_t sequence;
    messages::HomeplugMessage hp_message;
};

constexpr messages::HomeplugMessage::MacAddress default_peer_mac{{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01}};

struct SetKeyCnfParams {
    messages::HomeplugMessage::MacAddress source = default_peer_mac;
    std::uint8_t pid = defs::CM_SET_KEY_REQ_PID_HLE;
    std::uint16_t prn = defs::CM_SET_KEY_REQ_PRN_UNUSED;
    std::uint8_t pmn = defs::CM_SET_KEY_REQ_PMN_UNUSED;
    std::uint32_t my_nonce = 0;
    std::uint32_t your_nonce = 0;
};

messages::HomeplugMessage create_cm_set_key_cnf(std::uint8_t result, SetKeyCnfParams const& params = {}) {
    messages::cm_set_key_cnf cnf{};
    cnf.result = result;
    cnf.my_nonce = params.my_nonce;
    cnf.your_nonce = params.your_nonce;
    cnf.pid = params.pid;
    cnf.prn = params.prn;
    cnf.pmn = params.pmn;

    messages::HomeplugMessage message;
    message.set_source(params.source);
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_set_key_cnf_reserved() {
    return create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS + 0x80);
}

messages::HomeplugMessage create_short_cm_set_key_cnf() {
    auto message = create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_SUCCESS);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::homeplug_fragmentation_part) +
                                 sizeof(messages::cm_set_key_cnf));
    return message;
}

bool is_set_key_request(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ);
}

std::vector<SetKeySentMessage> sort_set_key_messages(std::vector<SetKeySentMessage> const& messages) {
    auto set_key_messages = std::vector<SetKeySentMessage>{};
    for (auto const& msg : messages) {
        if (is_set_key_request(msg.hp_message)) {
            set_key_messages.push_back(msg);
        }
    }

    std::stable_sort(set_key_messages.begin(), set_key_messages.end(),
                     [](auto const& lhs, auto const& rhs) { return lhs.sequence < rhs.sequence; });
    return set_key_messages;
}

using Nmk = std::array<uint8_t, defs::NMK_LEN>;

Nmk extract_nmk(messages::HomeplugMessage const& msg) {
    auto const& req = msg.get_payload<messages::cm_set_key_req>();
    Nmk nmk{};
    std::copy(std::begin(req.new_key), std::end(req.new_key), nmk.begin());
    return nmk;
}

size_t count_set_key_messages(std::vector<SetKeySentMessage> const& messages) {
    return sort_set_key_messages(messages).size();
}

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

template <typename Predicate>
bool wait_for(std::chrono::milliseconds timeout, slac_fsm& machine, Predicate&& predicate) {
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < timeout) {
        if (predicate()) {
            return true;
        }
        machine.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

bool wait_for_match_state(Context const& ctx, SlacState expected, slac_fsm& machine, int timeout_ms) {
    const auto state_match = [&ctx, expected]() { return ctx.status.match_state == expected; };
    return wait_for(std::chrono::milliseconds(timeout_ms), machine, state_match);
}

bool wait_for_set_key_count(std::vector<SetKeySentMessage> const& sent_messages, size_t expected, slac_fsm& machine,
                            int timeout_ms) {
    const auto check_count = [&sent_messages, expected]() { return count_set_key_messages(sent_messages) >= expected; };
    return wait_for(std::chrono::milliseconds(timeout_ms), machine, check_count);
}

bool assert_stays_at_count(std::vector<SetKeySentMessage> const& sent_messages, size_t expected_count,
                           slac_fsm& machine, int timeout_ms) {
    size_t start_count = count_set_key_messages(sent_messages);
    if (start_count != expected_count) {
        return false;
    }
    auto stable = wait_for(std::chrono::milliseconds(timeout_ms), machine, [&sent_messages, expected_count]() {
        return count_set_key_messages(sent_messages) > expected_count;
    });
    return not stable;
}

bool expect_nmk_equal(uint8_t const* lhs, Nmk const& rhs) {
    return std::equal(lhs, lhs + defs::NMK_LEN, rhs.begin());
}

bool expect_nmk_equal(Nmk const& lhs, Nmk const& rhs) {
    return lhs == rhs;
}

void configure_common(Context& ctx) {
    ctx.slac_config.request_info_delay_ms = 1;
    ctx.slac_config.set_key_timeout_ms = 5;
    ctx.slac_config.set_key_max_attempts = 3;
    ctx.slac_config.slac_init_timeout_ms = 5;
    ctx.slac_config.chip_reset.enabled = false;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.regenerate_key_on_reset = true;
    ctx.slac_config.ac_mode_five_percent = false;
}

void fill_session_nmk(Context& ctx, uint8_t base) {
    for (std::size_t i = 0; i < defs::NMK_LEN; ++i) {
        ctx.slac_config.session_nmk[i] = static_cast<uint8_t>(base + static_cast<uint8_t>(i));
    }
}

Nmk current_session_nmk(Context const& ctx) {
    Nmk out{};
    std::copy(std::begin(ctx.slac_config.session_nmk), std::end(ctx.slac_config.session_nmk), out.begin());
    return out;
}

bool test_legacy_single_attempt_accepts_valid_success_result() {
    const char* test_name = "legacy_single_attempt_accepts_valid_success_result";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    ctx.slac_config.set_key_timeout_ms = 50;
    fill_session_nmk(ctx, 0x11);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }
    if (!assert_true(count_set_key_messages(sent_messages) == 1, test_name,
                     "legacy_single_attempt did not send exactly one initial CM_SET_KEY.REQ")) {
        return false;
    }
    if (!assert_stays_at_count(sent_messages, 1, machine, 20)) {
        return assert_true(false, test_name, "legacy_single_attempt retried CM_SET_KEY.REQ before CNF");
    }

    auto set_key_messages = sort_set_key_messages(sent_messages);
    auto initial_nmk = extract_nmk(set_key_messages.front().hp_message);
    auto session_nmk_before = current_session_nmk(ctx);
    if (!assert_true(expect_nmk_equal(session_nmk_before, initial_nmk), test_name,
                     "legacy_single_attempt should send session_nmk on CM_SET_KEY.REQ")) {
        return false;
    }

    if (!assert_true(not ctx.status.modem_NMK, test_name, "legacy_single_attempt set modem_NMK before CNF")) {
        return false;
    }

    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS));
    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 150)) {
        return assert_true(false, test_name, "did not leave Reset on CNF");
    }

    if (!assert_stays_at_count(sent_messages, 1, machine, 50)) {
        return assert_true(false, test_name, "legacy_single_attempt retried CM_SET_KEY.REQ after CNF");
    }

    if (!assert_true(count_set_key_messages(sent_messages) == 1, test_name,
                     "legacy_single_attempt sent unexpected CM_SET_KEY.REQ count")) {
        return false;
    }

    if (!assert_true(ctx.status.modem_NMK, test_name, "legacy_single_attempt did not set modem_NMK on valid success")) {
        return false;
    }

    return true;
}

bool test_legacy_single_attempt_rejects_reserved_result() {
    const char* test_name = "legacy_single_attempt_rejects_reserved_result";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    ctx.slac_config.set_key_timeout_ms = 20;
    fill_session_nmk(ctx, 0x22);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    machine.message(create_cm_set_key_cnf_reserved());
    if (!assert_stays_at_count(sent_messages, 1, machine, 30)) {
        return assert_true(false, test_name, "legacy_single_attempt retried CM_SET_KEY.REQ on reserved result");
    }

    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 100)) {
        return assert_true(false, test_name, "did not reach Failed on reserved result");
    }

    if (!assert_true(not ctx.status.modem_NMK, test_name,
                     "legacy_single_attempt incorrectly set modem_NMK on failure")) {
        return false;
    }

    return true;
}

bool test_legacy_single_attempt_accepts_compat_success_cnf() {
    const char* test_name = "legacy_single_attempt_accepts_compat_success_cnf";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    ctx.slac_config.set_key_timeout_ms = 20;
    fill_session_nmk(ctx, 0x23);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    auto compat_cnf = SetKeyCnfParams{};
    compat_cnf.source = messages::HomeplugMessage::MacAddress{{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
    compat_cnf.pid = defs::CM_SET_KEY_REQ_PID_HLE + 1;
    compat_cnf.my_nonce = 1;
    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS, compat_cnf));

    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 150)) {
        return assert_true(false, test_name, "did not leave Reset on compatible success CNF");
    }

    if (!assert_stays_at_count(sent_messages, 1, machine, 50)) {
        return assert_true(false, test_name, "legacy_single_attempt retried after compatible success CNF");
    }

    return assert_true(ctx.status.modem_NMK, test_name,
                       "legacy_single_attempt did not set modem_NMK on compatible success CNF");
}

bool test_legacy_single_attempt_reaches_failed() {
    const char* test_name = "legacy_single_attempt_reaches_failed";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    fill_session_nmk(ctx, 0x22);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    auto failed = wait_for_match_state(ctx, SlacState::Failed, machine, 100);
    if (!assert_true(failed, test_name, "did not reach Failed on set-key timeout")) {
        return false;
    }

    return assert_true(count_set_key_messages(sent_messages) == 1, test_name,
                       "legacy_single_attempt attempted more than one CM_SET_KEY.REQ");
}

bool test_retry_confirmed_accepts_wrong_source() {
    const char* test_name = "retry_confirmed_accepts_wrong_source";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_timeout_ms = 80;
    fill_session_nmk(ctx, 0x33);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    SetKeyCnfParams wrong_source{};
    wrong_source.source = messages::HomeplugMessage::MacAddress{{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}};
    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS, wrong_source));

    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 100)) {
        return assert_true(false, test_name, "retry_confirmed did not accept wrong-source success CNF");
    }

    if (!assert_stays_at_count(sent_messages, 1, machine, 50)) {
        return assert_true(false, test_name, "retry_confirmed retried after wrong-source success CNF");
    }

    return assert_true(ctx.status.modem_NMK, test_name,
                       "retry_confirmed did not set modem_NMK on wrong-source success CNF");
}

bool test_retry_confirmed_accepts_malformed_fields() {
    const char* test_name = "retry_confirmed_accepts_malformed_fields";

    const auto run_case = [test_name](SetKeyCnfParams const& params, uint8_t nmk_base, const char* details) {
        ContextCallbacks callbacks{};
        std::vector<SetKeySentMessage> sent_messages;
        callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
            sent_messages.push_back({sent_messages.size(), hp_message});
            return true;
        };

        Context ctx(callbacks);
        configure_common(ctx);
        ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
        ctx.slac_config.set_key_timeout_ms = 80;
        fill_session_nmk(ctx, nmk_base);

        slac_fsm machine(ctx);
        machine.restart_fsm();
        machine.reset();

        if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
            return assert_true(false, test_name, "did not enter Reset state");
        }

        if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
            return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
        }

        machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS, params));
        if (!wait_for_match_state(ctx, SlacState::Idle, machine, 100)) {
            return assert_true(false, test_name, details);
        }

        if (!assert_stays_at_count(sent_messages, 1, machine, 50)) {
            return assert_true(false, test_name, "retry_confirmed retried after compatible success CNF");
        }

        return assert_true(ctx.status.modem_NMK, test_name,
                           "retry_confirmed did not set modem_NMK on compatible success CNF");
    };

    auto malformed_pid = SetKeyCnfParams{};
    malformed_pid.pid = defs::CM_SET_KEY_REQ_PID_HLE + 1;
    if (!run_case(malformed_pid, 0x44, "retry_confirmed did not accept malformed pid success CNF")) {
        return false;
    }

    auto malformed_prn = SetKeyCnfParams{};
    malformed_prn.prn = defs::CM_SET_KEY_REQ_PRN_UNUSED + 1;
    if (!run_case(malformed_prn, 0x45, "retry_confirmed did not accept malformed prn success CNF")) {
        return false;
    }

    auto malformed_pmn = SetKeyCnfParams{};
    malformed_pmn.pmn = defs::CM_SET_KEY_REQ_PMN_UNUSED + 1;
    if (!run_case(malformed_pmn, 0x46, "retry_confirmed did not accept malformed pmn success CNF")) {
        return false;
    }

    auto malformed_nonce = SetKeyCnfParams{};
    malformed_nonce.my_nonce = 1;
    malformed_nonce.your_nonce = 1;
    if (!run_case(malformed_nonce, 0x47, "retry_confirmed did not accept nonzero nonce success CNF")) {
        return false;
    }

    return true;
}

bool test_retry_confirmed_reserved_result_is_failure() {
    const char* test_name = "retry_confirmed_reserved_result_is_failure";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_max_attempts = 2;
    fill_session_nmk(ctx, 0x55);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    machine.message(create_cm_set_key_cnf_reserved());
    if (!wait_for_set_key_count(sent_messages, 2, machine, 150)) {
        return assert_true(false, test_name, "retry_confirmed did not retry on reserved result");
    }
    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "retry_confirmed no longer in Reset after first reserved result");
    }

    machine.message(create_cm_set_key_cnf_reserved());
    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 150)) {
        return assert_true(false, test_name, "retry_confirmed did not give up to Idle on max reserved failures");
    }

    if (!assert_true(count_set_key_messages(sent_messages) == 2, test_name,
                     "did not stop retries at max on reserved result")) {
        return false;
    }
    if (!assert_true(not ctx.status.modem_NMK, test_name, "modem_NMK set on reserved failure path")) {
        return false;
    }

    return true;
}

bool test_default_set_key_handling_mode_is_retry_confirmed() {
    const char* test_name = "test_default_set_key_handling_mode_is_retry_confirmed";

    ContextCallbacks callbacks{};
    Context ctx(callbacks);

    return assert_true(ctx.slac_config.set_key_handling_mode == SetKeyHandlingMode::retry_confirmed, test_name,
                       "default set_key_handling_mode is not retry_confirmed");
}

bool test_default_set_key_cnf_success_mode_is_modem_compat() {
    const char* test_name = "test_default_set_key_cnf_success_mode_is_modem_compat";

    ContextCallbacks callbacks{};
    Context ctx(callbacks);

    return assert_true(ctx.slac_config.set_key_cnf_success_mode == SetKeyCnfSuccessMode::modem_compat_0x01, test_name,
                       "default set_key_cnf_success_mode is not modem_compat_0x01");
}

bool test_retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk() {
    const char* test_name = "retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_max_attempts = 4;
    fill_session_nmk(ctx, 0x33);
    auto session_nmk_before = current_session_nmk(ctx);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    for (int attempt = 0; attempt < ctx.slac_config.set_key_max_attempts; ++attempt) {
        auto set_key_messages = sort_set_key_messages(sent_messages);
        if (not assert_true(not set_key_messages.empty(), test_name, "missing CM_SET_KEY.REQ in history")) {
            return false;
        }
        auto current_count = set_key_messages.size();
        auto last_nmk = extract_nmk(set_key_messages.back().hp_message);
        if (attempt == 0) {
            (void)last_nmk;
        }

        machine.message(create_cm_set_key_cnf_reserved());
        if (!assert_true(expect_nmk_equal(ctx.slac_config.session_nmk, session_nmk_before), test_name,
                         "session_nmk changed before success")) {
            return false;
        }

        if (attempt + 1 < ctx.slac_config.set_key_max_attempts) {
            if (!wait_for_set_key_count(sent_messages, current_count + 1, machine, 100)) {
                return assert_true(false, test_name, "retry_confirmed did not retry next CM_SET_KEY.REQ");
            }
        } else {
            if (!wait_for_match_state(ctx, SlacState::Idle, machine, 100)) {
                return assert_true(false, test_name, "retry_confirmed did not return to Idle after max attempts");
            }
            if (!assert_true(assert_stays_at_count(sent_messages, current_count, machine, 50), test_name,
                             "retry_confirmed sent extra CM_SET_KEY.REQ after max attempts")) {
                return false;
            }
        }
    }

    if (!assert_true(count_set_key_messages(sent_messages) == static_cast<size_t>(ctx.slac_config.set_key_max_attempts),
                     test_name, "retry_confirmed did not retry up to set_key_max_attempts")) {
        return false;
    }

    return true;
}

bool run_retry_confirmed_success(uint8_t result, SetKeyCnfSuccessMode success_mode, const char* test_name) {
    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_cnf_success_mode = success_mode;
    ctx.slac_config.set_key_max_attempts = 4;
    fill_session_nmk(ctx, 0x44);
    auto session_nmk_before = current_session_nmk(ctx);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    auto set_key_messages = sort_set_key_messages(sent_messages);
    auto request_nmk = extract_nmk(set_key_messages.front().hp_message);

    machine.message(create_cm_set_key_cnf(result));
    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 100)) {
        return assert_true(false, test_name, "did not transition to Idle on success");
    }

    auto updated_nmk = current_session_nmk(ctx);
    if (!assert_true(expect_nmk_equal(updated_nmk, request_nmk), test_name,
                     "session_nmk did not match CM_SET_KEY.REQ new_key")) {
        return false;
    }
    if (!assert_true(not expect_nmk_equal(updated_nmk, session_nmk_before), test_name,
                     "session_nmk did not change after successful CM_SET_KEY.CNF")) {
        return false;
    }

    return assert_true(ctx.status.modem_NMK, test_name, "retry_confirmed did not set modem_NMK on valid success");
}

bool test_default_retries_on_hpgp_0x00_success_result() {
    const char* test_name = "test_default_retries_on_hpgp_0x00_success_result";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_timeout_ms = 80;
    ctx.slac_config.set_key_max_attempts = 3;
    fill_session_nmk(ctx, 0x55);
    auto session_nmk_before = current_session_nmk(ctx);

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }
    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }

    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS));
    if (!wait_for_set_key_count(sent_messages, 2, machine, 150)) {
        return assert_true(false, test_name, "default modem_compat_0x01 did not retry after 0x00 CNF");
    }
    if (!assert_true(ctx.status.match_state == SlacState::Reset, test_name, "default 0x00 CNF left Reset state")) {
        return false;
    }
    if (!assert_true(not ctx.status.modem_NMK, test_name, "default 0x00 CNF marked modem_NMK true")) {
        return false;
    }
    return assert_true(expect_nmk_equal(ctx.slac_config.session_nmk, session_nmk_before), test_name,
                       "default 0x00 CNF promoted session_nmk");
}

bool test_short_cm_set_key_cnf_keeps_reset() {
    const char* test_name = "test_short_cm_set_key_cnf_keeps_reset";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_timeout_ms = 150;

    slac_fsm machine(ctx);
    machine.restart_fsm();
    machine.reset();

    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    if (!wait_for_set_key_count(sent_messages, 1, machine, 100)) {
        return assert_true(false, test_name, "did not send initial CM_SET_KEY.REQ");
    }
    auto initial_count = count_set_key_messages(sent_messages);

    auto const short_cm_set_key_cnf = create_short_cm_set_key_cnf();
    if (!assert_true(short_cm_set_key_cnf.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_SET_KEY.CNF is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cm_set_key_cnf.has_payload<std::uint8_t>(), test_name,
                     "short CM_SET_KEY.CNF exposes HomePlug payload bytes")) {
        return false;
    }
    machine.message(short_cm_set_key_cnf);
    auto stayed_in_reset =
        not wait_for(std::chrono::milliseconds(80), machine, [&ctx, &sent_messages, initial_count]() {
            return ctx.status.match_state != SlacState::Reset || count_set_key_messages(sent_messages) > initial_count;
        });
    return assert_true(stayed_in_reset, test_name, "short CM_SET_KEY.CNF changed state or emitted retry");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 13>{
        std::make_pair("legacy_single_attempt_accepts_valid_success_result",
                       test_legacy_single_attempt_accepts_valid_success_result),
        std::make_pair("legacy_single_attempt_rejects_reserved_result",
                       test_legacy_single_attempt_rejects_reserved_result),
        std::make_pair("legacy_single_attempt_accepts_compat_success_cnf",
                       test_legacy_single_attempt_accepts_compat_success_cnf),
        std::make_pair("legacy_single_attempt_reaches_failed", test_legacy_single_attempt_reaches_failed),
        std::make_pair("retry_confirmed_accepts_wrong_source", test_retry_confirmed_accepts_wrong_source),
        std::make_pair("retry_confirmed_accepts_malformed_fields", test_retry_confirmed_accepts_malformed_fields),
        std::make_pair("retry_confirmed_reserved_result_is_failure", test_retry_confirmed_reserved_result_is_failure),
        std::make_pair("retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk",
                       test_retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk),
        std::make_pair("test_default_set_key_handling_mode_is_retry_confirmed",
                       test_default_set_key_handling_mode_is_retry_confirmed),
        std::make_pair("test_default_set_key_cnf_success_mode_is_modem_compat",
                       test_default_set_key_cnf_success_mode_is_modem_compat),
        std::make_pair("test_default_retries_on_hpgp_0x00_success_result",
                       test_default_retries_on_hpgp_0x00_success_result),
        std::make_pair("retry_confirmed_hpgp_standard_accepts_0x00_success",
                       [] {
                           return run_retry_confirmed_success(defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS,
                                                              SetKeyCnfSuccessMode::hpgp_standard_0x00,
                                                              "retry_confirmed_hpgp_standard_accepts_0x00_success");
                       }),
        std::make_pair("test_short_cm_set_key_cnf_keeps_reset", test_short_cm_set_key_cnf_keeps_reset),
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

    if (run_retry_confirmed_success(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS,
                                    SetKeyCnfSuccessMode::modem_compat_0x01,
                                    "retry_confirmed_accepts_0x01_success") == false) {
        std::printf("[FAIL] retry_confirmed_accepts_0x01_success\n");
        ++failed_count;
    } else {
        std::printf("[PASS] retry_confirmed_accepts_0x01_success\n");
    }

    if (run_retry_confirmed_success(defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS, SetKeyCnfSuccessMode::accept_0x00_or_0x01,
                                    "retry_confirmed_dual_accepts_0x00_success") == false) {
        std::printf("[FAIL] retry_confirmed_dual_accepts_0x00_success\n");
        ++failed_count;
    } else {
        std::printf("[PASS] retry_confirmed_dual_accepts_0x00_success\n");
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
