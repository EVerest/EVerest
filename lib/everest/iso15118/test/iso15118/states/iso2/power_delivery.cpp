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
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, false, schedule);
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
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, false, schedule);
        THEN("FAILED_TariffSelectionInvalid") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_TariffSelectionInvalid);
        }
    }

    GIVEN("An AC Start without a ChargingProfile") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, false, 1, dt::IsolationLevel::Invalid, false, schedule);
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
        const auto res = d2::state::handle_request(req, id, false, 1, dt::IsolationLevel::Invalid, false, schedule);
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
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, false, schedule);
        THEN("FAILED_ChargingProfileInvalid [V2G2-224/225]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_ChargingProfileInvalid);
        }
    }

    GIVEN("A DC Start while a charger stop is pending") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, true, schedule);
        THEN("The response signals EVSENotification::StopCharging and EVSE_Shutdown, and still starts") {
            // A stop REQUEST is not an inability to deliver energy: [V2G2-679] leaves the reaction to the
            // EV, and the STOP_CHARGING guard enforces it once the grace window closes. So no
            // FAILED_PowerDeliveryNotApplied here, unlike EvseV2G's "status != EVSE_Ready" formulation.
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->notification == dt::EVSENotification::StopCharging);
            REQUIRE(res.dc_evse_status->status_code == dt::DC_EVSEStatusCode::EVSE_Shutdown);
        }
    }

    // [V2G2-366] the module-reported EVSE error (send_error) is reported as the DC EVSEStatusCode of every
    // DC response, PowerDeliveryRes included, and [V2G2-480] refuses a Start while it stands.
    GIVEN("A DC Start while the module reports a malfunction") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, false, schedule,
                                                   dt::DC_EVSEStatusCode::EVSE_Malfunction);
        THEN("FAILED_PowerDeliveryNotApplied with EVSE_Malfunction [V2G2-480]") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_PowerDeliveryNotApplied);
            REQUIRE(res.dc_evse_status.has_value());
            REQUIRE(res.dc_evse_status->status_code == dt::DC_EVSEStatusCode::EVSE_Malfunction);
        }
    }

    GIVEN("A DC Stop while the module reports a utility interrupt") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Stop;
        req.sa_schedule_tuple_id = 1;
        const auto res = d2::state::handle_request(req, id, true, 1, dt::IsolationLevel::Valid, false, schedule,
                                                   dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        THEN("The status code is reported but the stop is accepted") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.dc_evse_status->status_code == dt::DC_EVSEStatusCode::EVSE_UtilityInterruptEvent);
        }
    }

    GIVEN("An AC Start while the module reports an RCD error") {
        message_2::PowerDeliveryRequest req;
        req.charge_progress = dt::ChargeProgress::Start;
        req.sa_schedule_tuple_id = 1;
        req.charging_profile.emplace();
        const auto res = d2::state::handle_request(req, id, false, 1, dt::IsolationLevel::Valid, false, schedule,
                                                   std::nullopt, /*rcd_error=*/true);
        THEN("The AC status carries the RCD flag; an RCD error has no status code to refuse the Start") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.ac_evse_status.has_value());
            REQUIRE(res.ac_evse_status->rcd == true);
        }
    }
}
