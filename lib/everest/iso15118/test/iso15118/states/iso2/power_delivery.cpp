// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;

namespace {
dt::SAScheduleList make_schedule(double pmax_w) {
    dt::SAScheduleList list;
    dt::SAScheduleTuple tuple;
    tuple.sa_schedule_tuple_id = 1;
    dt::PMaxScheduleEntry entry;
    entry.start = 0;
    entry.p_max = dt::to_physical_value(pmax_w, dt::Unit::W);
    tuple.pmax_schedule.push_back(entry);
    list.push_back(tuple);
    return list;
}
} // namespace

SCENARIO("ISO 15118-2 SECC PowerDelivery handling") {
    const dt::SessionId id{};
    const auto schedule = make_schedule(150000.0);

    GIVEN("A DC Start with a matching SAScheduleTupleID") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, true, 1, true, false, schedule);
        THEN("OK with DC_EVSEStatus") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->isolation_status.value() == dt::IsolationLevel::Valid);
        }
    }

    GIVEN("A mismatched SAScheduleTupleID") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 5;
        const auto res = d2::state::handle_request(req, id, true, 1, true, false, schedule);
        THEN("FAILED_TariffSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_TariffSelectionInvalid);
        }
    }

    GIVEN("An AC Start without a ChargingProfile") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, false, 1, false, false, schedule);
        THEN("FAILED_ChargingProfileInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ChargingProfileInvalid);
        }
    }

    GIVEN("An AC Start with a valid ChargingProfile") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        auto& profile = req.charging_profile.emplace();
        dt::ProfileEntry entry;
        entry.start = 0;
        entry.max_power = dt::to_physical_value(11000.0, dt::Unit::W);
        profile.profile_entry.push_back(entry);
        const auto res = d2::state::handle_request(req, id, false, 1, false, false, schedule);
        THEN("OK with AC_EVSEStatus") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.ac_evse_status.has_value());
        }
    }

    GIVEN("A DC Start with a ChargingProfile exceeding the advertised PMax") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        auto& profile = req.charging_profile.emplace();
        dt::ProfileEntry entry;
        entry.start = 0;
        entry.max_power = dt::to_physical_value(200000.0, dt::Unit::W); // > 150 kW
        profile.profile_entry.push_back(entry);
        const auto res = d2::state::handle_request(req, id, true, 1, true, false, schedule);
        THEN("FAILED_ChargingProfileInvalid [V2G2-224/225]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ChargingProfileInvalid);
        }
    }

    GIVEN("A DC Start while a charger stop is pending") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, true, 1, true, true, schedule);
        THEN("The response signals EVSENotification::StopCharging and EVSE_Shutdown") {
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_status->status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }
}
