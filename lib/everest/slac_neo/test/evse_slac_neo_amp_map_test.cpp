// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 - 2026 Pionix GmbH and Contributors to EVerest
//
// Unit tests for the SECC-initiated CM_AMP_MAP exchange (src/fsm/evse/amp_map_handler.hpp), driven
// directly instead of through the whole Matched sub-machine. The end-to-end path is covered by
// evse_slac_neo_matching_test.

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <utility>
#include <vector>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

#include "../src/fsm/evse/amp_map_handler.hpp"
#include "mock_clock.hpp"

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

struct SentReq {
    MacAddress destination;
    std::uint16_t am_len;
};

const MacAddress ev_mac{{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x01}};

// A Context whose only callback records the CM_AMP_MAP.REQ frames the handler sends, configured to
// initiate the exchange with a two-entry amplitude map.
struct Harness {
    ContextCallbacks callbacks{};
    std::vector<SentReq> sent;
    test::MockClock clock;
    Context ctx{callbacks};

    Harness() {
        callbacks.now = clock.source();
        ctx.sample_time();
        callbacks.send_raw_slac = [this](messages::HomeplugMessage& hp_message) {
            if (hp_message.get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_REQ)) {
                return true;
            }
            auto const* raw = hp_message.get_raw_message_ptr();
            SentReq entry{};
            std::copy(std::begin(raw->ethernet_header.ether_dhost), std::end(raw->ethernet_header.ether_dhost),
                      entry.destination.begin());
            entry.am_len = hp_message.get_payload<messages::cm_amp_map_req>().am_len;
            sent.push_back(entry);
            return true;
        };
        ctx.status.ev_mac = ev_mac;
        ctx.slac_config.initiate_amp_map = true;
        ctx.slac_config.amp_map_len = 2;
        ctx.slac_config.amp_map_data = {0xFF};
    }

    // Move time forward as the state machine would see it: one sample per event.
    void advance_ms(long long ms) {
        clock.advance_ms(ms);
        ctx.sample_time();
    }
};

messages::HomeplugMessage make_cnf(std::uint8_t result) {
    messages::cm_amp_map_cnf cnf{};
    cnf.result = result;
    messages::HomeplugMessage message;
    message.setup_payload(&cnf, sizeof(cnf), defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_CNF, defs::MMV::AV_2_0);
    return message;
}

bool test_no_request_when_not_configured() {
    const char* test_name = "test_no_request_when_not_configured";
    Harness h;
    AmpMapHandler handler;

    h.ctx.slac_config.initiate_amp_map = false;
    handler.start(h.ctx);
    if (not assert_true(h.sent.empty(), test_name, "no REQ when initiate_amp_map is off") or
        not assert_true(not handler.awaiting_cnf(), test_name, "nothing awaited when the flag is off")) {
        return false;
    }

    // The flag alone is not enough: an empty map disables the transmit direction (CmAmpMap_005).
    h.ctx.slac_config.initiate_amp_map = true;
    h.ctx.slac_config.amp_map_len = 0;
    handler.start(h.ctx);
    return assert_true(h.sent.empty(), test_name, "no REQ for an empty amplitude map") and
           assert_true(not handler.awaiting_cnf(), test_name, "nothing awaited for an empty map") and
           assert_true(not handler.retransmit_due(h.ctx.current_time), test_name,
                       "no retransmission without a request");
}

bool test_start_sends_request_and_awaits_cnf() {
    const char* test_name = "test_start_sends_request_and_awaits_cnf";
    Harness h;
    AmpMapHandler handler;

    handler.start(h.ctx);

    return assert_true(h.sent.size() == 1, test_name, "start must send exactly one REQ") and
           assert_true(h.sent[0].destination == ev_mac, test_name, "REQ must go to the matched EV") and
           assert_true(h.sent[0].am_len == 2, test_name, "REQ must carry the configured am_len") and
           assert_true(handler.awaiting_cnf(), test_name, "a CNF must be awaited after start") and
           assert_true(handler.retries() == 0, test_name, "no retransmission yet") and
           assert_true(not handler.retransmit_due(h.ctx.current_time), test_name,
                       "interval must not have elapsed right away");
}

bool test_retransmits_after_interval_then_gives_up() {
    const char* test_name = "test_retransmits_after_interval_then_gives_up";
    Harness h;
    AmpMapHandler handler;

    handler.start(h.ctx);

    for (int i = 1; i <= defs::C_EV_MATCH_RETRY; ++i) {
        h.advance_ms(defs::TT_MATCH_RESPONSE_MS + 1);
        if (not assert_true(handler.retransmit_due(h.ctx.current_time), test_name, "interval must have elapsed")) {
            return false;
        }
        handler.retransmit(h.ctx);
        if (not assert_true(static_cast<int>(h.sent.size()) == 1 + i, test_name, "each service must retransmit once") or
            not assert_true(handler.retries() == i, test_name, "retry counter must follow") or
            not assert_true(handler.awaiting_cnf(), test_name, "still awaiting within the retry budget") or
            not assert_true(not handler.retransmit_due(h.ctx.current_time), test_name, "interval must be re-armed")) {
            return false;
        }
    }

    // Retry budget exhausted: the next service stops the exchange without another frame.
    h.advance_ms(defs::TT_MATCH_RESPONSE_MS + 1);
    if (not assert_true(handler.retransmit_due(h.ctx.current_time), test_name,
                        "interval must have elapsed once more")) {
        return false;
    }
    handler.retransmit(h.ctx);
    return assert_true(static_cast<int>(h.sent.size()) == 1 + defs::C_EV_MATCH_RETRY, test_name,
                       "no frame beyond C_EV_match_retry retransmissions") and
           assert_true(not handler.awaiting_cnf(), test_name, "exchange must be over after the retry limit") and
           assert_true(not handler.retransmit_due(h.ctx.current_time), test_name, "nothing due once given up");
}

bool test_cnf_stops_retransmission() {
    const char* test_name = "test_cnf_stops_retransmission";
    Harness h;
    AmpMapHandler handler;

    handler.start(h.ctx);

    // A CNF with a non-success result is not the awaited one and must not stop anything.
    if (not assert_true(not handler.is_awaited_cnf(make_cnf(0x01)), test_name,
                        "CNF with result != 0x00 must be ignored")) {
        return false;
    }
    auto const ok = make_cnf(defs::CM_AMP_MAP_CNF_RESULT_SUCCESS);
    if (not assert_true(handler.is_awaited_cnf(ok), test_name, "CNF(result=0x00) must be recognised")) {
        return false;
    }
    handler.acknowledge_cnf();

    h.advance_ms(defs::TT_MATCH_RESPONSE_MS + 1);
    return assert_true(not handler.awaiting_cnf(), test_name, "CNF must end the exchange") and
           assert_true(not handler.retransmit_due(h.ctx.current_time), test_name, "no retransmission after the CNF") and
           assert_true(not handler.is_awaited_cnf(ok), test_name,
                       "a CNF is only awaited while a REQ is outstanding") and
           assert_true(h.sent.size() == 1, test_name, "only the initial REQ must have been sent");
}

bool test_reset_forgets_exchange() {
    const char* test_name = "test_reset_forgets_exchange";
    Harness h;
    AmpMapHandler handler;

    handler.start(h.ctx);
    handler.reset();

    h.advance_ms(defs::TT_MATCH_RESPONSE_MS + 1);
    return assert_true(not handler.awaiting_cnf(), test_name, "reset must drop the outstanding REQ") and
           assert_true(not handler.retransmit_due(h.ctx.current_time), test_name, "nothing due after reset") and
           assert_true(handler.retries() == 0, test_name, "reset must clear the retry counter");
}

} // namespace

int main() {
    const auto tests = std::array<std::pair<const char*, bool (*)()>, 5>{
        std::make_pair("test_no_request_when_not_configured", test_no_request_when_not_configured),
        std::make_pair("test_start_sends_request_and_awaits_cnf", test_start_sends_request_and_awaits_cnf),
        std::make_pair("test_retransmits_after_interval_then_gives_up", test_retransmits_after_interval_then_gives_up),
        std::make_pair("test_cnf_stops_retransmission", test_cnf_stops_retransmission),
        std::make_pair("test_reset_forgets_exchange", test_reset_forgets_exchange),
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
