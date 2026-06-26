// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

#include <arpa/inet.h>

#include <cbv2g/exi_v2gtp.h>

#include <everest/io/event/fd_event_handler.hpp>

#include <iso15118/io/sdp.hpp>
#include <iso15118/io/sdp_packet.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/authorization.hpp>
#include <iso15118/message/authorization_setup.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/supported_app_protocol.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/variant.hpp>

#include <iso15118/ev/d20/control_event.hpp>
#include <iso15118/ev/session.hpp>
#include <iso15118/ev/session/feedback.hpp>

#include "test_support.hpp"

using namespace iso15118;
using namespace std::chrono_literals;
using namespace iso15118::ev::test;

SCENARIO("EV Session stops loudly when the outbound send is refused") {
    everest::lib::io::event::fd_event_handler reactor;

    int send_attempts = 0;
    bool timed_out = false;

    ev::feedback::Callbacks callbacks{};
    callbacks.timed_out = [&timed_out]() { timed_out = true; };

    const ev::SessionTiming timing{5ms, 200ms};

    // Refuse every send.
    ev::Session session{callbacks,
                        [&send_attempts](std::vector<uint8_t>) {
                            ++send_attempts;
                            return false;
                        },
                        reactor, timing, "EVTESTID01"};
    GIVEN("A Session whose outbound seam refuses the SAP request") {
        session.start();

        WHEN("the send-delay elapses and the first frame is refused") {
            THEN("The session stops loudly the same pass, not via the response watchdog") {
                REQUIRE(pump_until(
                    reactor, [&]() { return session.is_finished(); }, 1s));
                // The send was attempted exactly once and refused.
                REQUIRE(send_attempts == 1);
                // Stopped via the send-failure path, not the 200ms watchdog.
                REQUIRE_FALSE(timed_out);
            }
        }
    }
}
