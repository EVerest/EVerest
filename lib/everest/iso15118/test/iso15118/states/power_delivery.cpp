// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/state/power_delivery.hpp>

using namespace iso15118;

namespace dt = message_20::datatypes;

namespace {

d20::Session scheduled_session() {
    return d20::Session(
        d20::SelectedServiceParameters(dt::ServiceCategory::AC, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
                                       dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230));
}

message_20::PowerDeliveryRequest scheduled_start_request(const d20::Session& session,
                                                         dt::PowerToleranceAcceptance tolerance) {
    message_20::PowerDeliveryRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.processing = dt::Processing::Finished;
    req.charge_progress = dt::Progress::Start;

    auto& profile = req.power_profile.emplace();
    profile.time_anchor = 1691411798'000'000;
    auto& mode = profile.control_mode.emplace<dt::Scheduled_EVPPTControlMode>();
    mode.selected_schedule = 1;
    mode.power_tolerance_acceptance = tolerance;
    dt::PowerScheduleEntry entry{};
    entry.duration = 60;
    entry.power = {10, 3};
    profile.entries.push_back(entry);
    return req;
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
    GIVEN("Good case") {
    }
    GIVEN("Bad case - EVPowerProfileInvalid") {
    }
    GIVEN("Bad case - ScheduleSelectionInvalid") {
    }
    GIVEN("Bad case - PowerDeliveryNotApplied") {
    } // TODO(sl): evse is not able to deliver energy

    GIVEN("Bad case - FAILED_PowerToleranceNotConfirmed") {
        const auto session = scheduled_session();
        const auto req = scheduled_start_request(session, dt::PowerToleranceAcceptance::NotConfirmed);

        const auto res = d20::state::handle_request(req, session, false, false);

        THEN("ResponseCode: FAILED_PowerToleranceNotConfirmed, EVSENotification: Terminate [V2G20-1945][V2G20-1402]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_PowerToleranceNotConfirmed);
            REQUIRE(res.status.has_value());
            REQUIRE(res.status->notification == dt::EvseNotification::Terminate);
        }
    }
    GIVEN("Good case - OK_PowerToleranceConfirmed") {
        const auto session = scheduled_session();
        const auto req = scheduled_start_request(session, dt::PowerToleranceAcceptance::Confirmed);

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
