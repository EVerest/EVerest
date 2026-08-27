// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/din/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_din::datatypes;

SCENARIO("DIN SECC PowerDelivery state handling") {
    const dt::SessionId session{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

    GIVEN("A power delivery request (ready to charge)") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = session;
        req.ready_to_charge_state = true;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is OK and the DC EVSE status is present") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->evse_status_code == dt::DcEvseStatusCode::EVSE_Ready);
        }
    }

    GIVEN("A mismatching session id") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = dt::SessionId{};

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is FAILED_UnknownSession") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }

    GIVEN("A charging profile referencing an unoffered SAScheduleTupleID") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = session;
        req.ready_to_charge_state = true;
        dt::ChargingProfile profile;
        profile.sa_schedule_tuple_id = 2; // the SECC only offered tuple id 1
        profile.profile_entries.push_back({0, 0});
        req.charging_profile = profile;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is FAILED_TariffSelectionInvalid [V2G-DC-400]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_TariffSelectionInvalid);
            REQUIRE(res.dc_evse_status.has_value());
        }
    }

    GIVEN("A charging profile whose entry starts past the offered schedule") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = session;
        req.ready_to_charge_state = true;
        dt::ChargingProfile profile;
        profile.sa_schedule_tuple_id = 1;
        profile.profile_entries.push_back({86401, 0}); // > 86400 s schedule duration
        req.charging_profile = profile;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is FAILED_ChargingProfileInvalid [V2G-DC-399]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ChargingProfileInvalid);
        }
    }

    GIVEN("A valid charging profile (offered tupleID, in-schedule entry)") {
        message_din::PowerDeliveryRequest req;
        req.header.session_id = session;
        req.ready_to_charge_state = true;
        dt::ChargingProfile profile;
        profile.sa_schedule_tuple_id = 1;
        profile.profile_entries.push_back({0, 0});
        req.charging_profile = profile;

        const auto res = din::state::handle_request(req, session);
        THEN("ResponseCode is OK") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
        }
    }
}
