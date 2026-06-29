// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <net/ethernet.h>
#include <thread>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/context.hpp>
#include <everest/slac/slac_fsm.hpp>

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::evse;

namespace {

using RunId = std::array<uint8_t, defs::RUN_ID_LEN>;
using EvMac = messages::HomeplugMessage::MacAddress;
using Nmk = std::array<uint8_t, defs::NMK_LEN>;

struct SentMessage {
    std::size_t sequence;
    messages::HomeplugMessage hp_message;
};

messages::HomeplugMessage create_cm_set_key_cnf(std::uint8_t result) {
    messages::cm_set_key_cnf cnf{};
    cnf.result = result;

    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF, defs::MMV::AV_1_1);
    return message;
}

messages::cm_slac_match_req create_cm_slac_match_req_payload(EvMac const& ev_mac, RunId const& run_id,
                                                             EvMac const& evse_mac) {
    messages::cm_slac_match_req msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.mvf_length = defs::CM_SLAC_MATCH_REQ_MVF_LENGTH;
    std::copy(ev_mac.begin(), ev_mac.end(), msg.pev_mac);
    std::copy(evse_mac.begin(), evse_mac.end(), msg.evse_mac);
    std::copy(run_id.begin(), run_id.end(), msg.run_id);
    return msg;
}

messages::HomeplugMessage wrap_cm_slac_match_req(EvMac const& source_mac, messages::cm_slac_match_req const& msg) {
    messages::HomeplugMessage message;
    message.set_source(source_mac);
    message.setup_payload(&msg, sizeof(msg), defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_slac_parm_req(EvMac const& ev_mac, RunId const& run_id, bool valid = true) {
    messages::cm_slac_parm_req req{};
    if (valid) {
        req.application_type = defs::COMMON_APPLICATION_TYPE;
        req.security_type = defs::COMMON_SECURITY_TYPE;
    }
    std::copy(run_id.begin(), run_id.end(), req.run_id);
    if (!valid) {
        req.application_type = 0x80;
        req.security_type = 0x80;
    }

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&req, sizeof(req), defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_short_cm_slac_parm_req(EvMac const& ev_mac, RunId const& run_id, bool valid = true) {
    auto message = create_cm_slac_parm_req(ev_mac, run_id, valid);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::homeplug_fragmentation_part) +
                                 sizeof(messages::cm_slac_parm_req));
    return message;
}

messages::HomeplugMessage create_cm_start_atten_char_ind(EvMac const& ev_mac, RunId const& run_id) {
    messages::cm_start_atten_char_ind msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.num_sounds = defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    msg.timeout = defs::CM_SLAC_PARM_CNF_TIMEOUT;
    msg.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
    std::copy(run_id.begin(), run_id.end(), msg.run_id);

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&msg, sizeof(msg), defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND,
                          defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_atten_profile_ind(EvMac const& ev_mac, uint8_t seed = 0x00) {
    messages::cm_atten_profile_ind msg{};
    std::copy(ev_mac.begin(), ev_mac.end(), msg.pev_mac);
    msg.num_groups = defs::AAG_LIST_LEN;
    for (int i = 0; i < defs::AAG_LIST_LEN; ++i) {
        msg.aag[i] = static_cast<uint8_t>(seed + i);
    }

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&msg, sizeof(msg), defs::MMTYPE_CM_ATTEN_PROFILE | defs::MMTYPE_MODE_IND, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_atten_char_rsp(EvMac const& ev_mac, RunId const& run_id,
                                                   uint8_t result = defs::CM_ATTEN_CHAR_RSP_RESULT) {
    messages::cm_atten_char_rsp msg{};
    msg.application_type = defs::COMMON_APPLICATION_TYPE;
    msg.security_type = defs::COMMON_SECURITY_TYPE;
    msg.result = result;
    std::copy(ev_mac.begin(), ev_mac.end(), msg.source_address);
    std::copy(run_id.begin(), run_id.end(), msg.run_id);

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&msg, sizeof(msg), defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_RSP, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_cm_slac_match_req(EvMac const& ev_mac, RunId const& run_id, EvMac const& evse_mac) {
    return wrap_cm_slac_match_req(ev_mac, create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac));
}

messages::HomeplugMessage create_short_cm_slac_match_req(EvMac const& ev_mac, RunId const& run_id,
                                                         EvMac const& evse_mac) {
    auto message = create_cm_slac_match_req(ev_mac, run_id, evse_mac);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET);
    return message;
}

messages::HomeplugMessage create_cm_validate_req(EvMac const& ev_mac) {
    messages::cm_validate_req msg{};
    msg.signal_type = defs::CM_VALIDATE_REQ_SIGNAL_TYPE;
    msg.timer = 0;
    msg.result = defs::CM_VALIDATE_REQ_RESULT_READY;

    messages::HomeplugMessage message;
    message.set_source(ev_mac);
    message.setup_payload(&msg, sizeof(msg), defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_REQ, defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_lumissil_link_status_cnf(EvMac const& source_mac, uint8_t link_status) {
    messages::lumissil::nscm_get_d_link_status_cnf msg{};
    msg.link_status = link_status;

    messages::HomeplugMessage message;
    message.set_source(source_mac);
    message.setup_payload(&msg, sizeof(msg), defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_0);
    return message;
}

messages::HomeplugMessage create_short_lumissil_link_status_cnf(EvMac const& source_mac, uint8_t link_status) {
    auto message = create_lumissil_link_status_cnf(source_mac, link_status);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET +
                                 sizeof(messages::lumissil::nscm_get_d_link_status_cnf));
    return message;
}

messages::HomeplugMessage create_qualcomm_link_status_cnf(EvMac const& source_mac, uint8_t link_status) {
    messages::qualcomm::link_status_cnf msg{};
    msg.link_status = link_status;

    messages::HomeplugMessage message;
    message.set_source(source_mac);
    message.setup_payload(&msg, sizeof(msg), defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_0);
    return message;
}

messages::HomeplugMessage create_short_qualcomm_link_status_cnf(EvMac const& source_mac, uint8_t link_status) {
    auto message = create_qualcomm_link_status_cnf(source_mac, link_status);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::qualcomm::link_status_cnf));
    return message;
}

bool is_slac_parm_cnf(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF);
}

bool is_cm_atten_char_ind(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND);
}

bool is_cm_validate_cnf(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_CNF);
}

bool is_cm_slac_match_cnf(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_CNF);
}

std::size_t count_slac_parm_cnf(std::vector<SentMessage> const& messages) {
    return std::count_if(messages.begin(), messages.end(),
                         [](auto const& entry) { return is_slac_parm_cnf(entry.hp_message); });
}

std::size_t count_cm_atten_char_ind(std::vector<SentMessage> const& messages) {
    return std::count_if(messages.begin(), messages.end(),
                         [](auto const& entry) { return is_cm_atten_char_ind(entry.hp_message); });
}

std::size_t count_cm_validate_cnf(std::vector<SentMessage> const& messages) {
    return std::count_if(messages.begin(), messages.end(),
                         [](auto const& entry) { return is_cm_validate_cnf(entry.hp_message); });
}

std::size_t count_cm_slac_match_cnf(std::vector<SentMessage> const& messages) {
    return std::count_if(messages.begin(), messages.end(),
                         [](auto const& entry) { return is_cm_slac_match_cnf(entry.hp_message); });
}

bool is_cm_slac_match_cnf_to(messages::HomeplugMessage const& msg, EvMac const& destination_mac) {
    auto const* raw = msg.get_raw_message_ptr();
    return is_cm_slac_match_cnf(msg) && std::equal(std::begin(raw->ethernet_header.ether_dhost),
                                                   std::end(raw->ethernet_header.ether_dhost), destination_mac.begin());
}

std::size_t count_cm_slac_match_cnf_to(std::vector<SentMessage> const& messages, EvMac const& destination_mac) {
    return std::count_if(messages.begin(), messages.end(), [&destination_mac](auto const& entry) {
        return is_cm_slac_match_cnf_to(entry.hp_message, destination_mac);
    });
}

bool get_last_cm_atten_char_ind(std::vector<SentMessage> const& sent_messages,
                                messages::cm_atten_char_ind& atten_char) {
    for (auto it = sent_messages.rbegin(); it != sent_messages.rend(); ++it) {
        if (not is_cm_atten_char_ind(it->hp_message)) {
            continue;
        }
        atten_char = it->hp_message.get_payload<messages::cm_atten_char_ind>();
        return true;
    }
    return false;
}

bool get_last_cm_validate_cnf(std::vector<SentMessage> const& sent_messages, messages::cm_validate_cnf& validate_cnf) {
    for (auto it = sent_messages.rbegin(); it != sent_messages.rend(); ++it) {
        if (not is_cm_validate_cnf(it->hp_message)) {
            continue;
        }
        validate_cnf = it->hp_message.get_payload<messages::cm_validate_cnf>();
        return true;
    }
    return false;
}

bool get_last_cm_slac_match_cnf(std::vector<SentMessage> const& sent_messages, messages::cm_slac_match_cnf& match_cnf) {
    for (auto it = sent_messages.rbegin(); it != sent_messages.rend(); ++it) {
        if (not is_cm_slac_match_cnf(it->hp_message)) {
            continue;
        }
        match_cnf = it->hp_message.get_payload<messages::cm_slac_match_cnf>();
        return true;
    }
    return false;
}

void fill_session_nmk(Context& ctx, uint8_t base) {
    for (std::size_t i = 0; i < defs::NMK_LEN; ++i) {
        ctx.slac_config.session_nmk[i] = static_cast<uint8_t>(base + static_cast<uint8_t>(i));
    }
}

bool expect_nmk_equal(const uint8_t* lhs, Nmk const& rhs) {
    return std::equal(lhs, lhs + defs::NMK_LEN, rhs.begin());
}

bool expect_nmk_equal(Nmk const& lhs, Nmk const& rhs) {
    return lhs == rhs;
}

Nmk expected_failed_match_nmk() {
    return Nmk{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10};
}

std::array<uint8_t, defs::AAG_LIST_LEN> calc_expected_aag(uint8_t seed, std::size_t sample_count, int adjustment) {
    std::array<uint8_t, defs::AAG_LIST_LEN> aags{};
    for (std::size_t group = 0; group < defs::AAG_LIST_LEN; ++group) {
        int total = 0;
        for (std::size_t profile = 0; profile < sample_count; ++profile) {
            total += static_cast<int>(seed) + static_cast<int>(profile) + static_cast<int>(group);
        }
        aags[group] = static_cast<uint8_t>(total / static_cast<int>(sample_count) + adjustment);
    }
    return aags;
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

bool wait_for_parm_cnf_count(std::vector<SentMessage> const& sent_messages, size_t expected, slac_fsm& machine,
                             int timeout_ms) {
    const auto check_count = [&sent_messages, expected]() { return count_slac_parm_cnf(sent_messages) >= expected; };
    return wait_for(std::chrono::milliseconds(timeout_ms), machine, check_count);
}

bool wait_for_atten_char_ind_count(std::vector<SentMessage> const& sent_messages, size_t expected, slac_fsm& machine,
                                   int timeout_ms) {
    const auto check_count = [&sent_messages, expected]() {
        return count_cm_atten_char_ind(sent_messages) >= expected;
    };
    return wait_for(std::chrono::milliseconds(timeout_ms), machine, check_count);
}

bool assert_stays_at_count(std::size_t expected, std::vector<SentMessage> const& sent_messages, slac_fsm& machine,
                           int timeout_ms) {
    const auto observed_count_increase = [&sent_messages, expected]() {
        return count_cm_atten_char_ind(sent_messages) > expected;
    };
    if (count_cm_atten_char_ind(sent_messages) > expected) {
        return false;
    }
    return not wait_for(std::chrono::milliseconds(timeout_ms), machine, observed_count_increase);
}

bool assert_parm_cnf_count_stays_at(std::size_t expected, std::vector<SentMessage> const& sent_messages,
                                    slac_fsm& machine, int timeout_ms) {
    const auto observed_count_increase = [&sent_messages, expected]() {
        return count_slac_parm_cnf(sent_messages) > expected;
    };
    if (count_slac_parm_cnf(sent_messages) > expected) {
        return false;
    }
    return not wait_for(std::chrono::milliseconds(timeout_ms), machine, observed_count_increase);
}

bool assert_validate_cnf_count_stays_at(std::size_t expected, std::vector<SentMessage> const& sent_messages,
                                        slac_fsm& machine, int timeout_ms) {
    const auto observed_count_increase = [&sent_messages, expected]() {
        return count_cm_validate_cnf(sent_messages) > expected;
    };
    if (count_cm_validate_cnf(sent_messages) > expected) {
        return false;
    }
    return not wait_for(std::chrono::milliseconds(timeout_ms), machine, observed_count_increase);
}

bool perform_full_match_sequence(Context& ctx, std::vector<SentMessage>& sent_messages, slac_fsm& machine,
                                 EvMac const& ev_mac, RunId const& run_id, SlacState expected_state, int timeout_ms) {
    const char* test_name = "perform_full_match_sequence";
    const auto initial_parm_count = count_slac_parm_cnf(sent_messages);
    const auto initial_atten_count = count_cm_atten_char_ind(sent_messages);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_parm_count + 1, machine, timeout_ms)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF");
    }
    machine.message(create_cm_start_atten_char_ind(ev_mac, run_id));

    for (std::size_t i = 0; i < defs::CM_SLAC_PARM_CNF_NUM_SOUNDS; ++i) {
        machine.message(create_cm_atten_profile_ind(ev_mac, static_cast<uint8_t>(0xA0 + i)));
    }
    if (!wait_for_atten_char_ind_count(sent_messages, initial_atten_count + 1, machine, timeout_ms)) {
        return assert_true(false, test_name, "did not emit CM_ATTEN_CHAR.IND");
    }

    machine.message(create_cm_atten_char_rsp(ev_mac, run_id));
    EvMac evse_mac{};
    std::copy(std::begin(ctx.evse_mac), std::end(ctx.evse_mac), evse_mac.begin());
    machine.message(create_cm_slac_match_req(ev_mac, run_id, evse_mac));
    if (!wait_for_match_state(ctx, expected_state, machine, timeout_ms)) {
        return assert_true(false, test_name, "did not reach expected post-match state");
    }
    return true;
}

void configure_common(Context& ctx) {
    ctx.slac_config.request_info_delay_ms = 1;
    ctx.slac_config.set_key_timeout_ms = 5;
    ctx.slac_config.set_key_max_attempts = 3;
    ctx.slac_config.slac_init_timeout_ms = 5000;
    ctx.slac_config.chip_reset.enabled = false;
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.regenerate_key_on_reset = true;
    ctx.slac_config.ac_mode_five_percent = false;
}

void configure_wait_for_link(Context& ctx) {
    ctx.modem_vendor = defs::ModemVendor::Lumissil;
    ctx.slac_config.link_status.do_detect = true;
    ctx.slac_config.link_status.retry_ms = 20;
    ctx.slac_config.link_status.timeout_ms = 300;
}

bool enter_matching_state(Context& ctx, slac_fsm& machine) {
    const char* test_name = "enter_matching_state";
    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 200)) {
        return assert_true(false, test_name, "did not enter Reset state");
    }

    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS));
    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 100)) {
        return assert_true(false, test_name, "did not transition to Idle on CM_SET_KEY.CNF");
    }

    machine.enter_bcd();
    if (!wait_for_match_state(ctx, SlacState::Matching, machine, 100)) {
        return assert_true(false, test_name, "did not transition to Matching on enter_bcd");
    }

    return true;
}

RunId fill_run_id(std::uint8_t base) {
    RunId run_id{};
    for (std::size_t i = 0; i < run_id.size(); ++i) {
        run_id[i] = static_cast<uint8_t>(base + i);
    }
    return run_id;
}

bool test_duplicate_cm_slac_parm_req_restarts_same_session() {
    const char* test_name = "test_duplicate_cm_slac_parm_req_restarts_same_session";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.max_matching_sessions = 1;
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    const auto initial_count = count_slac_parm_cnf(sent_messages);
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id = fill_run_id(0x11);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for initial request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "duplicate request created extra session")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 2, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF on duplicate request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "same EV MAC + run_id increased session_count")) {
        return false;
    }

    return true;
}

bool test_duplicate_cm_slac_parm_req_restarts_inflight_session() {
    const char* test_name = "test_duplicate_cm_slac_parm_req_restarts_inflight_session";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id = fill_run_id(0x51);
    const auto initial_parm_cnf_count = count_slac_parm_cnf(sent_messages);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_parm_cnf_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "first request did not create one session")) {
        return false;
    }

    machine.message(create_cm_start_atten_char_ind(ev_mac, run_id));
    machine.message(create_cm_atten_profile_ind(ev_mac));
    if (!assert_true(ctx.status.session_count == 1, test_name, "session_count changed while in sounding")) {
        return false;
    }

    const auto atten_char_count_before_duplicate = count_cm_atten_char_ind(sent_messages);
    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_parm_cnf_count + 2, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for duplicate request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "duplicate request did not keep session count at 1")) {
        return false;
    }

    machine.message(create_cm_start_atten_char_ind(ev_mac, run_id));
    for (int i = 0; i < 9; ++i) {
        machine.message(create_cm_atten_profile_ind(ev_mac, static_cast<uint8_t>(0xA0 + i)));
    }
    if (!assert_true(assert_stays_at_count(atten_char_count_before_duplicate, sent_messages, machine, 80), test_name,
                     "received CM_ATTEN_CHAR.IND before restart-involved profile sequence completed")) {
        return false;
    }

    machine.message(create_cm_atten_profile_ind(ev_mac, 0xA9));
    if (!wait_for_atten_char_ind_count(sent_messages, atten_char_count_before_duplicate + 1, machine, 1000)) {
        return assert_true(false, test_name, "did not emit fresh CM_ATTEN_CHAR.IND after duplicate restart");
    }

    messages::cm_atten_char_ind atten_char;
    if (!get_last_cm_atten_char_ind(sent_messages, atten_char)) {
        return assert_true(false, test_name, "could not read CM_ATTEN_CHAR.IND after duplicate restart");
    }
    if (!assert_true(atten_char.num_sounds == defs::CM_SLAC_PARM_CNF_NUM_SOUNDS, test_name,
                     "CM_ATTEN_CHAR.IND num_sounds still includes pre-duplicate profile")) {
        return false;
    }

    const auto expected_aag =
        calc_expected_aag(0xA0, defs::CM_SLAC_PARM_CNF_NUM_SOUNDS, ctx.slac_config.sounding_atten_adjustment);
    for (std::size_t i = 0; i < defs::AAG_LIST_LEN; ++i) {
        if (!assert_true(atten_char.attenuation_profile.aag[i] == expected_aag[i], test_name,
                         "CM_ATTEN_CHAR.IND attenuation profile contains pre-duplicate samples")) {
            return false;
        }
    }

    return true;
}

bool test_invalid_cm_slac_parm_req_is_ignored() {
    const char* test_name = "test_invalid_cm_slac_parm_req_is_ignored";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id = fill_run_id(0x61);
    const auto baseline_parm_cnf_count = count_slac_parm_cnf(sent_messages);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, baseline_parm_cnf_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for valid request");
    }
    const auto valid_parm_cnf_count = count_slac_parm_cnf(sent_messages);
    if (!assert_true(ctx.status.session_count == 1, test_name, "valid request did not create session")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id, false));
    if (!assert_parm_cnf_count_stays_at(valid_parm_cnf_count, sent_messages, machine, 80)) {
        return assert_true(false, test_name, "invalid duplicate request emitted CM_SLAC_PARM.CNF");
    }

    if (!assert_true(ctx.status.session_count == 1, test_name, "invalid duplicate request changed session_count")) {
        return false;
    }

    return true;
}

bool test_short_cm_slac_param_req_does_not_create_session() {
    const char* test_name = "test_short_cm_slac_param_req_does_not_create_session";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    auto const baseline_parm_cnf_count = count_slac_parm_cnf(sent_messages);
    auto const baseline_session_count = ctx.status.session_count;

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id = fill_run_id(0x60);

    auto const short_cm_slac_parm_req = create_short_cm_slac_parm_req(ev_mac, run_id);
    if (!assert_true(short_cm_slac_parm_req.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_SLAC_PARAM.REQ is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cm_slac_parm_req.has_payload<std::uint8_t>(), test_name,
                     "short CM_SLAC_PARAM.REQ exposes HomePlug payload bytes")) {
        return false;
    }
    machine.message(short_cm_slac_parm_req);
    auto changed = wait_for(std::chrono::milliseconds(80), machine,
                            [&sent_messages, baseline_parm_cnf_count, &ctx, baseline_session_count]() {
                                return (count_slac_parm_cnf(sent_messages) > baseline_parm_cnf_count) ||
                                       (ctx.status.session_count != baseline_session_count);
                            });
    return assert_true(not changed, test_name, "short CM_SLAC_PARAM.REQ created session or emitted CNF");
}

bool test_different_run_id_creates_new_session() {
    const char* test_name = "test_different_run_id_creates_new_session";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    const auto initial_count = count_slac_parm_cnf(sent_messages);
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id_1 = fill_run_id(0x21);
    auto run_id_2 = fill_run_id(0x31);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_1));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "first request did not create one session")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_2));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 2, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for second request");
    }
    if (!assert_true(ctx.status.session_count == 2, test_name, "different run_id did not create second session")) {
        return false;
    }

    return true;
}

bool test_different_ev_mac_creates_new_session() {
    const char* test_name = "test_different_ev_mac_creates_new_session";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    const auto initial_count = count_slac_parm_cnf(sent_messages);
    auto run_id = fill_run_id(0x41);
    EvMac ev_mac_1 = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    EvMac ev_mac_2 = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};

    machine.message(create_cm_slac_parm_req(ev_mac_1, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "first request did not create one session")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac_2, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 2, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for second request");
    }
    if (!assert_true(ctx.status.session_count == 2, test_name, "different EV MAC did not create second session")) {
        return false;
    }

    return true;
}

bool test_matching_sessions_respect_max_matching_sessions() {
    const char* test_name = "test_matching_sessions_respect_max_matching_sessions";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    std::size_t cm_parm_req_signal_count = 0;
    std::size_t warning_count = 0;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };
    callbacks.signal_ev_mac_address_parm_req = [&cm_parm_req_signal_count](const std::string&) {
        ++cm_parm_req_signal_count;
    };
    callbacks.log_warn = [&warning_count](const std::string&) { ++warning_count; };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.max_matching_sessions = 3;
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    const auto initial_count = count_slac_parm_cnf(sent_messages);
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id_1 = fill_run_id(0x21);
    auto run_id_2 = fill_run_id(0x31);
    auto run_id_3 = fill_run_id(0x41);
    auto run_id_4 = fill_run_id(0x51);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_1));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "first request did not create one session")) {
        return false;
    }
    if (!assert_true(cm_parm_req_signal_count == 1, test_name, "first request did not signal first CM_SLAC_PARM.CNF")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_2));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 2, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for second request");
    }
    if (!assert_true(ctx.status.session_count == 2, test_name, "second run_id did not create second session")) {
        return false;
    }
    if (!assert_true(cm_parm_req_signal_count == 2, test_name,
                     "second request did not signal second CM_SLAC_PARM.CNF")) {
        return false;
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_3));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 3, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for third request");
    }
    if (!assert_true(ctx.status.session_count == 3, test_name, "third run_id did not create third session")) {
        return false;
    }
    if (!assert_true(cm_parm_req_signal_count == 3, test_name, "third request did not signal third CM_SLAC_PARM.CNF")) {
        return false;
    }

    auto maxed_count = count_slac_parm_cnf(sent_messages);
    auto warning_count_before_cap = warning_count;
    machine.message(create_cm_slac_parm_req(ev_mac, run_id_4));
    if (!assert_parm_cnf_count_stays_at(maxed_count, sent_messages, machine, 100)) {
        return assert_true(false, test_name, "additional request beyond cap emitted CM_SLAC_PARM.CNF");
    }
    if (!assert_true(ctx.status.session_count == 3, test_name, "session_count increased beyond configured cap")) {
        return false;
    }
    if (!assert_true(cm_parm_req_signal_count == 3, test_name, "ignored request signaled CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(warning_count == warning_count_before_cap + 1, test_name,
                     "ignored request did not log exactly one warning")) {
        return false;
    }

    auto duplicate_count = count_slac_parm_cnf(sent_messages);
    machine.message(create_cm_slac_parm_req(ev_mac, run_id_1));
    if (!wait_for_parm_cnf_count(sent_messages, duplicate_count + 1, machine, 100)) {
        return assert_true(false, test_name, "existing identity did not receive CM_SLAC_PARM.CNF at cap");
    }
    if (!assert_true(ctx.status.session_count == 3, test_name,
                     "session_count changed after existing identity restart")) {
        return false;
    }
    if (!assert_true(cm_parm_req_signal_count == 4, test_name,
                     "existing identity restart did not signal CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(warning_count == warning_count_before_cap + 1, test_name,
                     "existing identity restart logged an over-cap warning")) {
        return false;
    }

    return true;
}

bool test_invalid_max_matching_sessions_is_clamped() {
    const char* test_name = "test_invalid_max_matching_sessions_is_clamped";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.max_matching_sessions = 0;
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    const auto initial_count = count_slac_parm_cnf(sent_messages);
    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id_1 = fill_run_id(0x61);
    auto run_id_2 = fill_run_id(0x71);

    machine.message(create_cm_slac_parm_req(ev_mac, run_id_1));
    if (!wait_for_parm_cnf_count(sent_messages, initial_count + 1, machine, 100)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "invalid cap did not allow one session")) {
        return false;
    }

    auto capped_count = count_slac_parm_cnf(sent_messages);
    machine.message(create_cm_slac_parm_req(ev_mac, run_id_2));
    if (!assert_parm_cnf_count_stays_at(capped_count, sent_messages, machine, 100)) {
        return assert_true(false, test_name, "invalid cap allowed a second identity");
    }
    if (!assert_true(ctx.status.session_count == 1, test_name, "invalid cap allowed session_count above one")) {
        return false;
    }

    auto duplicate_count = count_slac_parm_cnf(sent_messages);
    machine.message(create_cm_slac_parm_req(ev_mac, run_id_1));
    if (!wait_for_parm_cnf_count(sent_messages, duplicate_count + 1, machine, 100)) {
        return assert_true(false, test_name, "existing identity did not restart with clamped cap");
    }
    return assert_true(ctx.status.session_count == 1, test_name, "existing identity changed clamped session_count");
}

bool test_debug_simulate_failed_matching_uses_wrong_nmk() {
    const char* test_name = "test_debug_simulate_failed_matching_uses_wrong_nmk";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    fill_session_nmk(ctx, 0x11);
    ctx.slac_config.link_status.debug_simulate_failed_matching = true;

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01};
    auto run_id = fill_run_id(0x71);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::Matched, 700)) {
        return false;
    }

    messages::cm_slac_match_cnf match_cnf{};
    if (!get_last_cm_slac_match_cnf(sent_messages, match_cnf)) {
        return assert_true(false, test_name, "did not capture CM_SLAC_MATCH.CNF after match request");
    }

    Nmk observed_nmk{};
    Nmk configured_nmk{};
    std::copy(std::begin(match_cnf.nmk), std::end(match_cnf.nmk), observed_nmk.begin());
    std::copy(std::begin(ctx.slac_config.session_nmk), std::end(ctx.slac_config.session_nmk), configured_nmk.begin());
    auto expected_wrong_nmk = expected_failed_match_nmk();

    return assert_true(not expect_nmk_equal(observed_nmk, configured_nmk), test_name,
                       "CM_SLAC_MATCH.CNF used session NMK despite debug_simulate_failed_matching=true") and
           assert_true(expect_nmk_equal(observed_nmk, expected_wrong_nmk), test_name,
                       "CM_SLAC_MATCH.CNF did not carry debug failure NMK");
}

bool test_matching_uses_session_nmk_when_debug_disabled() {
    const char* test_name = "test_matching_uses_session_nmk_when_debug_disabled";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.link_status.debug_simulate_failed_matching = false;
    fill_session_nmk(ctx, 0x22);

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x02};
    auto run_id = fill_run_id(0x72);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::Matched, 700)) {
        return false;
    }

    messages::cm_slac_match_cnf match_cnf{};
    if (!get_last_cm_slac_match_cnf(sent_messages, match_cnf)) {
        return assert_true(false, test_name, "did not capture CM_SLAC_MATCH.CNF after match request");
    }

    Nmk observed_nmk{};
    Nmk configured_nmk{};
    std::copy(std::begin(match_cnf.nmk), std::end(match_cnf.nmk), observed_nmk.begin());
    std::copy(std::begin(ctx.slac_config.session_nmk), std::end(ctx.slac_config.session_nmk), configured_nmk.begin());

    return assert_true(expect_nmk_equal(observed_nmk, configured_nmk), test_name,
                       "CM_SLAC_MATCH.CNF did not use configured session NMK");
}

bool test_waitforlink_retry_from_same_ev_and_run_id_resends_cnf() {
    const char* test_name = "test_waitforlink_retry_from_same_ev_and_run_id_resends_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };
    callbacks.signal_ev_mac_address_match_cnf = [](const std::string&) {};

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x07};
    auto run_id = fill_run_id(0x80);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    auto const baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto const baseline_match_cnf_to_count = count_cm_slac_match_cnf_to(sent_messages, ev_mac);
    EvMac evse_mac_copy{};
    std::copy(std::begin(ctx.evse_mac), std::end(ctx.evse_mac), evse_mac_copy.begin());
    auto retry = create_cm_slac_match_req(ev_mac, run_id, evse_mac_copy);
    machine.message(retry);

    if (!assert_true(wait_for(std::chrono::milliseconds(120), machine,
                              [&baseline_match_cnf_count, &sent_messages]() {
                                  return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
                              }),
                     test_name, "same EV MAC retry did not resend CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return assert_true(count_cm_slac_match_cnf(sent_messages) == baseline_match_cnf_count + 1, test_name,
                       "same EV MAC retry sent unexpected additional CM_SLAC_MATCH.CNF count") and
           assert_true(count_cm_slac_match_cnf_to(sent_messages, ev_mac) == baseline_match_cnf_to_count + 1, test_name,
                       "resend CM_SLAC_MATCH.CNF was not addressed to the cached EV MAC");
}

bool test_waitforlink_retry_from_different_src_does_not_resend_cnf() {
    const char* test_name = "test_waitforlink_retry_from_different_src_does_not_resend_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };
    std::size_t match_cnf_cb_count = 0;
    callbacks.signal_ev_mac_address_match_cnf = [&match_cnf_cb_count](const std::string&) { ++match_cnf_cb_count; };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x08};
    auto run_id = fill_run_id(0x81);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    auto baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto baseline_cb_count = match_cnf_cb_count;
    auto const bad_source = EvMac{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x99};
    EvMac evse_mac_copy{};
    std::copy(std::begin(ctx.evse_mac), std::end(ctx.evse_mac), evse_mac_copy.begin());
    auto retry_msg = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac_copy);
    machine.message(wrap_cm_slac_match_req(bad_source, retry_msg));

    auto changed = wait_for(std::chrono::milliseconds(120), machine, [&baseline_match_cnf_count, &sent_messages]() {
        return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
    });
    return assert_true(not changed, test_name, "retry from different source MAC resent CM_SLAC_MATCH.CNF") and
           assert_true(match_cnf_cb_count == baseline_cb_count, test_name,
                       "invalid retry from different source triggered CM_SLAC match callback");
}

bool test_waitforlink_retry_with_different_run_id_does_not_resend_cnf() {
    const char* test_name = "test_waitforlink_retry_with_different_run_id_does_not_resend_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x09};
    auto run_id = fill_run_id(0x82);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    auto baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto retry_run_id = fill_run_id(0x83);
    auto retry_msg = create_cm_slac_match_req_payload(ev_mac, retry_run_id, EvMac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01});
    machine.message(wrap_cm_slac_match_req(ev_mac, retry_msg));

    auto changed = wait_for(std::chrono::milliseconds(120), machine, [&baseline_match_cnf_count, &sent_messages]() {
        return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
    });
    return assert_true(not changed, test_name, "retry with different run_id resent CM_SLAC_MATCH.CNF");
}

bool test_waitforlink_retry_with_different_pev_mac_does_not_resend_cnf() {
    const char* test_name = "test_waitforlink_retry_with_different_pev_mac_does_not_resend_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0A};
    auto run_id = fill_run_id(0x84);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    auto baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto retry_msg = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac);
    EvMac altered_pev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0x00, 0x00};
    std::copy(altered_pev_mac.begin(), altered_pev_mac.end(), retry_msg.pev_mac);
    machine.message(wrap_cm_slac_match_req(ev_mac, retry_msg));

    auto changed = wait_for(std::chrono::milliseconds(120), machine, [&baseline_match_cnf_count, &sent_messages]() {
        return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
    });
    return assert_true(not changed, test_name, "retry with different pev_mac resent CM_SLAC_MATCH.CNF");
}

bool test_waitforlink_retry_with_invalid_match_fields_does_not_resend_cnf() {
    const char* test_name = "test_waitforlink_retry_with_invalid_match_fields_does_not_resend_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };
    std::size_t match_cnf_cb_count = 0;
    callbacks.signal_ev_mac_address_match_cnf = [&match_cnf_cb_count](const std::string&) { ++match_cnf_cb_count; };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0C};
    auto run_id = fill_run_id(0x86);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    auto baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto baseline_cb_count = match_cnf_cb_count;

    auto wrong_evse = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac);
    EvMac altered_evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x7F};
    std::copy(altered_evse_mac.begin(), altered_evse_mac.end(), wrong_evse.evse_mac);
    machine.message(wrap_cm_slac_match_req(ev_mac, wrong_evse));

    auto wrong_application = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac);
    wrong_application.application_type = defs::COMMON_APPLICATION_TYPE + 1;
    machine.message(wrap_cm_slac_match_req(ev_mac, wrong_application));

    auto wrong_security = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac);
    wrong_security.security_type = defs::COMMON_SECURITY_TYPE + 1;
    machine.message(wrap_cm_slac_match_req(ev_mac, wrong_security));

    auto wrong_mvf = create_cm_slac_match_req_payload(ev_mac, run_id, evse_mac);
    wrong_mvf.mvf_length = defs::CM_SLAC_MATCH_REQ_MVF_LENGTH + 1;
    machine.message(wrap_cm_slac_match_req(ev_mac, wrong_mvf));

    auto changed = wait_for(std::chrono::milliseconds(120), machine, [&baseline_match_cnf_count, &sent_messages]() {
        return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
    });
    return assert_true(not changed, test_name, "retry with invalid match fields resent CM_SLAC_MATCH.CNF") and
           assert_true(match_cnf_cb_count == baseline_cb_count, test_name,
                       "retry with invalid match fields triggered CM_SLAC match callback");
}

bool test_waitforlink_retry_after_reset_does_not_emit_cached_match_cnf() {
    const char* test_name = "test_waitforlink_retry_after_reset_does_not_emit_cached_match_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x0B};
    auto run_id = fill_run_id(0x85);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }
    auto const baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);

    machine.reset();
    if (!wait_for_match_state(ctx, SlacState::Reset, machine, 100)) {
        return assert_true(false, test_name, "did not enter Reset on reset() from WaitForLink");
    }
    if (!assert_true(not ctx.match_confirm_cache.valid, test_name, "Reset retained cached CM_SLAC_MATCH.CNF")) {
        return false;
    }
    machine.message(create_cm_set_key_cnf(defs::CM_SET_KEY_CNF_RESULT_MODEM_COMPAT_SUCCESS));
    if (!wait_for_match_state(ctx, SlacState::Idle, machine, 120)) {
        return assert_true(false, test_name, "did not return to Idle after CM_SET_KEY.CNF");
    }
    if (!assert_true(not ctx.match_confirm_cache.valid, test_name, "Idle retained cached CM_SLAC_MATCH.CNF")) {
        return false;
    }

    auto retry = create_cm_slac_match_req(ev_mac, run_id, EvMac{0x02, 0x00, 0x00, 0x00, 0x00, 0x01});
    machine.message(retry);
    auto changed = wait_for(std::chrono::milliseconds(120), machine, [&baseline_match_cnf_count, &sent_messages]() {
        return count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
    });
    return assert_true(not changed, test_name, "retry after reset emitted stale cached CM_SLAC_MATCH.CNF");
}

bool test_short_cm_slac_match_req_does_not_emit_match_cnf() {
    const char* test_name = "test_short_cm_slac_match_req_does_not_emit_match_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    configure_wait_for_link(ctx);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x07};
    auto run_id = fill_run_id(0x73);
    EvMac evse_mac_copy{};
    std::copy(std::begin(ctx.evse_mac), std::end(ctx.evse_mac), evse_mac_copy.begin());
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }
    if (!assert_true(ctx.status.match_state == SlacState::WaitForLink, test_name, "did not enter WaitForLink state")) {
        return false;
    }

    auto baseline_match_cnf_count = count_cm_slac_match_cnf(sent_messages);
    auto const short_cm_slac_match_req = create_short_cm_slac_match_req(ev_mac, run_id, evse_mac_copy);
    if (!assert_true(short_cm_slac_match_req.frame_size() == messages::HOMEPLUG_PAYLOAD_OFFSET, test_name,
                     "short CM_SLAC_MATCH.REQ length is not header-only")) {
        return false;
    }
    if (!assert_true(not short_cm_slac_match_req.has_payload<std::uint8_t>(), test_name,
                     "short CM_SLAC_MATCH.REQ exposes HomePlug payload bytes")) {
        return false;
    }
    machine.message(short_cm_slac_match_req);
    auto changed =
        wait_for(std::chrono::milliseconds(120), machine, [&ctx, &sent_messages, baseline_match_cnf_count]() {
            return ctx.status.match_state != SlacState::WaitForLink ||
                   count_cm_slac_match_cnf(sent_messages) > baseline_match_cnf_count;
        });
    return assert_true(not changed, test_name, "short CM_SLAC_MATCH.REQ emitted CM_SLAC_MATCH.CNF or changed state");
}

bool test_short_link_status_cnf_does_not_leave_matched_or_failed() {
    const char* test_name = "test_short_link_status_cnf_does_not_leave_matched_or_failed";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.modem_vendor = defs::ModemVendor::Lumissil;
    ctx.slac_config.link_status.do_detect = true;
    ctx.slac_config.link_status.retry_ms = 20;
    ctx.slac_config.link_status.poll_in_matched_state_ms = 120;
    ctx.slac_config.link_status.timeout_ms = 350;
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x08};
    auto run_id = fill_run_id(0x74);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    EvMac modem_source = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    machine.message(create_lumissil_link_status_cnf(modem_source, defs::D_LINK_STATUS_LINKED));
    if (!wait_for_match_state(ctx, SlacState::Matched, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Matched on positive link-status CNF");
    }
    if (!assert_true(not ctx.match_confirm_cache.valid, test_name, "Matched retained cached CM_SLAC_MATCH.CNF")) {
        return false;
    }

    auto const short_link_status_cnf = create_short_lumissil_link_status_cnf(modem_source, 0x00);
    if (!assert_true(short_link_status_cnf.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short link-status CNF is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_link_status_cnf.has_payload<std::uint8_t>(), test_name,
                     "short link-status CNF exposes HomePlug payload bytes")) {
        return false;
    }
    machine.message(short_link_status_cnf);
    auto failed = wait_for(std::chrono::milliseconds(120), machine,
                           [&ctx]() { return ctx.status.match_state == SlacState::Failed; });
    if (!assert_true(not failed, test_name, "short link-status CNF changed matched/failed state")) {
        return false;
    }

    machine.message(create_lumissil_link_status_cnf(modem_source, 0x00));
    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Failed on negative full-length link-status CNF");
    }

    return true;
}

bool test_reset_instead_of_fail_waits_for_new_parm_request() {
    const char* test_name = "test_reset_instead_of_fail_waits_for_new_parm_request";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.reset_instead_of_fail = true;
    fill_session_nmk(ctx, 0x33);

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x03};
    auto run_id = fill_run_id(0x73);

    const auto initial_parm_count = count_slac_parm_cnf(sent_messages);
    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_parm_count + 1, machine, 150)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for first request");
    }
    const auto first_failure_reset = wait_for(std::chrono::milliseconds(1700), machine, [&ctx]() {
        return ctx.status.match_state == SlacState::Matching && ctx.status.session_count == 0;
    });
    if (!first_failure_reset) {
        return assert_true(false, test_name,
                           "first matching failure did not reset and return to Matching without failing");
    }

    machine.message(create_cm_slac_parm_req(ev_mac, run_id));
    if (!wait_for_parm_cnf_count(sent_messages, initial_parm_count + 2, machine, 200)) {
        return assert_true(false, test_name, "did not emit CM_SLAC_PARM.CNF for second request");
    }
    if (!assert_true(ctx.status.match_state == SlacState::Matching, test_name,
                     "reset_instead_of_fail should keep Matching on first failure")) {
        return false;
    }
    if (!assert_true(ctx.status.session_count == 1, test_name,
                     "second parm request did not start a new matching session")) {
        return false;
    }
    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 1900)) {
        return assert_true(false, test_name, "second matching failure did not transition to Failed");
    }

    return true;
}

bool test_cm_validate_req_returns_failure_cnf() {
    const char* test_name = "test_cm_validate_req_returns_failure_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x04};
    const auto initial_validate_cnf = count_cm_validate_cnf(sent_messages);
    const auto expected_validate_cnf_count = initial_validate_cnf + 1;
    machine.message(create_cm_validate_req(ev_mac));

    if (!wait_for(std::chrono::milliseconds(200), machine, [&sent_messages, expected_validate_cnf_count]() {
            return count_cm_validate_cnf(sent_messages) == expected_validate_cnf_count;
        })) {
        return assert_true(false, test_name, "did not emit CM_VALIDATE.CNF");
    }
    if (!assert_validate_cnf_count_stays_at(expected_validate_cnf_count, sent_messages, machine, 80)) {
        return assert_true(false, test_name, "CM_VALIDATE.CNF was emitted more than once");
    }

    messages::cm_validate_cnf validate_cnf{};
    if (!get_last_cm_validate_cnf(sent_messages, validate_cnf)) {
        return assert_true(false, test_name, "could not parse CM_VALIDATE.CNF");
    }
    return assert_true(validate_cnf.signal_type == defs::CM_VALIDATE_REQ_SIGNAL_TYPE, test_name,
                       "CM_VALIDATE.CNF has unexpected signal_type") and
           assert_true(validate_cnf.toggle_num == 0, test_name, "CM_VALIDATE.CNF has unexpected toggle_num") and
           assert_true(validate_cnf.result == defs::CM_VALIDATE_REQ_RESULT_FAILURE, test_name,
                       "CM_VALIDATE.CNF has non-failure result");
}

bool test_no_cm_slac_parm_timeout_resets_then_fails() {
    const char* test_name = "test_no_cm_slac_parm_timeout_resets_then_fails";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.reset_instead_of_fail = true;
    ctx.slac_config.slac_init_timeout_ms = 120;

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    auto const timeout_ms = ctx.slac_config.slac_init_timeout_ms;
    auto const first_timeout_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms + 30);
    while (std::chrono::steady_clock::now() < first_timeout_deadline) {
        machine.update();
        if (ctx.status.match_state == SlacState::Failed) {
            return assert_true(false, test_name, "state transitioned to Failed before first matching timeout reset");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!assert_true(ctx.status.match_state == SlacState::Matching, test_name,
                     "first matching timeout did not remain in Matching after reset")) {
        return false;
    }
    if (!assert_true(ctx.status.session_count == 0, test_name, "session_count should remain zero after reset")) {
        return false;
    }
    if (!wait_for_match_state(ctx, SlacState::Failed, machine, static_cast<int>(timeout_ms * 2 + 60))) {
        return assert_true(false, test_name, "second matching timeout did not transition to Failed");
    }

    return true;
}

bool test_no_cm_slac_parm_timeout_fails_when_reset_disabled() {
    const char* test_name = "test_no_cm_slac_parm_timeout_fails_when_reset_disabled";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.reset_instead_of_fail = false;
    ctx.slac_config.slac_init_timeout_ms = 80;

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 200)) {
        return assert_true(false, test_name, "matching timeout did not transition to Failed when reset disabled");
    }

    return true;
}

bool test_matched_link_status_rejects_only_negative_cnf() {
    const char* test_name = "test_matched_link_status_rejects_only_negative_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.link_status.do_detect = true;
    ctx.slac_config.link_status.retry_ms = 20;
    ctx.slac_config.link_status.poll_in_matched_state_ms = 120;
    ctx.slac_config.link_status.timeout_ms = 300;
    ctx.modem_vendor = defs::ModemVendor::Lumissil;

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x05};
    auto run_id = fill_run_id(0x74);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    EvMac modem_source = {0x00, 0x00, 0x00, 0x00, 0x00, 0x02};
    machine.message(create_lumissil_link_status_cnf(modem_source, defs::D_LINK_STATUS_LINKED));
    if (!wait_for_match_state(ctx, SlacState::Matched, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Matched on positive link-status CNF");
    }

    machine.message(create_lumissil_link_status_cnf(modem_source, defs::D_LINK_STATUS_LINKED));
    if (!assert_true(not wait_for(std::chrono::milliseconds(60), machine,
                                  [&ctx]() { return ctx.status.match_state != SlacState::Matched; }),
                     test_name, "positive link-status CNF changed state")) {
        return false;
    }

    machine.message(create_cm_validate_req(ev_mac));
    if (!assert_true(not wait_for(std::chrono::milliseconds(60), machine,
                                  [&ctx]() { return ctx.status.match_state != SlacState::Matched; }),
                     test_name, "unrelated message changed state in Matched")) {
        return false;
    }

    machine.message(create_lumissil_link_status_cnf(modem_source, 0x00));
    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Failed on negative link-status CNF");
    }

    return assert_true(not ctx.match_confirm_cache.valid, test_name, "Failed retained cached CM_SLAC_MATCH.CNF");
}

bool test_matched_qualcomm_link_status_rejects_only_negative_cnf() {
    const char* test_name = "test_matched_qualcomm_link_status_rejects_only_negative_cnf";
    ContextCallbacks callbacks{};
    std::vector<SentMessage> sent_messages;
    callbacks.send_raw_slac = [&sent_messages](messages::HomeplugMessage& hp_message) {
        sent_messages.push_back({sent_messages.size(), hp_message});
        return true;
    };

    Context ctx(callbacks);
    configure_common(ctx);
    ctx.slac_config.link_status.do_detect = true;
    ctx.slac_config.link_status.retry_ms = 20;
    ctx.slac_config.link_status.poll_in_matched_state_ms = 120;
    ctx.slac_config.link_status.timeout_ms = 300;
    ctx.modem_vendor = defs::ModemVendor::Qualcomm;

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    std::copy(evse_mac.begin(), evse_mac.end(), std::begin(ctx.evse_mac));

    slac_fsm machine(ctx);
    machine.restart_fsm();
    if (!enter_matching_state(ctx, machine)) {
        return false;
    }

    EvMac ev_mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x06};
    auto run_id = fill_run_id(0x75);
    if (!perform_full_match_sequence(ctx, sent_messages, machine, ev_mac, run_id, SlacState::WaitForLink, 700)) {
        return false;
    }

    EvMac modem_source = {0x00, 0xB0, 0x52, 0x00, 0x00, 0x02};
    machine.message(create_qualcomm_link_status_cnf(modem_source, defs::D_LINK_STATUS_LINKED));
    if (!wait_for_match_state(ctx, SlacState::Matched, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Matched on positive link-status CNF");
    }

    machine.message(create_cm_validate_req(ev_mac));
    if (!assert_true(not wait_for(std::chrono::milliseconds(60), machine,
                                  [&ctx]() { return ctx.status.match_state != SlacState::Matched; }),
                     test_name, "unrelated message changed state in Matched")) {
        return false;
    }

    machine.message(create_qualcomm_link_status_cnf(modem_source, 0x00));
    if (!wait_for_match_state(ctx, SlacState::Failed, machine, 200)) {
        return assert_true(false, test_name, "did not transition to Failed on negative link-status CNF");
    }

    return true;
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 24>{
        std::make_pair("test_duplicate_cm_slac_parm_req_restarts_same_session",
                       test_duplicate_cm_slac_parm_req_restarts_same_session),
        std::make_pair("test_duplicate_cm_slac_parm_req_restarts_inflight_session",
                       test_duplicate_cm_slac_parm_req_restarts_inflight_session),
        std::make_pair("test_invalid_cm_slac_parm_req_is_ignored", test_invalid_cm_slac_parm_req_is_ignored),
        std::make_pair("test_short_cm_slac_param_req_does_not_create_session",
                       test_short_cm_slac_param_req_does_not_create_session),
        std::make_pair("test_different_run_id_creates_new_session", test_different_run_id_creates_new_session),
        std::make_pair("test_different_ev_mac_creates_new_session", test_different_ev_mac_creates_new_session),
        std::make_pair("test_matching_sessions_respect_max_matching_sessions",
                       test_matching_sessions_respect_max_matching_sessions),
        std::make_pair("test_invalid_max_matching_sessions_is_clamped", test_invalid_max_matching_sessions_is_clamped),
        std::make_pair("test_debug_simulate_failed_matching_uses_wrong_nmk",
                       test_debug_simulate_failed_matching_uses_wrong_nmk),
        std::make_pair("test_matching_uses_session_nmk_when_debug_disabled",
                       test_matching_uses_session_nmk_when_debug_disabled),
        std::make_pair("test_waitforlink_retry_from_same_ev_and_run_id_resends_cnf",
                       test_waitforlink_retry_from_same_ev_and_run_id_resends_cnf),
        std::make_pair("test_waitforlink_retry_from_different_src_does_not_resend_cnf",
                       test_waitforlink_retry_from_different_src_does_not_resend_cnf),
        std::make_pair("test_waitforlink_retry_with_different_pev_mac_does_not_resend_cnf",
                       test_waitforlink_retry_with_different_pev_mac_does_not_resend_cnf),
        std::make_pair("test_waitforlink_retry_with_different_run_id_does_not_resend_cnf",
                       test_waitforlink_retry_with_different_run_id_does_not_resend_cnf),
        std::make_pair("test_waitforlink_retry_with_invalid_match_fields_does_not_resend_cnf",
                       test_waitforlink_retry_with_invalid_match_fields_does_not_resend_cnf),
        std::make_pair("test_waitforlink_retry_after_reset_does_not_emit_cached_match_cnf",
                       test_waitforlink_retry_after_reset_does_not_emit_cached_match_cnf),
        std::make_pair("test_short_cm_slac_match_req_does_not_emit_match_cnf",
                       test_short_cm_slac_match_req_does_not_emit_match_cnf),
        std::make_pair("test_short_link_status_cnf_does_not_leave_matched_or_failed",
                       test_short_link_status_cnf_does_not_leave_matched_or_failed),
        std::make_pair("test_reset_instead_of_fail_waits_for_new_parm_request",
                       test_reset_instead_of_fail_waits_for_new_parm_request),
        std::make_pair("test_no_cm_slac_parm_timeout_resets_then_fails",
                       test_no_cm_slac_parm_timeout_resets_then_fails),
        std::make_pair("test_no_cm_slac_parm_timeout_fails_when_reset_disabled",
                       test_no_cm_slac_parm_timeout_fails_when_reset_disabled),
        std::make_pair("test_cm_validate_req_returns_failure_cnf", test_cm_validate_req_returns_failure_cnf),
        std::make_pair("test_matched_link_status_rejects_only_negative_cnf",
                       test_matched_link_status_rejects_only_negative_cnf),
        std::make_pair("test_matched_qualcomm_link_status_rejects_only_negative_cnf",
                       test_matched_qualcomm_link_status_rejects_only_negative_cnf),
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
        return EXIT_FAILURE;
    }

    std::printf("PASSED\n");
    return EXIT_SUCCESS;
}
