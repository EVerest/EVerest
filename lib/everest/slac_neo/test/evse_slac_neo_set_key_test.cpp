// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <thread>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_fsm.hpp>

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;

namespace {

struct SetKeySentMessage {
    std::size_t sequence;
    messages::HomeplugMessage hp_message;
};

messages::HomeplugMessage create_cm_set_key_cnf(std::uint8_t result) {
    messages::cm_set_key_cnf cnf{};
    cnf.result = result;
    cnf.my_nonce = 0;
    cnf.your_nonce = 0;
    cnf.pid = 0;
    cnf.prn = 0;
    cnf.pmn = 0;

    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_set_key_cnf_reserved() {
    return create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS + 0x80);
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

template <typename Predicate> bool wait_for(std::chrono::milliseconds timeout, slac_fsm& machine, Predicate&& predicate) {
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

bool assert_stays_at_count(std::vector<SetKeySentMessage> const& sent_messages, size_t expected_count, slac_fsm& machine,
                          int timeout_ms) {
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

bool test_legacy_single_attempt_accepts_any_result_and_no_retry() {
    const char* test_name = "legacy_single_attempt_accepts_any_result_and_no_retry";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    ctx.slac_config.set_key_timeout_ms = 50;
    fill_session_nmk(ctx, 0x11);

    slac_fsm machine(ctx);
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

    machine.message(create_cm_set_key_cnf_reserved());
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

    return true;
}

bool test_legacy_single_attempt_reaches_failed() {
    const char* test_name = "legacy_single_attempt_reaches_failed";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    fill_session_nmk(ctx, 0x22);

    slac_fsm machine(ctx);
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

bool test_retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk() {
    const char* test_name = "retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk";

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_max_attempts = 4;
    fill_session_nmk(ctx, 0x33);
    auto session_nmk_before = current_session_nmk(ctx);

    slac_fsm machine(ctx);
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

    if (!assert_true(count_set_key_messages(sent_messages) == static_cast<size_t>(ctx.slac_config.set_key_max_attempts), test_name,
                    "retry_confirmed did not retry up to set_key_max_attempts")) {
        return false;
    }

    return true;
}

bool run_retry_confirmed_success(uint8_t result) {
    std::array<const char*, 32> test_name_values = {"retry_confirmed_accepts_0x00_success", "retry_confirmed_accepts_0x01_success"};
    auto const& test_name = result == defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS ? test_name_values[0] : test_name_values[1];

    ContextCallbacks callbacks{};
    std::vector<SetKeySentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::retry_confirmed;
    ctx.slac_config.set_key_max_attempts = 4;
    fill_session_nmk(ctx, 0x44);
    auto session_nmk_before = current_session_nmk(ctx);

    slac_fsm machine(ctx);
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
    if (!assert_true(expect_nmk_equal(updated_nmk, request_nmk), test_name, "session_nmk did not match CM_SET_KEY.REQ new_key")) {
        return false;
    }
    if (!assert_true(not expect_nmk_equal(updated_nmk, session_nmk_before), test_name,
                     "session_nmk did not change after successful CM_SET_KEY.CNF")) {
        return false;
    }

    return true;
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 4>{
        std::make_pair("legacy_single_attempt_accepts_any_result_and_no_retry", test_legacy_single_attempt_accepts_any_result_and_no_retry),
        std::make_pair("legacy_single_attempt_reaches_failed", test_legacy_single_attempt_reaches_failed),
        std::make_pair("retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk",
                       test_retry_confirmed_retries_until_attempt_limit_without_promoting_session_nmk),
        std::make_pair("retry_confirmed_accepts_0x00_success", [] { return run_retry_confirmed_success(defs::CM_SET_KEY_CNF_RESULT_HPGP_SUCCESS); }),
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

    if (run_retry_confirmed_success(defs::CM_SET_KEY_CNF_RESULT_SUCCESS) == false) {
        std::printf("[FAIL] retry_confirmed_accepts_0x01_success\n");
        ++failed_count;
    } else {
        std::printf("[PASS] retry_confirmed_accepts_0x01_success\n");
    }

    if (failed_count > 0) {
        std::printf("FAILED (%d)\n", failed_count);
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
