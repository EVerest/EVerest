// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <net/ethernet.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/ev_slac_fsm.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_utils.hpp>

using namespace everest::lib::slac;
using namespace everest::lib::slac::fsm::ev;

namespace {

using EvMac = messages::HomeplugMessage::MacAddress;
using RunId = std::array<std::uint8_t, defs::RUN_ID_LEN>;
using Nmk = std::array<std::uint8_t, defs::NMK_LEN>;

struct TestHarness {
    std::vector<messages::HomeplugMessage> sent_messages;
    std::vector<std::string> state_messages;
    std::vector<bool> dlink_ready_history;
    std::vector<std::string> warning_messages;
    bool saw_matching_state = false;
    bool saw_matched_state = false;
    bool saw_dlink_false = false;
    bool saw_dlink_true = false;

    ContextCallbacks callbacks{};
    Context ctx;
    ev_slac_fsm machine;

    explicit TestHarness() : ctx(callbacks, Context::EV_PLC_MAC), machine(ctx) {
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& hp_message) {
            sent_messages.push_back(hp_message);
            return true;
        };
        callbacks.signal_state = [this](const std::string& state) {
            state_messages.push_back(state);
            if (state == "MATCHING") {
                saw_matching_state = true;
            } else if (state == "MATCHED") {
                saw_matched_state = true;
            }
        };
        callbacks.signal_dlink_ready = [this](bool dlink_ready) {
            dlink_ready_history.push_back(dlink_ready);
            if (not dlink_ready) {
                saw_dlink_false = true;
            } else {
                saw_dlink_true = true;
            }
        };
        callbacks.log_warn = [this](const std::string& message) { warning_messages.push_back(message); };
    }
};

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool has_state(std::vector<std::string> const& state_messages, const char* state) {
    return std::any_of(state_messages.begin(), state_messages.end(),
                       [state](std::string const& candidate) { return candidate == state; });
}

std::size_t count_state(std::vector<std::string> const& state_messages, const char* state) {
    return std::count_if(state_messages.begin(), state_messages.end(),
                         [state](std::string const& candidate) { return candidate == state; });
}

std::size_t count_dlink_ready(std::vector<bool> const& dlink_ready_history, bool expected) {
    return std::count(dlink_ready_history.begin(), dlink_ready_history.end(), expected);
}

bool has_warning(std::vector<std::string> const& warning_messages, const char* text) {
    return std::any_of(warning_messages.begin(), warning_messages.end(),
                       [text](std::string const& candidate) { return candidate == text; });
}

bool dlink_last_ready(std::vector<bool> const& dlink_ready_history, bool expected) {
    return not dlink_ready_history.empty() && dlink_ready_history.back() == expected;
}

bool is_cm_slac_parm_req(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ);
}

bool is_cm_start_atten_char_ind(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND);
}

bool is_cm_mnbc_sound_ind(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_MNBC_SOUND | defs::MMTYPE_MODE_IND);
}

bool is_cm_atten_char_rsp(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_RSP);
}

bool is_cm_slac_match_req(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ);
}

bool is_cm_set_key_cnf(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF);
}

bool is_broadcast_destination(messages::HomeplugMessage const& msg) {
    auto const* raw = msg.get_raw_message_ptr();
    return std::equal(std::begin(raw->ethernet_header.ether_dhost), std::end(raw->ethernet_header.ether_dhost),
                      Context::BROADCAST_MAC.begin());
}

bool is_destination(const messages::HomeplugMessage& msg, EvMac const& destination_mac) {
    auto const* raw = msg.get_raw_message_ptr();
    return std::equal(std::begin(raw->ethernet_header.ether_dhost), std::end(raw->ethernet_header.ether_dhost),
                      destination_mac.begin());
}

std::size_t count_cm_start_atten_char_ind(std::vector<messages::HomeplugMessage> const& sent_messages) {
    return std::count_if(sent_messages.begin(), sent_messages.end(), is_cm_start_atten_char_ind);
}

std::size_t count_cm_mnbc_sound_ind(std::vector<messages::HomeplugMessage> const& sent_messages) {
    return std::count_if(sent_messages.begin(), sent_messages.end(), is_cm_mnbc_sound_ind);
}

std::size_t count_cm_atten_char_rsp(std::vector<messages::HomeplugMessage> const& sent_messages) {
    return std::count_if(sent_messages.begin(), sent_messages.end(), is_cm_atten_char_rsp);
}

std::size_t count_cm_slac_match_req(std::vector<messages::HomeplugMessage> const& sent_messages) {
    return std::count_if(sent_messages.begin(), sent_messages.end(), is_cm_slac_match_req);
}

bool is_cm_set_key_req(messages::HomeplugMessage const& msg) {
    return msg.get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ);
}

std::size_t count_cm_set_key_req(std::vector<messages::HomeplugMessage> const& sent_messages) {
    return std::count_if(sent_messages.begin(), sent_messages.end(), is_cm_set_key_req);
}

bool get_last_cm_set_key_req(std::vector<messages::HomeplugMessage> const& sent_messages,
                             messages::cm_set_key_req& set_key_req) {
    for (auto it = sent_messages.rbegin(); it != sent_messages.rend(); ++it) {
        if (not is_cm_set_key_req(*it)) {
            continue;
        }
        auto const payload = it->payload_as<messages::cm_set_key_req>();
        if (not payload.has_value()) {
            return false;
        }
        set_key_req = payload.value();
        return true;
    }
    return false;
}

bool get_last_cm_start_atten_char_ind(std::vector<messages::HomeplugMessage> const& sent_messages,
                                      messages::cm_start_atten_char_ind& atten_char) {
    for (auto it = sent_messages.rbegin(); it != sent_messages.rend(); ++it) {
        if (not is_cm_start_atten_char_ind(*it)) {
            continue;
        }
        auto const payload = it->payload_as<messages::cm_start_atten_char_ind>();
        if (not payload.has_value()) {
            return false;
        }
        atten_char = payload.value();
        return true;
    }
    return false;
}

messages::HomeplugMessage create_cm_atten_char_ind(EvMac const& source_mac, EvMac const& ev_host_mac,
                                                   RunId const& run_id) {
    messages::cm_atten_char_ind atten_char{};
    atten_char.application_type = defs::COMMON_APPLICATION_TYPE;
    atten_char.security_type = defs::COMMON_SECURITY_TYPE;
    atten_char.num_sounds = defs::C_EV_MATCH_MNBC;
    std::copy(ev_host_mac.begin(), ev_host_mac.end(), atten_char.source_address);
    std::copy(run_id.begin(), run_id.end(), atten_char.run_id);
    std::fill(std::begin(atten_char.source_id), std::end(atten_char.source_id), std::uint8_t{0});
    std::fill(std::begin(atten_char.resp_id), std::end(atten_char.resp_id), std::uint8_t{0});
    atten_char.attenuation_profile.num_groups = defs::AAG_LIST_LEN;
    std::fill(std::begin(atten_char.attenuation_profile.aag), std::end(atten_char.attenuation_profile.aag),
              std::uint8_t{0});

    messages::HomeplugMessage message;
    message.set_source(source_mac);
    message.setup_payload(&atten_char, sizeof(atten_char), defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND,
                          defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_short_cm_atten_char_ind(EvMac const& source_mac, EvMac const& ev_host_mac,
                                                         RunId const& run_id) {
    auto message = create_cm_atten_char_ind(source_mac, ev_host_mac, run_id);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET);
    return message;
}

bool collect_cm_start_atten_char_ind(std::vector<messages::HomeplugMessage> const& sent_messages,
                                     std::vector<messages::cm_start_atten_char_ind>& atten_chars) {
    atten_chars.clear();
    for (auto const& message : sent_messages) {
        if (not is_cm_start_atten_char_ind(message)) {
            continue;
        }
        auto const payload = message.payload_as<messages::cm_start_atten_char_ind>();
        if (not payload.has_value()) {
            return false;
        }
        atten_chars.push_back(payload.value());
    }
    return true;
}

bool collect_cm_mnbc_sound_ind(std::vector<messages::HomeplugMessage> const& sent_messages,
                               std::vector<messages::cm_mnbc_sound_ind>& mnbc_sounds) {
    mnbc_sounds.clear();
    for (auto const& message : sent_messages) {
        if (not is_cm_mnbc_sound_ind(message)) {
            continue;
        }
        auto const payload = message.payload_as<messages::cm_mnbc_sound_ind>();
        if (not payload.has_value()) {
            return false;
        }
        mnbc_sounds.push_back(payload.value());
    }
    return true;
}

bool parse_run_id_from_parm_req(messages::HomeplugMessage const& request, RunId& run_id) {
    auto const payload = request.payload_as<messages::cm_slac_parm_req>();
    if (not payload.has_value()) {
        return false;
    }
    std::copy(std::begin(payload->run_id), std::end(payload->run_id), run_id.begin());
    return true;
}

messages::HomeplugMessage create_cm_slac_parm_cnf(EvMac const& evse_mac, RunId const& run_id) {
    messages::cm_slac_parm_cnf payload{};
    std::fill(std::begin(payload.m_sound_target), std::end(payload.m_sound_target), std::uint8_t{0xFF});
    payload.num_sounds = defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    payload.timeout = defs::CM_SLAC_PARM_CNF_TIMEOUT;
    payload.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
    std::copy(evse_mac.begin(), evse_mac.end(), payload.forwarding_sta);
    payload.application_type = defs::COMMON_APPLICATION_TYPE;
    payload.security_type = defs::COMMON_SECURITY_TYPE;
    std::copy(run_id.begin(), run_id.end(), payload.run_id);

    messages::HomeplugMessage message;
    message.set_source(evse_mac);
    message.setup_payload(&payload, sizeof(payload), defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_short_cm_slac_parm_cnf(EvMac const& evse_mac, RunId const& run_id) {
    auto message = create_cm_slac_parm_cnf(evse_mac, run_id);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::homeplug_fragmentation_part) +
                                 sizeof(messages::cm_slac_parm_cnf));
    return message;
}

messages::HomeplugMessage create_cm_slac_match_cnf(EvMac const& source_mac, EvMac const& evse_mac,
                                                   EvMac const& ev_host_mac, RunId const& run_id, Nmk const& nmk) {
    messages::cm_slac_match_cnf match_cnf{};
    match_cnf.application_type = defs::COMMON_APPLICATION_TYPE;
    match_cnf.security_type = defs::COMMON_SECURITY_TYPE;
    match_cnf.mvf_length = defs::CM_SLAC_MATCH_CNF_MVF_LENGTH;
    std::fill(std::begin(match_cnf.pev_id), std::end(match_cnf.pev_id), std::uint8_t{0});
    std::copy(ev_host_mac.begin(), ev_host_mac.end(), match_cnf.pev_mac);
    std::fill(std::begin(match_cnf.evse_id), std::end(match_cnf.evse_id), std::uint8_t{0});
    std::copy(evse_mac.begin(), evse_mac.end(), match_cnf.evse_mac);
    std::copy(run_id.begin(), run_id.end(), match_cnf.run_id);
    std::fill(std::begin(match_cnf._rerserved), std::end(match_cnf._rerserved), std::uint8_t{0});
    match_cnf._reserved2 = 0;
    utils::generate_nid_from_nmk(match_cnf.nid, nmk.data());
    std::copy(nmk.begin(), nmk.end(), match_cnf.nmk);

    messages::HomeplugMessage message;
    message.set_source(source_mac);
    message.setup_payload(&match_cnf, sizeof(match_cnf), defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_short_cm_slac_match_cnf(EvMac const& source_mac, EvMac const& ev_host_mac,
                                                         RunId const& run_id, Nmk const& nmk) {
    auto message = create_cm_slac_match_cnf(source_mac, source_mac, ev_host_mac, run_id, nmk);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::homeplug_fragmentation_part));
    return message;
}

struct SetKeyCnfParams {
    EvMac source{Context::EV_PLC_MAC};
    std::uint8_t result = defs::CM_SET_KEY_CNF_RESULT_SUCCESS;
};

messages::HomeplugMessage create_cm_set_key_cnf(SetKeyCnfParams const& params) {
    messages::cm_set_key_cnf set_key_cnf{};
    set_key_cnf.result = params.result;

    messages::HomeplugMessage message;
    message.set_source(params.source);
    message.setup_payload(&set_key_cnf, sizeof(set_key_cnf), defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF,
                          defs::MMV::AV_1_1);
    return message;
}

messages::HomeplugMessage create_short_cm_set_key_cnf(SetKeyCnfParams const& params) {
    auto message = create_cm_set_key_cnf(params);
    message.mark_received_length(messages::HOMEPLUG_PAYLOAD_OFFSET + sizeof(messages::homeplug_fragmentation_part) +
                                 sizeof(messages::cm_set_key_cnf));
    return message;
}

bool init_unmatched(TestHarness& harness, char const* test_name) {
    harness.machine.restart_fsm();
    harness.machine.update();

    if (!assert_true(has_state(harness.state_messages, "UNMATCHED"), test_name,
                     "did not publish UNMATCHED after restart/update")) {
        return false;
    }
    if (!assert_true(harness.saw_dlink_false, test_name, "did not publish dlink_ready=false after restart/update")) {
        return false;
    }

    return true;
}

bool run_matching_with_full_sounding_sequence(TestHarness& harness, const char* test_name, EvMac const& evse_mac,
                                              RunId& run_id) {
    harness.machine.trigger_matching();
    if (!assert_true(not harness.sent_messages.empty(), test_name, "no CM_SLAC_PARM.REQ emitted")) {
        return false;
    }

    if (!assert_true(parse_run_id_from_parm_req(harness.sent_messages.front(), run_id), test_name,
                     "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }

    auto const start_atten_count_before = count_cm_start_atten_char_ind(harness.sent_messages);
    auto const sound_count_before = count_cm_mnbc_sound_ind(harness.sent_messages);
    harness.machine.message(create_cm_slac_parm_cnf(evse_mac, run_id));
    harness.machine.update();

    auto const expected_start_count = start_atten_count_before + defs::C_EV_START_ATTEN_CHAR_INDS;
    auto const expected_sound_count = sound_count_before + defs::C_EV_MATCH_MNBC;

    for (std::size_t i = 0; i < defs::C_EV_START_ATTEN_CHAR_INDS + defs::C_EV_MATCH_MNBC + 1; ++i) {
        if (count_cm_start_atten_char_ind(harness.sent_messages) == expected_start_count &&
            count_cm_mnbc_sound_ind(harness.sent_messages) == expected_sound_count) {
            break;
        }
        harness.machine.update();
    }

    if (!assert_true(harness.saw_matching_state, test_name, "did not publish MATCHING after valid CM_SLAC_PARM.CNF")) {
        return false;
    }

    if (!assert_true(count_cm_start_atten_char_ind(harness.sent_messages) == expected_start_count, test_name,
                     "did not emit exactly 3 CM_START_ATTEN_CHAR.IND after valid CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(count_cm_mnbc_sound_ind(harness.sent_messages) == expected_sound_count, test_name,
                     "did not emit exactly 10 CM_MNBC_SOUND.IND after valid CM_SLAC_PARM.CNF")) {
        return false;
    }

    return true;
}

bool run_matching_to_wait_for_match_cnf(TestHarness& harness, const char* test_name, EvMac const& evse_mac,
                                        RunId& run_id) {
    if (not run_matching_with_full_sounding_sequence(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const rsp_count_before = count_cm_atten_char_rsp(harness.sent_messages);
    auto const match_req_count_before = count_cm_slac_match_req(harness.sent_messages);
    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);

    harness.machine.message(create_cm_atten_char_ind(evse_mac, harness.ctx.ev_host_mac, run_id));

    auto const expected_set_key_count = set_key_count_before;
    for (std::size_t i = 0; i < 2; ++i) {
        if ((rsp_count_before + 1 == count_cm_atten_char_rsp(harness.sent_messages)) &&
            (match_req_count_before + 1 == count_cm_slac_match_req(harness.sent_messages))) {
            break;
        }
        harness.machine.update();
    }

    if (!assert_true(rsp_count_before + 1 == count_cm_atten_char_rsp(harness.sent_messages), test_name,
                     "did not emit CM_ATTEN_CHAR.RSP after valid CM_ATTEN_CHAR.IND for setup")) {
        return false;
    }
    if (!assert_true(match_req_count_before + 1 == count_cm_slac_match_req(harness.sent_messages), test_name,
                     "did not emit CM_SLAC_MATCH.REQ after valid CM_ATTEN_CHAR.IND for setup")) {
        return false;
    }
    if (!assert_true(expected_set_key_count == count_cm_set_key_req(harness.sent_messages), test_name,
                     "received CM_SET_KEY.REQ before CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return true;
}

bool run_matching_to_wait_for_set_key_cnf(TestHarness& harness, const char* test_name, EvMac const& evse_mac,
                                          RunId& run_id, Nmk const& nmk) {
    if (not run_matching_to_wait_for_match_cnf(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);
    harness.machine.message(create_cm_slac_match_cnf(evse_mac, evse_mac, harness.ctx.ev_host_mac, run_id, nmk));

    if (!assert_true(set_key_count_before + 1 == count_cm_set_key_req(harness.sent_messages), test_name,
                     "did not emit CM_SET_KEY.REQ after valid CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return true;
}

bool test_trigger_matching_emits_single_parm_request() {
    const char* test_name = "test_trigger_matching_emits_single_parm_request";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    harness.machine.trigger_matching();

    if (!assert_true(harness.sent_messages.size() == 1, test_name,
                     "trigger_matching did not send exactly one CM_SLAC_PARM.REQ")) {
        return false;
    }
    if (!assert_true(not harness.saw_matching_state, test_name, "EV FSM entered MATCHING during trigger_matching")) {
        return false;
    }

    auto const& request = harness.sent_messages.front();
    if (!assert_true(request.is_valid(), test_name, "CM_SLAC_PARM.REQ is not valid")) {
        return false;
    }
    if (!assert_true(is_cm_slac_parm_req(request), test_name, "trigger_matching did not send CM_SLAC_PARM.REQ")) {
        return false;
    }
    if (!assert_true(is_broadcast_destination(request), test_name, "CM_SLAC_PARM.REQ destination is not broadcast")) {
        return false;
    }

    auto cm_slac_parm_req = request.payload_as<messages::cm_slac_parm_req>();
    if (!assert_true(cm_slac_parm_req.has_value(), test_name, "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }
    if (!assert_true(cm_slac_parm_req->application_type == defs::COMMON_APPLICATION_TYPE, test_name,
                     "CM_SLAC_PARM.REQ application_type is not COMMON_APPLICATION_TYPE")) {
        return false;
    }
    if (!assert_true(cm_slac_parm_req->security_type == defs::COMMON_SECURITY_TYPE, test_name,
                     "CM_SLAC_PARM.REQ security_type is not COMMON_SECURITY_TYPE")) {
        return false;
    }

    return true;
}

bool test_trigger_matching_immediately_after_reset_emits_single_parm_request() {
    const char* test_name = "test_trigger_matching_immediately_after_reset_emits_single_parm_request";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    harness.machine.reset();
    harness.machine.trigger_matching();

    if (!assert_true(harness.sent_messages.size() == 1, test_name,
                     "reset followed by trigger_matching did not send exactly one CM_SLAC_PARM.REQ")) {
        return false;
    }
    if (!assert_true(not harness.saw_matching_state, test_name, "EV FSM entered MATCHING during trigger_matching")) {
        return false;
    }
    if (!assert_true(is_cm_slac_parm_req(harness.sent_messages.front()), test_name,
                     "reset followed by trigger_matching did not send CM_SLAC_PARM.REQ")) {
        return false;
    }

    return true;
}

bool test_valid_cm_slac_parm_cnf_triggers_matching_and_sounding() {
    const char* test_name = "test_valid_cm_slac_parm_cnf_triggers_matching_and_sounding";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    harness.machine.trigger_matching();

    if (!assert_true(not harness.sent_messages.empty(), test_name, "no CM_SLAC_PARM.REQ emitted")) {
        return false;
    }

    RunId run_id{};
    if (!assert_true(parse_run_id_from_parm_req(harness.sent_messages.front(), run_id), test_name,
                     "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }

    auto const sound_count_before = count_cm_start_atten_char_ind(harness.sent_messages);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    harness.machine.message(create_cm_slac_parm_cnf(evse_mac, run_id));
    harness.machine.update();

    if (!assert_true(harness.saw_matching_state, test_name, "did not publish MATCHING after valid CM_SLAC_PARM.CNF")) {
        return false;
    }

    if (!assert_true(count_cm_start_atten_char_ind(harness.sent_messages) == sound_count_before + 1, test_name,
                     "did not emit CM_START_ATTEN_CHAR.IND after valid CM_SLAC_PARM.CNF")) {
        return false;
    }
    messages::cm_start_atten_char_ind start_atten_char{};
    if (!assert_true(get_last_cm_start_atten_char_ind(harness.sent_messages, start_atten_char), test_name,
                     "did not capture emitted CM_START_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(std::equal(std::begin(start_atten_char.run_id), std::end(start_atten_char.run_id), run_id.begin()),
                     test_name, "CM_START_ATTEN_CHAR.IND run_id does not match CM_SLAC_PARM.REQ run_id")) {
        return false;
    }
    if (!assert_true(is_broadcast_destination(harness.sent_messages.back()), test_name,
                     "CM_START_ATTEN_CHAR.IND destination is not broadcast")) {
        return false;
    }

    return true;
}

bool test_cm_slac_parm_cnf_sounding_sequence() {
    const char* test_name = "test_cm_slac_parm_cnf_sounding_sequence";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    harness.machine.trigger_matching();
    if (!assert_true(not harness.sent_messages.empty(), test_name, "no CM_SLAC_PARM.REQ emitted")) {
        return false;
    }

    RunId run_id{};
    if (!assert_true(parse_run_id_from_parm_req(harness.sent_messages.front(), run_id), test_name,
                     "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }

    auto const start_atten_count_before = count_cm_start_atten_char_ind(harness.sent_messages);
    auto const sound_count_before = count_cm_mnbc_sound_ind(harness.sent_messages);
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    harness.machine.message(create_cm_slac_parm_cnf(evse_mac, run_id));
    harness.machine.update();

    auto const expected_start_count = start_atten_count_before + defs::C_EV_START_ATTEN_CHAR_INDS;
    auto const expected_sound_count = sound_count_before + defs::C_EV_MATCH_MNBC;

    for (std::size_t i = 0; i < defs::C_EV_START_ATTEN_CHAR_INDS + defs::C_EV_MATCH_MNBC + 1; ++i) {
        if (count_cm_start_atten_char_ind(harness.sent_messages) == expected_start_count &&
            count_cm_mnbc_sound_ind(harness.sent_messages) == expected_sound_count) {
            break;
        }
        harness.machine.update();
    }

    if (!assert_true(harness.saw_matching_state, test_name, "did not publish MATCHING after valid CM_SLAC_PARM.CNF")) {
        return false;
    }

    auto const total_start_count = count_cm_start_atten_char_ind(harness.sent_messages);
    auto const total_sound_count = count_cm_mnbc_sound_ind(harness.sent_messages);
    if (!assert_true(total_start_count == expected_start_count, test_name,
                     "did not emit exactly 3 CM_START_ATTEN_CHAR.IND after valid CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(total_sound_count == expected_sound_count, test_name,
                     "did not emit exactly 10 CM_MNBC_SOUND.IND after valid CM_SLAC_PARM.CNF")) {
        return false;
    }

    std::vector<messages::cm_start_atten_char_ind> start_messages;
    if (!assert_true(collect_cm_start_atten_char_ind(harness.sent_messages, start_messages), test_name,
                     "could not parse CM_START_ATTEN_CHAR.IND payload")) {
        return false;
    }
    for (auto const& start_atten_char : start_messages) {
        if (!assert_true(std::equal(std::begin(start_atten_char.forwarding_sta),
                                    std::end(start_atten_char.forwarding_sta), harness.ctx.ev_host_mac.begin()),
                         test_name, "CM_START_ATTEN_CHAR.IND forwarding_sta is not EV host MAC")) {
            return false;
        }
        if (!assert_true(start_atten_char.num_sounds == defs::C_EV_MATCH_MNBC, test_name,
                         "CM_START_ATTEN_CHAR.IND num_sounds is not C_EV_MATCH_MNBC")) {
            return false;
        }
        if (!assert_true(start_atten_char.resp_type == defs::CM_SLAC_PARM_CNF_RESP_TYPE, test_name,
                         "CM_START_ATTEN_CHAR.IND resp_type is not CM_SLAC_PARM_CNF_RESP_TYPE")) {
            return false;
        }
        if (!assert_true(start_atten_char.timeout == (defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100, test_name,
                         "CM_START_ATTEN_CHAR.IND timeout is unexpected")) {
            return false;
        }
        if (!assert_true(
                std::equal(std::begin(start_atten_char.run_id), std::end(start_atten_char.run_id), run_id.begin()),
                test_name, "CM_START_ATTEN_CHAR.IND run_id is not original CM_SLAC_PARM.REQ run_id")) {
            return false;
        }
    }

    std::vector<messages::cm_mnbc_sound_ind> mnbc_messages;
    if (!assert_true(collect_cm_mnbc_sound_ind(harness.sent_messages, mnbc_messages), test_name,
                     "could not parse CM_MNBC_SOUND.IND payload")) {
        return false;
    }
    if (!assert_true(mnbc_messages.size() == defs::C_EV_MATCH_MNBC, test_name,
                     "CM_MNBC_SOUND.IND count changed while collecting")) {
        return false;
    }
    for (std::size_t i = 0; i < mnbc_messages.size(); ++i) {
        auto const expected_remaining = static_cast<std::uint8_t>(defs::C_EV_MATCH_MNBC - 1u - i);
        if (!assert_true(mnbc_messages[i].remaining_sound_count == expected_remaining, test_name,
                         "CM_MNBC_SOUND.IND remaining_sound_count is not descending as expected")) {
            return false;
        }
        if (!assert_true(
                std::equal(std::begin(mnbc_messages[i].run_id), std::end(mnbc_messages[i].run_id), run_id.begin()),
                test_name, "CM_MNBC_SOUND.IND run_id is not original CM_SLAC_PARM.REQ run_id")) {
            return false;
        }
    }

    harness.machine.update();
    if (!assert_true(count_cm_start_atten_char_ind(harness.sent_messages) == total_start_count &&
                         count_cm_mnbc_sound_ind(harness.sent_messages) == total_sound_count,
                     test_name, "extra CM_START_ATTEN_CHAR.IND or CM_MNBC_SOUND.IND emitted after sequence complete")) {
        return false;
    }

    return true;
}

bool test_wrong_run_id_cm_slac_parm_cnf_is_ignored() {
    const char* test_name = "test_wrong_run_id_cm_slac_parm_cnf_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }
    harness.machine.trigger_matching();

    if (!assert_true(not harness.sent_messages.empty(), test_name, "no CM_SLAC_PARM.REQ emitted")) {
        return false;
    }
    RunId run_id{};
    if (!assert_true(parse_run_id_from_parm_req(harness.sent_messages.front(), run_id), test_name,
                     "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }

    run_id[0] ^= 0xFF;
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    auto const sound_count_before = count_cm_start_atten_char_ind(harness.sent_messages);
    harness.machine.message(create_cm_slac_parm_cnf(evse_mac, run_id));
    harness.machine.update();

    if (!assert_true(not harness.saw_matching_state, test_name,
                     "EV entered MATCHING after wrong-run_id CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(count_cm_start_atten_char_ind(harness.sent_messages) == sound_count_before, test_name,
                     "sounding message emitted after wrong-run_id CM_SLAC_PARM.CNF")) {
        return false;
    }

    return true;
}

bool test_short_cm_slac_parm_cnf_is_ignored() {
    const char* test_name = "test_short_cm_slac_parm_cnf_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }
    harness.machine.trigger_matching();

    if (!assert_true(not harness.sent_messages.empty(), test_name, "no CM_SLAC_PARM.REQ emitted")) {
        return false;
    }
    RunId run_id{};
    if (!assert_true(parse_run_id_from_parm_req(harness.sent_messages.front(), run_id), test_name,
                     "CM_SLAC_PARM.REQ payload was not parseable")) {
        return false;
    }

    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    auto short_cnf = create_short_cm_slac_parm_cnf(evse_mac, run_id);
    if (!assert_true(short_cnf.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_SLAC_PARAM.CNF is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cnf.has_payload<std::uint8_t>(), test_name,
                     "short CM_SLAC_PARAM.CNF exposes HomePlug payload bytes")) {
        return false;
    }

    auto const sound_count_before = count_cm_start_atten_char_ind(harness.sent_messages);
    harness.machine.message(short_cnf);
    harness.machine.update();

    if (!assert_true(not harness.saw_matching_state, test_name, "EV entered MATCHING after short CM_SLAC_PARM.CNF")) {
        return false;
    }
    if (!assert_true(count_cm_start_atten_char_ind(harness.sent_messages) == sound_count_before, test_name,
                     "sounding message emitted after short CM_SLAC_PARM.CNF")) {
        return false;
    }

    return true;
}

bool test_cm_atten_char_ind_emits_rsp_and_match_req() {
    const char* test_name = "test_cm_atten_char_ind_emits_rsp_and_match_req";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (!run_matching_with_full_sounding_sequence(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const message_count_before = harness.sent_messages.size();
    auto const rsp_count_before = count_cm_atten_char_rsp(harness.sent_messages);
    auto const match_req_count_before = count_cm_slac_match_req(harness.sent_messages);

    harness.machine.message(create_cm_atten_char_ind(evse_mac, harness.ctx.ev_host_mac, run_id));

    if (!assert_true(rsp_count_before + 1 == count_cm_atten_char_rsp(harness.sent_messages), test_name,
                     "did not emit exactly one CM_ATTEN_CHAR.RSP after CM_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(match_req_count_before + 1 == count_cm_slac_match_req(harness.sent_messages), test_name,
                     "did not emit exactly one CM_SLAC_MATCH.REQ after CM_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(harness.sent_messages.size() == message_count_before + 2, test_name,
                     "did not emit exactly one CM_ATTEN_CHAR.RSP and one CM_SLAC_MATCH.REQ")) {
        return false;
    }

    auto const& atten_char_rsp_message = harness.sent_messages[message_count_before];
    auto const& match_req_message = harness.sent_messages[message_count_before + 1];
    auto const* atten_char_rsp_raw = atten_char_rsp_message.get_raw_message_ptr();
    auto const* match_req_raw = match_req_message.get_raw_message_ptr();

    auto const atten_rsp = atten_char_rsp_message.payload_as<messages::cm_atten_char_rsp>();
    auto const match_req = match_req_message.payload_as<messages::cm_slac_match_req>();
    if (!assert_true(atten_rsp.has_value(), test_name, "CM_ATTEN_CHAR.RSP payload was not parseable")) {
        return false;
    }
    if (!assert_true(match_req.has_value(), test_name, "CM_SLAC_MATCH.REQ payload was not parseable")) {
        return false;
    }

    auto const& atten_rsp_payload = atten_rsp.value();
    auto const& match_req_payload = match_req.value();

    if (!assert_true(std::equal(std::begin(atten_char_rsp_raw->ethernet_header.ether_dhost),
                                std::end(atten_char_rsp_raw->ethernet_header.ether_dhost), evse_mac.begin()),
                     test_name, "CM_ATTEN_CHAR.RSP destination is not EVSE MAC")) {
        return false;
    }
    if (!assert_true(std::equal(std::begin(atten_rsp_payload.source_address),
                                std::end(atten_rsp_payload.source_address), harness.ctx.ev_host_mac.begin()),
                     test_name, "CM_ATTEN_CHAR.RSP source_address is not EV host MAC")) {
        return false;
    }
    if (!assert_true(
            std::equal(std::begin(atten_rsp_payload.run_id), std::end(atten_rsp_payload.run_id), run_id.begin()),
            test_name, "CM_ATTEN_CHAR.RSP run_id does not match CM_SLAC_PARM.CNF run_id")) {
        return false;
    }
    if (!assert_true(atten_rsp_payload.result == defs::CM_ATTEN_CHAR_RSP_RESULT, test_name,
                     "CM_ATTEN_CHAR.RSP result is not CM_ATTEN_CHAR_RSP_RESULT")) {
        return false;
    }
    if (!assert_true(atten_rsp_payload.application_type == defs::COMMON_APPLICATION_TYPE, test_name,
                     "CM_ATTEN_CHAR.RSP application_type is not COMMON_APPLICATION_TYPE")) {
        return false;
    }
    if (!assert_true(atten_rsp_payload.security_type == defs::COMMON_SECURITY_TYPE, test_name,
                     "CM_ATTEN_CHAR.RSP security_type is not COMMON_SECURITY_TYPE")) {
        return false;
    }

    if (!assert_true(std::equal(std::begin(match_req_raw->ethernet_header.ether_dhost),
                                std::end(match_req_raw->ethernet_header.ether_dhost), evse_mac.begin()),
                     test_name, "CM_SLAC_MATCH.REQ destination is not EVSE MAC")) {
        return false;
    }
    if (!assert_true(std::equal(std::begin(match_req_payload.pev_mac), std::end(match_req_payload.pev_mac),
                                harness.ctx.ev_host_mac.begin()),
                     test_name, "CM_SLAC_MATCH.REQ pev_mac is not EV host MAC")) {
        return false;
    }
    if (!assert_true(
            std::equal(std::begin(match_req_payload.evse_mac), std::end(match_req_payload.evse_mac), evse_mac.begin()),
            test_name, "CM_SLAC_MATCH.REQ evse_mac is not EVSE MAC")) {
        return false;
    }
    if (!assert_true(
            std::equal(std::begin(match_req_payload.run_id), std::end(match_req_payload.run_id), run_id.begin()),
            test_name, "CM_SLAC_MATCH.REQ run_id is not CM_SLAC_PARM.CNF run_id")) {
        return false;
    }
    if (!assert_true(match_req_payload.mvf_length == defs::CM_SLAC_MATCH_REQ_MVF_LENGTH, test_name,
                     "CM_SLAC_MATCH.REQ mvf_length is not CM_SLAC_MATCH_REQ_MVF_LENGTH")) {
        return false;
    }
    if (!assert_true(match_req_payload.application_type == defs::COMMON_APPLICATION_TYPE, test_name,
                     "CM_SLAC_MATCH.REQ application_type is not COMMON_APPLICATION_TYPE")) {
        return false;
    }
    if (!assert_true(match_req_payload.security_type == defs::COMMON_SECURITY_TYPE, test_name,
                     "CM_SLAC_MATCH.REQ security_type is not COMMON_SECURITY_TYPE")) {
        return false;
    }

    return true;
}

bool test_wait_atten_char_ind_in_wait_state_accepts_late_result() {
    const char* test_name = "test_wait_atten_char_ind_in_wait_state_accepts_late_result";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (!run_matching_with_full_sounding_sequence(harness, test_name, evse_mac, run_id)) {
        return false;
    }
    harness.machine.update();

    auto const message_count_before = harness.sent_messages.size();
    auto const rsp_count_before = count_cm_atten_char_rsp(harness.sent_messages);
    auto const match_req_count_before = count_cm_slac_match_req(harness.sent_messages);
    auto const warning_count_before = harness.warning_messages.size();

    std::this_thread::sleep_for(std::chrono::milliseconds(defs::TT_MATCH_RESPONSE_MS + 10));
    harness.machine.update();

    if (!assert_true(warning_count_before == harness.warning_messages.size(), test_name,
                     "EV transitioned to failed state before CM_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(message_count_before == harness.sent_messages.size(), test_name,
                     "messages sent unexpectedly after waiting in WaitAttenCharInd")) {
        return false;
    }

    harness.machine.message(create_cm_atten_char_ind(evse_mac, harness.ctx.ev_host_mac, run_id));

    if (!assert_true(rsp_count_before + 1 == count_cm_atten_char_rsp(harness.sent_messages), test_name,
                     "did not emit exactly one CM_ATTEN_CHAR.RSP after delayed CM_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(match_req_count_before + 1 == count_cm_slac_match_req(harness.sent_messages), test_name,
                     "did not emit exactly one CM_SLAC_MATCH.REQ after delayed CM_ATTEN_CHAR.IND")) {
        return false;
    }
    if (!assert_true(
            message_count_before + 2 == harness.sent_messages.size(), test_name,
            "did not emit exactly one CM_ATTEN_CHAR.RSP and one CM_SLAC_MATCH.REQ after delayed CM_ATTEN_CHAR.IND")) {
        return false;
    }

    return true;
}

bool test_wrong_run_id_cm_atten_char_ind_is_ignored() {
    const char* test_name = "test_wrong_run_id_cm_atten_char_ind_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (!run_matching_with_full_sounding_sequence(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto wrong_run_id = run_id;
    wrong_run_id[0] ^= 0xFF;
    auto const message_count_before = harness.sent_messages.size();
    auto const rsp_count_before = count_cm_atten_char_rsp(harness.sent_messages);
    auto const match_req_count_before = count_cm_slac_match_req(harness.sent_messages);

    harness.machine.message(create_cm_atten_char_ind(evse_mac, harness.ctx.ev_host_mac, wrong_run_id));
    for (std::size_t i = 0; i < 3; ++i) {
        harness.machine.update();
    }

    if (!assert_true(harness.sent_messages.size() == message_count_before, test_name,
                     "CM_ATTEN_CHAR.IND with wrong run_id emitted CM_ATTEN_CHAR.RSP/CM_SLAC_MATCH.REQ")) {
        return false;
    }
    if (!assert_true(rsp_count_before == count_cm_atten_char_rsp(harness.sent_messages), test_name,
                     "CM_ATTEN_CHAR.IND with wrong run_id emitted CM_ATTEN_CHAR.RSP")) {
        return false;
    }
    if (!assert_true(match_req_count_before == count_cm_slac_match_req(harness.sent_messages), test_name,
                     "CM_ATTEN_CHAR.IND with wrong run_id emitted CM_SLAC_MATCH.REQ")) {
        return false;
    }

    return true;
}

bool test_short_cm_atten_char_ind_is_ignored() {
    const char* test_name = "test_short_cm_atten_char_ind_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    if (!run_matching_with_full_sounding_sequence(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const short_cm_atten_char_ind = create_short_cm_atten_char_ind(evse_mac, harness.ctx.ev_host_mac, run_id);
    if (!assert_true(short_cm_atten_char_ind.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_ATTEN_CHAR.IND is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cm_atten_char_ind.has_payload<std::uint8_t>(), test_name,
                     "short CM_ATTEN_CHAR.IND exposes HomePlug payload bytes")) {
        return false;
    }

    auto const message_count_before = harness.sent_messages.size();
    auto const rsp_count_before = count_cm_atten_char_rsp(harness.sent_messages);
    auto const match_req_count_before = count_cm_slac_match_req(harness.sent_messages);

    harness.machine.message(short_cm_atten_char_ind);

    if (!assert_true(harness.sent_messages.size() == message_count_before, test_name,
                     "short CM_ATTEN_CHAR.IND emitted CM_ATTEN_CHAR.RSP/CM_SLAC_MATCH.REQ")) {
        return false;
    }
    if (!assert_true(rsp_count_before == count_cm_atten_char_rsp(harness.sent_messages), test_name,
                     "short CM_ATTEN_CHAR.IND emitted CM_ATTEN_CHAR.RSP")) {
        return false;
    }
    if (!assert_true(match_req_count_before == count_cm_slac_match_req(harness.sent_messages), test_name,
                     "short CM_ATTEN_CHAR.IND emitted CM_SLAC_MATCH.REQ")) {
        return false;
    }

    return true;
}

bool test_valid_cm_slac_match_cnf_emits_cm_set_key_req() {
    const char* test_name = "test_valid_cm_slac_match_cnf_emits_cm_set_key_req";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_match_cnf(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);
    auto const message_count_before = harness.sent_messages.size();
    harness.machine.message(create_cm_slac_match_cnf(evse_mac, evse_mac, harness.ctx.ev_host_mac, run_id, match_nmk));

    if (!assert_true(set_key_count_before + 1 == count_cm_set_key_req(harness.sent_messages), test_name,
                     "did not emit exactly one CM_SET_KEY.REQ after valid CM_SLAC_MATCH.CNF")) {
        return false;
    }
    if (!assert_true(message_count_before + 1 == harness.sent_messages.size(), test_name,
                     "CM_SLAC_MATCH.CNF emitted unexpected number of messages")) {
        return false;
    }

    auto const& set_key_message = harness.sent_messages.back();
    if (!assert_true(set_key_message.get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ), test_name,
                     "CM_SET_KEY request has wrong mmtype")) {
        return false;
    }
    if (!assert_true(is_destination(set_key_message, Context::EV_PLC_MAC), test_name,
                     "CM_SET_KEY.REQ destination is not EV_PLC_MAC")) {
        return false;
    }

    messages::cm_set_key_req set_key_payload{};
    if (!assert_true(get_last_cm_set_key_req(harness.sent_messages, set_key_payload), test_name,
                     "did not parse CM_SET_KEY.REQ payload")) {
        return false;
    }
    if (!assert_true(
            std::equal(std::begin(set_key_payload.new_key), std::end(set_key_payload.new_key), match_nmk.begin()),
            test_name, "CM_SET_KEY.REQ new_key is not CM_SLAC_MATCH.CNF NMK")) {
        return false;
    }
    if (!assert_true(set_key_payload.key_type == defs::CM_SET_KEY_REQ_KEY_TYPE_NMK, test_name,
                     "CM_SET_KEY.REQ key_type is not NMK")) {
        return false;
    }
    if (!assert_true(set_key_payload.pid == defs::CM_SET_KEY_REQ_PID_HLE, test_name,
                     "CM_SET_KEY.REQ pid is not CM_SET_KEY_REQ_PID_HLE")) {
        return false;
    }
    if (!assert_true(set_key_payload.prn == defs::CM_SET_KEY_REQ_PRN_UNUSED, test_name,
                     "CM_SET_KEY.REQ prn is not CM_SET_KEY_REQ_PRN_UNUSED")) {
        return false;
    }
    if (!assert_true(set_key_payload.pmn == defs::CM_SET_KEY_REQ_PMN_UNUSED, test_name,
                     "CM_SET_KEY.REQ pmn is not CM_SET_KEY_REQ_PMN_UNUSED")) {
        return false;
    }
    if (!assert_true(set_key_payload.cco_capability == defs::CM_SET_KEY_REQ_CCO_CAP_NONE, test_name,
                     "CM_SET_KEY.REQ cco_capability is not CM_SET_KEY_REQ_CCO_CAP_NONE")) {
        return false;
    }
    if (!assert_true(set_key_payload.new_eks == defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA, test_name,
                     "CM_SET_KEY.REQ new_eks is not CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA")) {
        return false;
    }

    std::array<std::uint8_t, defs::NID_LEN> derived_nid{};
    utils::generate_nid_from_nmk(derived_nid.data(), match_nmk.data());
    if (!assert_true(std::equal(std::begin(set_key_payload.nid), std::end(set_key_payload.nid), derived_nid.begin()),
                     test_name, "CM_SET_KEY.REQ NID does not match NMK-derived NID")) {
        return false;
    }

    return true;
}

bool test_wrong_run_id_cm_slac_match_cnf_is_ignored() {
    const char* test_name = "test_wrong_run_id_cm_slac_match_cnf_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_match_cnf(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto wrong_run_id = run_id;
    wrong_run_id[0] ^= 0xFF;
    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);
    harness.machine.message(
        create_cm_slac_match_cnf(evse_mac, evse_mac, harness.ctx.ev_host_mac, wrong_run_id, match_nmk));

    if (!assert_true(set_key_count_before == count_cm_set_key_req(harness.sent_messages), test_name,
                     "received CM_SET_KEY.REQ after wrong-run-id CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return true;
}

bool test_wrong_source_mac_cm_slac_match_cnf_is_ignored() {
    const char* test_name = "test_wrong_source_mac_cm_slac_match_cnf_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    EvMac wrong_evse_source = {0x02, 0x00, 0x00, 0x00, 0x00, 0x99};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_match_cnf(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);
    harness.machine.message(
        create_cm_slac_match_cnf(wrong_evse_source, evse_mac, harness.ctx.ev_host_mac, run_id, match_nmk));

    if (!assert_true(set_key_count_before == count_cm_set_key_req(harness.sent_messages), test_name,
                     "received CM_SET_KEY.REQ after wrong-source CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return true;
}

bool test_short_cm_slac_match_cnf_is_ignored() {
    const char* test_name = "test_short_cm_slac_match_cnf_is_ignored";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_match_cnf(harness, test_name, evse_mac, run_id)) {
        return false;
    }

    auto short_cnf = create_short_cm_slac_match_cnf(evse_mac, harness.ctx.ev_host_mac, run_id, match_nmk);
    if (!assert_true(short_cnf.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_SLAC_MATCH.CNF is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cnf.has_payload<std::uint8_t>(), test_name,
                     "short CM_SLAC_MATCH.CNF exposes HomePlug payload bytes")) {
        return false;
    }

    auto const set_key_count_before = count_cm_set_key_req(harness.sent_messages);
    harness.machine.message(short_cnf);

    if (!assert_true(set_key_count_before == count_cm_set_key_req(harness.sent_messages), test_name,
                     "received CM_SET_KEY.REQ after short CM_SLAC_MATCH.CNF")) {
        return false;
    }

    return true;
}

bool test_valid_cm_set_key_cnf_enters_matched_and_dlink_ready() {
    const char* test_name = "test_valid_cm_set_key_cnf_enters_matched_and_dlink_ready";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_set_key_cnf(harness, test_name, evse_mac, run_id, match_nmk)) {
        return false;
    }

    auto const valid_set_key_cnf = create_cm_set_key_cnf(SetKeyCnfParams{evse_mac});
    if (!assert_true(is_cm_set_key_cnf(valid_set_key_cnf), test_name, "created CM_SET_KEY.CNF has unexpected MMTYPE")) {
        return false;
    }

    harness.machine.message(valid_set_key_cnf);
    harness.machine.update();

    if (!assert_true(harness.saw_matched_state, test_name, "did not enter MATCHED after valid CM_SET_KEY.CNF")) {
        return false;
    }
    if (!assert_true(harness.saw_dlink_true, test_name, "did not set dlink_ready=true after valid CM_SET_KEY.CNF")) {
        return false;
    }

    return true;
}

bool test_short_cm_set_key_cnf_is_ignored_in_wait_set_key_cnf() {
    const char* test_name = "test_short_cm_set_key_cnf_is_ignored_in_wait_set_key_cnf";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_set_key_cnf(harness, test_name, evse_mac, run_id, match_nmk)) {
        return false;
    }

    auto const short_cnf = create_short_cm_set_key_cnf(SetKeyCnfParams{evse_mac});
    if (!assert_true(short_cnf.frame_size() < defs::MME_MIN_LENGTH, test_name,
                     "short CM_SET_KEY.CNF is not below minimum expected-frame length")) {
        return false;
    }
    if (!assert_true(not short_cnf.has_payload<std::uint8_t>(), test_name,
                     "short CM_SET_KEY.CNF exposes HomePlug payload bytes")) {
        return false;
    }

    harness.machine.message(short_cnf);
    harness.machine.update();

    if (!assert_true(not has_state(harness.state_messages, "MATCHED"), test_name,
                     "EV published MATCHED after short CM_SET_KEY.CNF")) {
        return false;
    }
    if (!assert_true(not harness.saw_dlink_true, test_name, "EV set dlink_ready=true after short CM_SET_KEY.CNF")) {
        return false;
    }

    return true;
}

bool test_reset_after_matched_returns_to_unmatched() {
    const char* test_name = "test_reset_after_matched_returns_to_unmatched";

    TestHarness harness{};
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    RunId run_id{};
    EvMac evse_mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    Nmk match_nmk = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
    if (!run_matching_to_wait_for_set_key_cnf(harness, test_name, evse_mac, run_id, match_nmk)) {
        return false;
    }

    auto const valid_set_key_cnf = create_cm_set_key_cnf(SetKeyCnfParams{evse_mac});
    harness.machine.message(valid_set_key_cnf);
    harness.machine.update();

    if (!assert_true(count_state(harness.state_messages, "MATCHED") == 1, test_name, "did not enter MATCHED once")) {
        return false;
    }
    if (!assert_true(dlink_last_ready(harness.dlink_ready_history, true), test_name,
                     "did not set dlink_ready=true after valid CM_SET_KEY.CNF")) {
        return false;
    }

    auto const unmatched_state_count_before_reset = count_state(harness.state_messages, "UNMATCHED");
    auto const dlink_false_count_before_reset = count_dlink_ready(harness.dlink_ready_history, false);
    harness.machine.reset();
    harness.machine.update();

    if (!assert_true(count_state(harness.state_messages, "MATCHED") == 1, test_name,
                     "MATCHED state changed unexpectedly after reset")) {
        return false;
    }
    if (!assert_true(count_state(harness.state_messages, "UNMATCHED") > unmatched_state_count_before_reset, test_name,
                     "did not publish UNMATCHED after reset")) {
        return false;
    }
    if (!assert_true(count_dlink_ready(harness.dlink_ready_history, false) > dlink_false_count_before_reset, test_name,
                     "did not publish dlink_ready=false after reset")) {
        return false;
    }
    if (!assert_true(!harness.dlink_ready_history.empty() && harness.dlink_ready_history.back() == false, test_name,
                     "last dlink_ready value after reset is not false")) {
        return false;
    }

    return true;
}

bool test_timeout_failure_reaches_failed_state() {
    const char* test_name = "test_timeout_failure_reaches_failed_state";

    TestHarness harness{};
    harness.ctx.slac_config.parm_req_timeout_ms = 1;
    harness.ctx.slac_config.parm_req_attempts = 1;
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    auto const unmatched_state_count_before_matching = count_state(harness.state_messages, "UNMATCHED");
    auto const warning_count_before_matching = harness.warning_messages.size();
    auto const dlink_false_count_before_matching = count_dlink_ready(harness.dlink_ready_history, false);

    harness.machine.trigger_matching();
    for (int i = 0;
         i < 10 && count_state(harness.state_messages, "UNMATCHED") == unmatched_state_count_before_matching &&
         harness.warning_messages.size() == warning_count_before_matching;
         ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        harness.machine.update();
    }

    if (!assert_true(harness.warning_messages.size() > warning_count_before_matching, test_name,
                     "did not log additional warning after timeout")) {
        return false;
    }
    if (!assert_true(has_warning(harness.warning_messages, "EV MSM entered failed"), test_name,
                     "did not log failed warning after timeout")) {
        return false;
    }
    if (!assert_true(count_state(harness.state_messages, "UNMATCHED") > unmatched_state_count_before_matching,
                     test_name, "did not publish UNMATCHED after timeout failure")) {
        return false;
    }
    if (!assert_true(count_dlink_ready(harness.dlink_ready_history, false) > dlink_false_count_before_matching,
                     test_name, "did not observe dlink_ready=false publication")) {
        return false;
    }
    if (!assert_true(dlink_last_ready(harness.dlink_ready_history, false), test_name,
                     "last dlink_ready value after timeout is not false")) {
        return false;
    }

    return true;
}

bool test_parm_req_attempts_config_controls_retry_count() {
    const char* test_name = "test_parm_req_attempts_config_controls_retry_count";

    TestHarness harness{};
    harness.ctx.slac_config.parm_req_timeout_ms = 1;
    harness.ctx.slac_config.parm_req_attempts = 3;
    if (!init_unmatched(harness, test_name)) {
        return false;
    }

    const auto count_parm_req = [&harness]() {
        return static_cast<int>(std::count_if(harness.sent_messages.begin(), harness.sent_messages.end(),
                                              [](messages::HomeplugMessage const& m) { return is_cm_slac_parm_req(m); }));
    };

    harness.machine.trigger_matching();
    // The first CM_SLAC_PARM.REQ is sent immediately on trigger_matching.
    if (!assert_true(count_parm_req() == 1, test_name, "trigger_matching did not emit the first CM_SLAC_PARM.REQ")) {
        return false;
    }

    // Drive timeout-based retries until the FSM exhausts the attempts and fails.
    const auto unmatched_before = count_state(harness.state_messages, "UNMATCHED");
    for (int i = 0; i < 50 && count_state(harness.state_messages, "UNMATCHED") == unmatched_before; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        harness.machine.update();
    }

    if (!assert_true(has_warning(harness.warning_messages, "EV MSM entered failed"), test_name,
                     "did not reach failed state after exhausting parm_req_attempts")) {
        return false;
    }
    // Exactly parm_req_attempts transmissions must have been emitted: not fewer (retries must fire),
    // not more (must stop and fail once the configured budget is spent).
    return assert_true(count_parm_req() == 3, test_name,
                       "number of CM_SLAC_PARM.REQ transmissions did not match configured parm_req_attempts");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 19>{
        std::make_pair("test_trigger_matching_emits_single_parm_request",
                       test_trigger_matching_emits_single_parm_request),
        std::make_pair("test_trigger_matching_immediately_after_reset_emits_single_parm_request",
                       test_trigger_matching_immediately_after_reset_emits_single_parm_request),
        std::make_pair("test_valid_cm_slac_parm_cnf_triggers_matching_and_sounding",
                       test_valid_cm_slac_parm_cnf_triggers_matching_and_sounding),
        std::make_pair("test_cm_slac_parm_cnf_sounding_sequence", test_cm_slac_parm_cnf_sounding_sequence),
        std::make_pair("test_wrong_run_id_cm_slac_parm_cnf_is_ignored", test_wrong_run_id_cm_slac_parm_cnf_is_ignored),
        std::make_pair("test_short_cm_slac_parm_cnf_is_ignored", test_short_cm_slac_parm_cnf_is_ignored),
        std::make_pair("test_cm_atten_char_ind_emits_rsp_and_match_req",
                       test_cm_atten_char_ind_emits_rsp_and_match_req),
        std::make_pair("test_wait_atten_char_ind_in_wait_state_accepts_late_result",
                       test_wait_atten_char_ind_in_wait_state_accepts_late_result),
        std::make_pair("test_wrong_run_id_cm_atten_char_ind_is_ignored",
                       test_wrong_run_id_cm_atten_char_ind_is_ignored),
        std::make_pair("test_short_cm_atten_char_ind_is_ignored", test_short_cm_atten_char_ind_is_ignored),
        std::make_pair("test_valid_cm_slac_match_cnf_emits_cm_set_key_req",
                       test_valid_cm_slac_match_cnf_emits_cm_set_key_req),
        std::make_pair("test_wrong_run_id_cm_slac_match_cnf_is_ignored",
                       test_wrong_run_id_cm_slac_match_cnf_is_ignored),
        std::make_pair("test_wrong_source_mac_cm_slac_match_cnf_is_ignored",
                       test_wrong_source_mac_cm_slac_match_cnf_is_ignored),
        std::make_pair("test_short_cm_slac_match_cnf_is_ignored", test_short_cm_slac_match_cnf_is_ignored),
        std::make_pair("test_valid_cm_set_key_cnf_enters_matched_and_dlink_ready",
                       test_valid_cm_set_key_cnf_enters_matched_and_dlink_ready),
        std::make_pair("test_short_cm_set_key_cnf_is_ignored_in_wait_set_key_cnf",
                       test_short_cm_set_key_cnf_is_ignored_in_wait_set_key_cnf),
        std::make_pair("test_reset_after_matched_returns_to_unmatched", test_reset_after_matched_returns_to_unmatched),
        std::make_pair("test_timeout_failure_reaches_failed_state", test_timeout_failure_reaches_failed_state),
        std::make_pair("test_parm_req_attempts_config_controls_retry_count",
                       test_parm_req_attempts_config_controls_retry_count),
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
