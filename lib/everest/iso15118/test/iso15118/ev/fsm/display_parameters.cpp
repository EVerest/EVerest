// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/d20/state/ac_charge_loop.hpp>
#include <iso15118/ev/d20/state/dc_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/detail/d20/display_parameters.hpp>
#include <iso15118/message/ac_charge_loop.hpp>
#include <iso15118/message/common_types.hpp>
#include <iso15118/message/dc_charge_loop.hpp>
#include <iso15118/message/power_delivery.hpp>

using namespace iso15118;

namespace {
using message_20::datatypes::ResponseCode;

ev::params::DcChargeParams dc_with_soc(double soc, float capacity_wh) {
    ev::params::DcChargeParams params{};
    params.max_charge_power = 11000.0F;
    params.max_charge_current = 32.0F;
    params.max_voltage = 250.0F;
    params.min_voltage = 200.0F;
    params.present_voltage = 230.0F;
    params.energy_capacity = capacity_wh;
    params.present_soc = soc;
    params.present_soc_known = true;
    return params;
}

const auto by_power_delivery = entered_by([](FsmStateHelper& helper) {
    return successor_of<ev::d20::state::PowerDelivery>(
        helper, message_20::PowerDeliveryResponse{SESSION_HEADER, ResponseCode::OK, std::nullopt},
        ev::d20::state::PowerDelivery::Start{});
});
} // namespace

SCENARIO("ISO15118-20 EV DisplayParameters follow the reported SoC") {
    GIVEN("No SoC reported") {
        ev::params::DcChargeParams params{};
        params.present_soc = 55.0;
        THEN("there are none") {
            REQUIRE_FALSE(ev::d20::make_display_parameters(params).has_value());
        }
    }

    GIVEN("A reported SoC and a battery capacity") {
        const auto display = ev::d20::make_display_parameters(dc_with_soc(67.4, 5000.0F));
        THEN("they carry the rounded SoC, the capacity and an incomplete charge") {
            REQUIRE(display.has_value());
            REQUIRE(display->present_soc == 67);
            REQUIRE(display->charging_complete == false);
            REQUIRE(display->battery_energy_capacity.has_value());
            REQUIRE(message_20::datatypes::from_RationalNumber(*display->battery_energy_capacity) ==
                    Catch::Approx(5000.0F));
            REQUIRE_FALSE(display->target_soc.has_value());
        }
    }

    GIVEN("A full battery without a capacity") {
        const auto display = ev::d20::make_display_parameters(dc_with_soc(100.0, 0.0F));
        THEN("the charge is complete and no capacity is sent") {
            REQUIRE(display.has_value());
            REQUIRE(display->present_soc == 100);
            REQUIRE(display->charging_complete == true);
            REQUIRE_FALSE(display->battery_energy_capacity.has_value());
        }
    }

    GIVEN("An out-of-range SoC") {
        THEN("it is clamped to 0-100") {
            REQUIRE(ev::d20::make_display_parameters(dc_with_soc(130.0, 0.0F))->present_soc == 100);
            REQUIRE(ev::d20::make_display_parameters(dc_with_soc(-3.0, 0.0F))->present_soc == 0);
        }
    }
}

SCENARIO("ISO15118-20 EV DC_ChargeLoopRequest carries the reported SoC") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed = [](FsmStateHelper& helper) { helper.set_dc_params(dc_with_soc(42.0, 5000.0F)); };
    PrimedState<ev::d20::state::DC_ChargeLoop> primed{callbacks, seed, by_power_delivery};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::DC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->display_parameters.has_value());
    REQUIRE(request_message->display_parameters->present_soc == 42);
    REQUIRE(message_20::datatypes::from_RationalNumber(request_message->present_voltage) == Catch::Approx(230.0F));
}

SCENARIO("ISO15118-20 EV AC_ChargeLoopRequest carries the reported SoC") {
    const ev::feedback::Callbacks callbacks{};
    const auto seed = [](FsmStateHelper& helper) {
        ev::params::AcChargeParams ac{};
        ac.phase_count = 1;
        ac.max_charge_power = 3600.0F;
        ac.min_charge_power = 1000.0F;
        ac.present_active_power = 2000.0F;
        helper.set_ac_params(ac);
        helper.set_dc_params(dc_with_soc(81.0, 5000.0F));
    };
    PrimedState<ev::d20::state::AC_ChargeLoop> primed{callbacks, message_20::datatypes::ServiceCategory::AC, seed,
                                                      by_power_delivery};

    const auto requests = primed.take_requests();
    const auto request_message = requests.get<message_20::AC_ChargeLoopRequest>();
    REQUIRE(request_message.has_value());
    REQUIRE(request_message->display_parameters.has_value());
    REQUIRE(request_message->display_parameters->present_soc == 81);
    REQUIRE(request_message->display_parameters->charging_complete == false);
}
