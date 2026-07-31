// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d2/ev/state/power_delivery.hpp>

using namespace iso15118;
namespace dt = message_2::datatypes;
namespace power_delivery = d2::ev::state::power_delivery;

SCENARIO("EVCC ISO-2 PowerDelivery request construction") {

    GIVEN("A DC start request") {
        dt::DC_EVStatus status;
        status.ev_ready = true;
        status.ev_ress_soc = 75;
        const auto req = power_delivery::create_dc_request(dt::ChargeProgress::Start, 3, status, false);
        THEN("It echoes the schedule tuple id, SoC and carries an incomplete DC power delivery parameter") {
            REQUIRE(req.charge_progress == dt::ChargeProgress::Start);
            REQUIRE(req.sa_schedule_tuple_id == 3);
            REQUIRE(req.dc_ev_power_delivery_parameter.has_value());
            REQUIRE(req.dc_ev_power_delivery_parameter->dc_ev_status.ev_ress_soc == 75);
            REQUIRE(not req.dc_ev_power_delivery_parameter->charging_complete);
            REQUIRE(not req.charging_profile.has_value());
        }
    }

    GIVEN("A DC stop request") {
        const auto req = power_delivery::create_dc_request(dt::ChargeProgress::Stop, 3, dt::DC_EVStatus{}, true);
        THEN("charging_complete is set") {
            REQUIRE(req.charge_progress == dt::ChargeProgress::Stop);
            REQUIRE(req.dc_ev_power_delivery_parameter->charging_complete);
        }
    }

    GIVEN("A captured multi-entry PMax schedule") {
        everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12> schedule;
        dt::PMaxScheduleEntry entry;
        entry.start = 42;
        entry.p_max = dt::to_physical_value(11000.0, dt::Unit::W);
        schedule.push_back(entry);
        dt::PMaxScheduleEntry entry2;
        entry2.start = 3642;
        entry2.p_max = dt::to_physical_value(4000.0, dt::Unit::W);
        schedule.push_back(entry2);

        const auto profile = power_delivery::build_charging_profile(schedule, 7000.0f);
        THEN("The ChargingProfile spans every PMaxSchedule entry") {
            REQUIRE(profile.profile_entry.size() == 2);
            REQUIRE(profile.profile_entry[0].start == 42);
            REQUIRE(dt::from_physical_value(profile.profile_entry[0].max_power) == 11000.0);
            REQUIRE(profile.profile_entry[1].start == 3642);
            REQUIRE(dt::from_physical_value(profile.profile_entry[1].max_power) == 4000.0);
        }
    }

    GIVEN("An empty PMax schedule") {
        const auto profile = power_delivery::build_charging_profile(
            everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12>{}, 7000.0f);
        THEN("A single fallback entry is produced") {
            REQUIRE(profile.profile_entry.size() == 1);
            REQUIRE(profile.profile_entry[0].start == 0);
            REQUIRE(dt::from_physical_value(profile.profile_entry[0].max_power) == 7000.0);
        }
    }

    GIVEN("An AC start request with a mandatory ChargingProfile") {
        auto profile = power_delivery::build_charging_profile(
            everest::lib::util::fixed_vector<dt::PMaxScheduleEntry, 12>{}, 7000.0f);
        const auto req = power_delivery::create_ac_request(dt::ChargeProgress::Start, 2, std::move(profile));
        THEN("It echoes the schedule tuple id and carries the ChargingProfile") {
            REQUIRE(req.charge_progress == dt::ChargeProgress::Start);
            REQUIRE(req.sa_schedule_tuple_id == 2);
            REQUIRE(req.charging_profile.has_value());
            REQUIRE(not req.dc_ev_power_delivery_parameter.has_value());
        }
    }

    GIVEN("An OK response") {
        message_2::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::OK;
        THEN("It is valid") {
            REQUIRE(power_delivery::handle_response(res).valid);
        }
    }

    GIVEN("A failed response") {
        message_2::PowerDeliveryResponse res;
        res.response_code = dt::ResponseCode::FAILED_PowerDeliveryNotApplied;
        THEN("It is invalid") {
            REQUIRE(not power_delivery::handle_response(res).valid);
        }
    }
}
