// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

#include <iso15118/d20/pause_notification.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/session/feedback.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

d20::Session dynamic_session() {
    return d20::Session(d20::SelectedServiceParameters(dt::ServiceCategory::AC, dt::AcConnector::ThreePhase,
                                                       dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc,
                                                       dt::Pricing::NoPricing, 230));
}

} // namespace

SCENARIO("The SECC's pause request in a charge loop") {
    std::vector<session::feedback::Signal> signals;
    session::feedback::Callbacks callbacks{};
    callbacks.signal = [&signals](session::feedback::Signal signal) { signals.push_back(signal); };
    const session::Feedback feedback{callbacks};

    d20::PauseNotification notification;

    GIVEN("A dynamic control mode session") {
        auto session = dynamic_session();

        WHEN("the pause is requested") {
            REQUIRE(notification.update(true, false, session, feedback));

            THEN("the response carries it and it is reported once") {
                REQUIRE(session.secc_pause_notified);
                REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::PAUSE_NOTIFIED) == 1);

                REQUIRE(notification.update(true, false, session, feedback));
                REQUIRE(std::count(signals.begin(), signals.end(), session::feedback::Signal::PAUSE_NOTIFIED) == 1);
            }

            AND_WHEN("a stop is requested on top of it") {
                REQUIRE_FALSE(notification.update(true, true, session, feedback));

                THEN("the stop takes the pause's place in the response, but the EV may still pause [V2G20-1195]") {
                    REQUIRE(session.secc_pause_notified);
                }
            }

            AND_WHEN("the pause request is withdrawn") {
                REQUIRE_FALSE(notification.update(false, false, session, feedback));

                THEN("the EV may no longer pause") {
                    REQUIRE_FALSE(session.secc_pause_notified);
                }
            }
        }

        WHEN("only a stop is requested") {
            REQUIRE_FALSE(notification.update(false, true, session, feedback));

            THEN("no pause was notified") {
                REQUIRE_FALSE(session.secc_pause_notified);
                REQUIRE(signals.empty());
            }
        }
    }

    GIVEN("A scheduled control mode session whose applied EVPowerProfileEntry is not 0 kW") {
        d20::Session session(d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));
        session.ev_power_profile = d20::PowerTimeline{0, {{std::numeric_limits<uint32_t>::max(), 11000.0f}}};

        THEN("the pause request is held back [V2G20-1198]") {
            REQUIRE_FALSE(notification.update(true, false, session, feedback));
            REQUIRE_FALSE(session.secc_pause_notified);
            REQUIRE(signals.empty());
        }
    }
}
