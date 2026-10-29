// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/fsm/evse/states/matching.hpp>

namespace {

using slac::fsm::evse::Context;
using slac::fsm::evse::Event;
using slac::fsm::evse::MatchingState;
using slac::fsm::evse::MatchingSubState;
using slac::fsm::evse::MAX_MATCHING_SESSIONS;

std::array<uint8_t, slac::defs::RUN_ID_LEN> run_id_of(uint32_t run) {
    std::array<uint8_t, slac::defs::RUN_ID_LEN> run_id{};
    std::memcpy(run_id.data(), &run, sizeof(run));
    return run_id;
}

class EvseMatchingSessionsTest : public ::testing::Test {
protected:
    void SetUp() override {
        ctx.callbacks.send_raw_slac = [this](slac::messages::HomeplugMessage&) { ++sent_messages; };
        ctx.callbacks.log_warn = [this](const std::string& text) {
            ++warnings;
            last_warning = text;
        };
        ctx.callbacks.signal_ev_mac_address_parm_req = [this](const std::string&) { ++parm_req_signals; };
        state.enter();
    }

    // CM_SLAC_PARM.REQ from the EV MAC ending in ev_mac_last_byte, with a run id derived from run
    void parm_req(uint8_t ev_mac_last_byte, uint32_t run) {
        ev_mac[5] = ev_mac_last_byte;
        slac::messages::cm_slac_parm_req msg{};
        msg.application_type = slac::defs::COMMON_APPLICATION_TYPE;
        msg.security_type = slac::defs::COMMON_SECURITY_TYPE;
        const auto run_id = run_id_of(run);
        std::memcpy(msg.run_id, run_id.data(), run_id.size());
        state.tmp_ev_mac = ev_mac;
        state.handle_cm_slac_parm_req(msg);
    }

    // CM_START_ATTEN_CHAR.IND of the same EV, moves its session on to SOUNDING
    void start_atten_char_ind(uint8_t ev_mac_last_byte, uint32_t run) {
        ev_mac[5] = ev_mac_last_byte;
        slac::messages::cm_start_atten_char_ind msg{};
        msg.application_type = slac::defs::COMMON_APPLICATION_TYPE;
        msg.security_type = slac::defs::COMMON_SECURITY_TYPE;
        msg.num_sounds = slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
        msg.timeout = slac::defs::CM_SLAC_PARM_CNF_TIMEOUT;
        msg.resp_type = slac::defs::CM_SLAC_PARM_CNF_RESP_TYPE;
        std::memcpy(msg.forwarding_sta, ev_mac, sizeof(msg.forwarding_sta));
        const auto run_id = run_id_of(run);
        std::memcpy(msg.run_id, run_id.data(), run_id.size());
        state.tmp_ev_mac = ev_mac;
        state.handle_cm_start_atten_char_ind(msg);
    }

    const slac::fsm::evse::MatchingSession* find(uint8_t ev_mac_last_byte, uint32_t run) {
        uint8_t mac[ETH_ALEN];
        std::memcpy(mac, ev_mac, sizeof(mac));
        mac[5] = ev_mac_last_byte;
        const auto run_id = run_id_of(run);
        for (const auto& session : state.sessions) {
            if (session.is_identified_by(mac, run_id.data())) {
                return &session;
            }
        }
        return nullptr;
    }

    bool has_session(uint8_t ev_mac_last_byte, uint32_t run) {
        return find(ev_mac_last_byte, run) != nullptr;
    }

    void expire(slac::fsm::evse::MatchingSession& session) {
        session.next_timeout = std::chrono::steady_clock::now() - std::chrono::milliseconds(1);
    }

    Context ctx;
    MatchingState state{ctx};
    uint8_t ev_mac[ETH_ALEN] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    std::size_t sent_messages{0};
    std::size_t warnings{0};
    std::string last_warning;
    std::size_t parm_req_signals{0};
};

TEST_F(EvseMatchingSessionsTest, single_parm_req_creates_one_session) {
    ASSERT_FALSE(state.seen_slac_parm_req);
    parm_req(1, 1);
    ASSERT_EQ(state.sessions.size(), 1u);
    EXPECT_TRUE(state.seen_slac_parm_req);
    EXPECT_EQ(sent_messages, 1u);
    EXPECT_EQ(parm_req_signals, 1u);
}

TEST_F(EvseMatchingSessionsTest, repeated_parm_req_restarts_the_same_session) {
    for (int i = 0; i < 10; ++i) {
        parm_req(1, 1);
    }
    EXPECT_EQ(state.sessions.size(), 1u);
    EXPECT_EQ(sent_messages, 10u);
}

TEST_F(EvseMatchingSessionsTest, evs_sharing_a_mac_keep_separate_sessions) {
    parm_req(1, 1);
    start_atten_char_ind(1, 1);
    ASSERT_EQ(find(1, 1)->state, MatchingSubState::SOUNDING);

    // a neighbouring EV with the same MAC starts its own run
    parm_req(1, 2);
    ASSERT_EQ(state.sessions.size(), 2u);
    EXPECT_EQ(find(1, 1)->state, MatchingSubState::SOUNDING);
    EXPECT_EQ(find(1, 2)->state, MatchingSubState::WAIT_FOR_START_ATTEN_CHAR);
}

TEST_F(EvseMatchingSessionsTest, flood_of_distinct_parm_req_is_capped) {
    constexpr uint32_t flood = 10000;
    for (uint32_t run = 0; run < flood; ++run) {
        parm_req(static_cast<uint8_t>(run), run);
    }
    ASSERT_EQ(state.sessions.size(), MAX_MATCHING_SESSIONS);
    // the first EVs keep their slots, only their requests are answered and signalled
    for (uint32_t run = 0; run < MAX_MATCHING_SESSIONS; ++run) {
        EXPECT_TRUE(has_session(static_cast<uint8_t>(run), run));
    }
    EXPECT_EQ(sent_messages, MAX_MATCHING_SESSIONS);
    EXPECT_EQ(parm_req_signals, MAX_MATCHING_SESSIONS);
    EXPECT_EQ(warnings, 1u);
}

TEST_F(EvseMatchingSessionsTest, sessions_in_progress_keep_their_slots_at_the_cap) {
    for (uint8_t ev = 0; ev < MAX_MATCHING_SESSIONS; ++ev) {
        parm_req(ev, 1);
        start_atten_char_ind(ev, 1);
    }
    const auto sent_before = sent_messages;

    parm_req(0xF0, 100);
    parm_req(0xF1, 100);
    EXPECT_FALSE(has_session(0xF0, 100));
    EXPECT_FALSE(has_session(0xF1, 100));
    EXPECT_EQ(sent_messages, sent_before);
    EXPECT_EQ(parm_req_signals, sent_before);
    EXPECT_EQ(warnings, 1u);
    EXPECT_NE(last_warning.find("00:F0"), std::string::npos) << last_warning;

    // an EV that already has a session is still restarted at the cap
    parm_req(0, 1);
    EXPECT_EQ(find(0, 1)->state, MatchingSubState::WAIT_FOR_START_ATTEN_CHAR);
    EXPECT_EQ(sent_messages, sent_before + 1);
    EXPECT_EQ(state.sessions.size(), MAX_MATCHING_SESSIONS);
}

TEST_F(EvseMatchingSessionsTest, failed_session_slot_is_reused) {
    for (uint8_t ev = 0; ev < MAX_MATCHING_SESSIONS; ++ev) {
        parm_req(ev, 1);
    }
    // the second session times out, the others stay in progress
    expire(state.sessions[1]);
    const auto result = state.callback();
    EXPECT_FALSE(result.is_event);
    ASSERT_EQ(state.sessions[1].state, MatchingSubState::FAILED);

    parm_req(0xF0, 100);
    ASSERT_EQ(state.sessions.size(), MAX_MATCHING_SESSIONS);
    EXPECT_TRUE(has_session(0xF0, 100));
    EXPECT_FALSE(has_session(1, 1));
    EXPECT_EQ(state.sessions[1].state, MatchingSubState::WAIT_FOR_START_ATTEN_CHAR);
    EXPECT_EQ(parm_req_signals, MAX_MATCHING_SESSIONS + 1);
    EXPECT_EQ(warnings, 0u);
}

TEST_F(EvseMatchingSessionsTest, warning_is_repeated_once_a_slot_was_reused) {
    for (uint8_t ev = 0; ev < MAX_MATCHING_SESSIONS; ++ev) {
        parm_req(ev, 1);
    }
    parm_req(0xF0, 100);
    parm_req(0xF1, 100);
    ASSERT_EQ(warnings, 1u);

    expire(state.sessions[0]);
    state.callback();
    parm_req(0xF2, 100);
    ASSERT_TRUE(has_session(0xF2, 100));

    parm_req(0xF3, 100);
    parm_req(0xF4, 100);
    EXPECT_EQ(warnings, 2u);
    EXPECT_NE(last_warning.find("00:F3"), std::string::npos) << last_warning;
}

TEST_F(EvseMatchingSessionsTest, single_failed_session_still_fails_matching) {
    parm_req(1, 1);
    expire(state.sessions[0]);
    const auto result = state.callback();
    ASSERT_TRUE(result.is_event);
    EXPECT_EQ(result.event, Event::FAILED);
}

TEST_F(EvseMatchingSessionsTest, all_capped_sessions_failed_fails_matching) {
    for (uint8_t ev = 0; ev < 100; ++ev) {
        parm_req(ev, 1);
    }
    for (auto& session : state.sessions) {
        expire(session);
    }
    const auto result = state.callback();
    ASSERT_TRUE(result.is_event);
    EXPECT_EQ(result.event, Event::FAILED);
}

} // namespace
