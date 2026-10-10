// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/state/power_delivery.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

constexpr uint64_t ANCHOR_S = 1691411798;
constexpr uint64_t ANCHOR_US = ANCHOR_S * d20::MICROSECONDS_PER_SECOND;

// Tuple 1: 11 kW for the first hour, then 0 W for another hour.
d20::Session scheduled_session_offering_tuple_1() {
    d20::Session session(
        d20::SelectedServiceParameters(dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));
    session.offered_schedules.emplace(1, d20::PowerTimeline{ANCHOR_S, {{3600, 11000.0f}, {3600, 0.0f}}});
    return session;
}

message_20::PowerDeliveryRequest scheduled_start_request(const d20::Session& session, dt::NumericId tuple_id,
                                                         dt::RationalNumber power,
                                                         std::optional<dt::PowerToleranceAcceptance> tolerance = {}) {
    message_20::PowerDeliveryRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Finished;
    req.charge_progress = dt::Progress::Start;

    auto& profile = req.power_profile.emplace();
    profile.time_anchor = ANCHOR_US;
    auto& mode = profile.control_mode.emplace<dt::Scheduled_EVPPTControlMode>();
    mode.selected_schedule = tuple_id;
    mode.power_tolerance_acceptance = tolerance;
    dt::PowerScheduleEntry entry{};
    entry.duration = 60;
    entry.power = power;
    profile.entries.push_back(entry);
    return req;
}

dt::PowerScheduleEntry profile_entry(uint32_t duration_s, dt::RationalNumber power) {
    dt::PowerScheduleEntry entry{};
    entry.duration = duration_s;
    entry.power = power;
    return entry;
}

} // namespace

SCENARIO("Power delivery state handling") {
    GIVEN("Bad case - Unknown session") {
        d20::Session session = d20::Session();

        message_20::PowerDeliveryRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Start;

        const auto res = d20::state::handle_request(req, d20::Session(), false, false);

        THEN("ResponseCode: FAILED_UnknownSession, mandatory fields should be set") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.status.has_value() == false);
        }
    }
    GIVEN("Not so bad case - WARNING_StandbyNotAllowed") {
        d20::Session session = d20::Session();

        message_20::PowerDeliveryRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Standby;

        const auto res = d20::state::handle_request(req, session, false, false);

        // Right now standby ist not supported

        THEN("ResponseCode: WARNING_StandbyNotAllowed, mandatory fields should be set") {
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_StandbyNotAllowed);
            REQUIRE(res.status.has_value() == false);
        }
    }
    GIVEN("Good case - scheduled EVPowerProfile within the offered schedule") {
        const auto session = scheduled_session_offering_tuple_1();
        const auto req = scheduled_start_request(session, 1, {10, 3});

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Bad case - EVPowerProfileInvalid") {
        const auto session = scheduled_session_offering_tuple_1();
        const auto req = scheduled_start_request(session, 1, {30000, 2});

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: FAILED_EVPowerProfileInvalid [V2G20-478]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_EVPowerProfileInvalid);
        }
    }
    GIVEN("Scheduled EVPowerProfile against a schedule that drops to 0 W after an hour [V2G20-1559]") {
        const auto session = scheduled_session_offering_tuple_1();
        auto req = scheduled_start_request(session, 1, {11, 3});
        req.power_profile->entries = {profile_entry(3600, {11, 3}), profile_entry(3600, {0, 0})};

        WHEN("the profile follows the offered power per slot") {
            const auto res = d20::state::handle_request(req, session, false, false);
            THEN("ResponseCode: OK") {
                REQUIRE(res.response_code == dt::ResponseCode::OK);
            }
        }

        WHEN("a later entry exceeds the 0 W slot although it stays below the schedule's maximum") {
            req.power_profile->entries = {profile_entry(3600, {11, 3}), profile_entry(3600, {5, 3})};
            const auto res = d20::state::handle_request(req, session, false, false);
            THEN("ResponseCode: FAILED_EVPowerProfileInvalid") {
                REQUIRE(res.response_code == dt::ResponseCode::FAILED_EVPowerProfileInvalid);
            }
        }

        WHEN("a single entry spans both slots at 11 kW") {
            req.power_profile->entries = {profile_entry(7200, {11, 3})};
            const auto res = d20::state::handle_request(req, session, false, false);
            THEN("ResponseCode: FAILED_EVPowerProfileInvalid") {
                REQUIRE(res.response_code == dt::ResponseCode::FAILED_EVPowerProfileInvalid);
            }
        }

        WHEN("the profile extends past the offered schedule") {
            req.power_profile->time_anchor = (ANCHOR_S + 7200) * d20::MICROSECONDS_PER_SECOND;
            req.power_profile->entries = {profile_entry(3600, {22, 3})};
            const auto res = d20::state::handle_request(req, session, false, false);
            THEN("time the schedule does not cover is unconstrained: ResponseCode: OK") {
                REQUIRE(res.response_code == dt::ResponseCode::OK);
            }
        }
    }
    GIVEN("Bad case - ScheduleSelectionInvalid") {
        const auto session = scheduled_session_offering_tuple_1();
        const auto req = scheduled_start_request(session, 2, {10, 3});

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: FAILED_ScheduleSelectionInvalid [V2G20-479]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ScheduleSelectionInvalid);
        }
    }
    GIVEN("Good case - a dynamic EVPowerProfile is never checked against a schedule") {
        d20::Session session(d20::SelectedServiceParameters(
            dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Dynamic,
            dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));
        auto req = scheduled_start_request(session, 1, {30000, 2});
        req.power_profile->control_mode.emplace<dt::Dynamic_EVPPTControlMode>();

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: OK [V2G20-1070]") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
    GIVEN("Bad case - PowerDeliveryNotApplied") {
    } // TODO(sl): evse is not able to deliver energy

    GIVEN("Not so bad case - WARNING_PowerToleranceNotConfirmed") {
        const auto session = scheduled_session_offering_tuple_1();
        const auto req = scheduled_start_request(session, 1, {10, 3}, dt::PowerToleranceAcceptance::NotConfirmed);

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: WARNING_PowerToleranceNotConfirmed, the session goes on [V2G20-1946]") {
            REQUIRE(res.response_code == dt::ResponseCode::WARNING_PowerToleranceNotConfirmed);
            REQUIRE(res.status.has_value() == false);
        }
    }
    GIVEN("Good case - OK_PowerToleranceConfirmed") {
        const auto session = scheduled_session_offering_tuple_1();
        const auto req = scheduled_start_request(session, 1, {10, 3}, dt::PowerToleranceAcceptance::Confirmed);

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: OK_PowerToleranceConfirmed [V2G20-1944]") {
            REQUIRE(res.response_code == dt::ResponseCode::OK_PowerToleranceConfirmed);
        }
    }
    GIVEN("Bad case - AC ContactorError") {
        d20::Session session = d20::Session();

        message_20::PowerDeliveryRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Start;

        const auto res = d20::state::handle_request(req, session, true, false);

        THEN("ResponseCode: FAILED_ContactorError, mandatory fields should be set") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ContactorError);
            REQUIRE(res.status.has_value() == false);
        }
    } // TODO(sl): AC stuff

    GIVEN("Good case - shutdown requested") {
        d20::Session session = d20::Session();

        message_20::PowerDeliveryRequest req;
        req.header.session_id = session.get_id();
        req.header.timestamp = 1691411798;

        req.processing = dt::Processing::Ongoing;
        req.charge_progress = dt::Progress::Start;

        const auto res = d20::state::handle_request(req, session, false, true);

        THEN("ResponseCode: OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.has_value());
            REQUIRE(res.status.value().notification == message_20::datatypes::EvseNotification::Terminate);
        }
    }

    // GIVEN("Bad Case - sequence error") {} // TODO(sl): not here

    // GIVEN("Bad Case - Performance Timeout") {} // TODO(sl): not here

    // GIVEN("Bad Case - Sequence Timeout") {} // TODO(sl): not here
}
