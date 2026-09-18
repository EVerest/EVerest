// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// ResetChip sub-machine: the optional modem reset between CM_SET_KEY.CNF and Idle must always end in
// Idle, whether the modem confirms (Qualcomm), never confirms (Lumissil), cannot be reset, or stays
// silent past chip_reset.timeout.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_fsm.hpp>

#include "mock_clock.hpp"

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;
using namespace std::chrono_literals;

namespace {

constexpr messages::HomeplugMessage::MacAddress default_peer_mac{{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01}};

// The clock every Context in this file runs on; wait_for() advances it one millisecond per update tick.
test::MockClock test_clock;

struct Harness {
    std::vector<messages::HomeplugMessage> sent;
    std::vector<std::string> infos;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
    ContextCallbacks callbacks{};
};

void wire(Harness& h) {
    h.callbacks.send_raw_slac = [&h](messages::HomeplugMessage& msg) {
        h.sent.push_back(msg);
        return true;
    };
    h.callbacks.log_info = [&h](const std::string& s) { h.infos.push_back(s); };
    h.callbacks.log_warn = [&h](const std::string& s) { h.warnings.push_back(s); };
    h.callbacks.log_error = [&h](const std::string& s) { h.errors.push_back(s); };
    h.callbacks.now = test_clock.source();
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
    auto const deadline = test_clock.now() + timeout;
    while (test_clock.now() < deadline) {
        if (predicate()) {
            return true;
        }
        machine.update();
        test_clock.advance_ms(1);
    }
    return predicate();
}

bool wait_for_match_state(Context const& ctx, SlacState expected, slac_fsm& machine, int timeout_ms) {
    return wait_for(std::chrono::milliseconds(timeout_ms), machine,
                    [&ctx, expected]() { return ctx.status.match_state == expected; });
}

// Drives the machine for the whole duration and reports whether the state stayed put.
bool stays_in_state(Context const& ctx, SlacState expected, slac_fsm& machine, int duration_ms) {
    auto left = wait_for(std::chrono::milliseconds(duration_ms), machine,
                         [&ctx, expected]() { return ctx.status.match_state != expected; });
    return not left;
}

std::size_t count_mmtype(std::vector<messages::HomeplugMessage> const& sent, std::uint16_t mmtype) {
    return static_cast<std::size_t>(
        std::count_if(sent.begin(), sent.end(), [mmtype](auto const& m) { return m.get_mmtype() == mmtype; }));
}

std::size_t count_reset_requests(std::vector<messages::HomeplugMessage> const& sent) {
    return count_mmtype(sent, defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_REQ);
}

std::size_t count_set_key_requests(std::vector<messages::HomeplugMessage> const& sent) {
    return count_mmtype(sent, defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ);
}

bool any_contains(std::vector<std::string> const& lines, const char* needle) {
    return std::any_of(lines.begin(), lines.end(),
                       [needle](auto const& l) { return l.find(needle) != std::string::npos; });
}

messages::HomeplugMessage create_cm_set_key_cnf() {
    messages::cm_set_key_cnf cnf{};
    cnf.result = defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS;
    cnf.pid = defs::CM_SET_KEY_REQ_PID_HLE;
    cnf.prn = defs::CM_SET_KEY_REQ_PRN_UNUSED;
    cnf.pmn = defs::CM_SET_KEY_REQ_PMN_UNUSED;
    messages::HomeplugMessage message;
    message.set_source(default_peer_mac);
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_qualcomm_reset_device_cnf() {
    messages::qualcomm::cm_reset_device_cnf msg{};
    msg.success = 0x00;
    messages::HomeplugMessage message;
    message.set_source(default_peer_mac);
    message.setup_payload(&msg, sizeof(msg), defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_0);
    return message;
}

constexpr int chip_reset_delay_ms = 5;
constexpr int chip_reset_timeout_ms = 50;

void configure(Context& ctx, defs::ModemVendor vendor) {
    ctx.slac_config.request_info_delay = 1ms;
    ctx.slac_config.set_key_timeout = 5ms;
    ctx.slac_config.set_key_handling_mode = SetKeyHandlingMode::legacy_single_attempt;
    ctx.slac_config.slac_init_timeout = 5ms;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.regenerate_key_on_reset = true;
    ctx.slac_config.ac_mode_five_percent = false;
    ctx.slac_config.chip_reset.enabled = true;
    ctx.slac_config.chip_reset.delay = std::chrono::milliseconds{chip_reset_delay_ms};
    ctx.slac_config.chip_reset.timeout = std::chrono::milliseconds{chip_reset_timeout_ms};
    ctx.modem_vendor = vendor;
}

// Reset -> (CM_SET_KEY.CNF) -> ResetChip. Returns false if the machine never got there.
bool enter_reset_chip(Context const& ctx, slac_fsm& machine, const char* test_name) {
    machine.restart_fsm();
    machine.reset();
    if (not wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset");
    }
    machine.message(create_cm_set_key_cnf());
    if (not wait_for_match_state(ctx, SlacState::ResetChip, machine, 100)) {
        return assert_true(false, test_name, "did not enter ResetChip after CM_SET_KEY.CNF");
    }
    return true;
}

bool test_qualcomm_cnf_reaches_idle_before_timeout() {
    const char* test_name = "qualcomm_cnf_reaches_idle_before_timeout";
    Harness h;
    wire(h);
    Context ctx(h.callbacks);
    configure(ctx, defs::ModemVendor::Qualcomm);
    slac_fsm machine(ctx);
    if (not enter_reset_chip(ctx, machine, test_name)) {
        return false;
    }
    if (not wait_for(std::chrono::milliseconds(chip_reset_delay_ms + 5), machine,
                     [&h]() { return count_reset_requests(h.sent) == 1; })) {
        return assert_true(false, test_name, "did not send CM_RESET_DEVICE.REQ after chip_reset.delay");
    }
    if (not assert_true(any_contains(h.infos, "CM_RESET_DEVICE.REQ"), test_name, "sending the reset was not logged")) {
        return false;
    }
    machine.message(create_qualcomm_reset_device_cnf());
    if (not wait_for_match_state(ctx, SlacState::Idle, machine, 5)) {
        return assert_true(false, test_name, "did not reach Idle right after CM_RESET_DEVICE.CNF");
    }
    if (not assert_true(h.errors.empty(), test_name, "logged an error although the modem confirmed")) {
        return false;
    }
    return assert_true(count_reset_requests(h.sent) == 1, test_name, "sent more than one CM_RESET_DEVICE.REQ");
}

bool test_qualcomm_silent_modem_times_out_to_idle() {
    const char* test_name = "qualcomm_silent_modem_times_out_to_idle";
    Harness h;
    wire(h);
    Context ctx(h.callbacks);
    configure(ctx, defs::ModemVendor::Qualcomm);
    slac_fsm machine(ctx);
    if (not enter_reset_chip(ctx, machine, test_name)) {
        return false;
    }
    if (not wait_for(std::chrono::milliseconds(chip_reset_delay_ms + 5), machine,
                     [&h]() { return count_reset_requests(h.sent) == 1; })) {
        return assert_true(false, test_name, "did not send CM_RESET_DEVICE.REQ");
    }
    // Well inside the timeout the machine must still be waiting for the CNF.
    if (not stays_in_state(ctx, SlacState::ResetChip, machine, chip_reset_timeout_ms / 2)) {
        return assert_true(false, test_name, "left ResetChip before chip_reset.timeout");
    }
    if (not assert_true(h.errors.empty(), test_name, "logged the timeout error too early")) {
        return false;
    }
    if (not wait_for_match_state(ctx, SlacState::Idle, machine, chip_reset_timeout_ms)) {
        return assert_true(false, test_name, "did not reach Idle after chip_reset.timeout without CNF");
    }
    if (not assert_true(any_contains(h.errors, "Reset timeout"), test_name, "timeout was not logged as error")) {
        return false;
    }
    return assert_true(count_reset_requests(h.sent) == 1, test_name, "retried CM_RESET_DEVICE.REQ");
}

bool test_lumissil_proceeds_without_waiting() {
    const char* test_name = "lumissil_proceeds_without_waiting";
    Harness h;
    wire(h);
    Context ctx(h.callbacks);
    configure(ctx, defs::ModemVendor::Lumissil);
    slac_fsm machine(ctx);
    if (not enter_reset_chip(ctx, machine, test_name)) {
        return false;
    }
    if (not wait_for_match_state(ctx, SlacState::Idle, machine, chip_reset_delay_ms + 5)) {
        return assert_true(false, test_name, "did not reach Idle right after sending NSCM_RESET_DEVICE.REQ");
    }
    auto const lumissil_reset = defs::lumissil::MMTYPE_NSCM_RESET_DEVICE | defs::MMTYPE_MODE_REQ;
    if (not assert_true(count_mmtype(h.sent, lumissil_reset) == 1, test_name, "did not send NSCM_RESET_DEVICE.REQ")) {
        return false;
    }
    return assert_true(h.errors.empty() and h.warnings.empty(), test_name, "Lumissil path logged a warning or error");
}

bool test_unsupported_modem_skips_reset() {
    const char* test_name = "unsupported_modem_skips_reset";
    Harness h;
    wire(h);
    Context ctx(h.callbacks);
    configure(ctx, defs::ModemVendor::Unknown);
    slac_fsm machine(ctx);
    if (not enter_reset_chip(ctx, machine, test_name)) {
        return false;
    }
    if (not wait_for_match_state(ctx, SlacState::Idle, machine, chip_reset_delay_ms + 5)) {
        return assert_true(false, test_name, "did not reach Idle promptly for an unsupported modem");
    }
    if (not assert_true(count_reset_requests(h.sent) == 0, test_name, "sent a reset request to an unsupported modem")) {
        return false;
    }
    if (not assert_true(any_contains(h.warnings, "not supported"), test_name, "skipping the reset was not logged")) {
        return false;
    }
    return assert_true(h.errors.empty(), test_name, "unsupported modem logged an error");
}

bool test_reset_while_waiting_for_cnf_restarts() {
    const char* test_name = "reset_while_waiting_for_cnf_restarts";
    Harness h;
    wire(h);
    Context ctx(h.callbacks);
    configure(ctx, defs::ModemVendor::Qualcomm);
    slac_fsm machine(ctx);
    if (not enter_reset_chip(ctx, machine, test_name)) {
        return false;
    }
    if (not wait_for(std::chrono::milliseconds(chip_reset_delay_ms + 5), machine,
                     [&h]() { return count_reset_requests(h.sent) == 1; })) {
        return assert_true(false, test_name, "did not send CM_RESET_DEVICE.REQ");
    }
    auto const set_keys_before = count_set_key_requests(h.sent);
    machine.reset();
    if (not wait_for_match_state(ctx, SlacState::Reset, machine, 5)) {
        return assert_true(false, test_name, "reset during ResetChip did not return to Reset");
    }
    if (not wait_for(20ms, machine,
                     [&h, set_keys_before]() { return count_set_key_requests(h.sent) > set_keys_before; })) {
        return assert_true(false, test_name, "Reset after ResetChip did not send a new CM_SET_KEY.REQ");
    }
    return assert_true(h.errors.empty(), test_name, "reset during ResetChip logged an error");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 5>{
        std::make_pair("qualcomm_cnf_reaches_idle_before_timeout", test_qualcomm_cnf_reaches_idle_before_timeout),
        std::make_pair("qualcomm_silent_modem_times_out_to_idle", test_qualcomm_silent_modem_times_out_to_idle),
        std::make_pair("lumissil_proceeds_without_waiting", test_lumissil_proceeds_without_waiting),
        std::make_pair("unsupported_modem_skips_reset", test_unsupported_modem_skips_reset),
        std::make_pair("reset_while_waiting_for_cnf_restarts", test_reset_while_waiting_for_cnf_restarts),
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
    return failed_count == 0 ? 0 : 1;
}
