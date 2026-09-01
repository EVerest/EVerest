// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Every other SLAC test drives one machine and hand-crafts the peer's frames, so each side is only
// ever tested against messages written the way that side expects them. Here both real machines
// complete a matching process together, which is the only way a disagreement about what goes on
// the wire becomes visible. The expected sequence is ISO 15118-3 Annex A, Figure A.1.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "mock_clock.hpp"
#include "virtual_plc_link.hpp"

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

using namespace everest::slac;
using namespace everest::slac::test;

SCENARIO("An EV and an EVSE complete the ISO 15118-3 matching process") {

    GIVEN("an EV and an EVSE state machine joined by a virtual PLC link") {
        VirtualPLCLink link;
        Recorder rec;

        MockClock test_clock;

        evse::ContextCallbacks evse_callbacks{};
        evse_callbacks.now = test_clock.source();
        evse_callbacks.send_raw_slac = [&link](messages::HomeplugMessage& f) { return link.from_evse(f); };
        evse_callbacks.signal_state = [&rec](D3State s) { rec.evse_states.push_back(s); };
        evse_callbacks.signal_dlink_ready = [&rec](bool v) { rec.evse_dlink.push_back(v); };

        ev::ContextCallbacks ev_callbacks{};
        ev_callbacks.now = test_clock.source();
        ev_callbacks.send_raw_slac = [&link](messages::HomeplugMessage& f) { return link.from_ev(f); };
        ev_callbacks.signal_state = [&rec](D3State s) { rec.ev_states.push_back(to_string(s)); };
        ev_callbacks.signal_dlink_ready = [&rec](bool v) { rec.ev_dlink.push_back(v); };

        evse::Context evse_ctx(evse_callbacks);
        evse_ctx.evse_mac = EVSE_MAC;
        evse_ctx.slac_config.request_info_delay_ms = 1; // no modem answers the vendor probes
        evse_ctx.slac_config.chip_reset.enabled = false;
        evse_ctx.slac_config.link_status.do_detect = false;

        ev::Context ev_ctx(ev_callbacks, EV_MAC);

        evse::EvseFSM evse(evse_ctx);
        ev::EvFSM ev(ev_ctx);

        int max_session_count = 0;
        auto tick = [&] {
            link.deliver(evse, ev);
            evse.update();
            ev.update();
            max_session_count = std::max(max_session_count, evse_ctx.status.session_count);
            test_clock.advance(std::chrono::milliseconds(1));
        };

        evse.restart_fsm();
        ev.restart_fsm();

        WHEN("the control pilot enters state B and the EV starts matching") {
            // the EVSE probes for a modem, sets the NMK and settles in Idle first
            for (int i = 0; i < 200 && evse_ctx.status.match_state != SlacState::Idle; ++i) {
                tick();
            }
            REQUIRE(evse_ctx.status.match_state == SlacState::Idle);

            evse.enter_bcd();
            ev.trigger_matching();

            // TT_EVSE_match_MNBC is 600 ms and dominates; 2000 ticks is ample headroom
            for (int i = 0; i < 2000; ++i) {
                tick();
                if (evse_ctx.status.match_state == SlacState::Matched and not rec.ev_states.empty() and
                    rec.ev_states.back() == "MATCHED") {
                    break;
                }
            }

            THEN("the message sequence of ISO 15118-3 Annex A, Figure A.1 is exchanged") {
                std::vector<std::uint16_t> expected{defs::mmtype::SLAC_PARAM_REQ, defs::mmtype::SLAC_PARAM_CNF};
                expected.insert(expected.end(), defs::C_EV_START_ATTEN_CHAR_INDS, defs::mmtype::START_ATTEN_CHAR_IND);
                expected.insert(expected.end(), defs::C_EV_MATCH_MNBC, defs::mmtype::MNBC_SOUND_IND);
                expected.insert(expected.end(), {defs::mmtype::ATTEN_CHAR_IND, defs::mmtype::ATTEN_CHAR_RSP,
                                                 defs::mmtype::SLAC_MATCH_REQ, defs::mmtype::SLAC_MATCH_CNF});

                REQUIRE(link.over_the_air() == expected);
            }

            THEN("the EVSE's modem reported one attenuation profile per M-Sound") {
                // V2G3-A09-43: the profiles come from the EVSE's own module, not over the air
                REQUIRE(link.injected_atten_profiles() == defs::C_EV_MATCH_MNBC);
            }

            THEN("both sides report a matched data link") {
                // ISO 15118-3 9.1 / Figure 11
                REQUIRE(evse_ctx.status.match_state == SlacState::Matched);
                REQUIRE(evse_ctx.status.d3_state == D3State::Matched);
                REQUIRE(rec.evse_states.back() == D3State::Matched);
                REQUIRE(rec.ev_states.back() == "MATCHED");

                REQUIRE_FALSE(rec.evse_dlink.empty());
                REQUIRE(rec.evse_dlink.back() == true);
                REQUIRE_FALSE(rec.ev_dlink.empty());
                REQUIRE(rec.ev_dlink.back() == true);
            }

            THEN("exactly one matching session ran") {
                // sampled while running: the count is cleared again when Matching is left
                REQUIRE(max_session_count == 1);
                REQUIRE(evse_ctx.status.session_count == 0);
            }

            THEN("the EV received the NMK the EVSE generated") {
                // V2G3-A09-92. Comparing the two proves the key survived both encode and decode.
                REQUIRE(link.ev_set_key_reqs().size() == 1);

                auto const req = link.ev_set_key_reqs().front().payload_as<messages::cm_set_key_req>();
                REQUIRE(req.has_value());
                REQUIRE(std::equal(evse_ctx.slac_config.session_nmk.begin(), evse_ctx.slac_config.session_nmk.end(),
                                   std::begin(req->new_key)));
            }
        }
    }
}
