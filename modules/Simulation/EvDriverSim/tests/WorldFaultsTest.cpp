// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "main/WorldFaults.hpp"
#include <catch2/catch_test_macros.hpp>
#include <limits>

using module::WorldFaults;
namespace api = module::API_driver;
using types::ev_board_support::EvCpState;

TEST_CASE("EvDriverSim unplug clears only the shorted pilot fault", "[evdriver]") {
    for (const auto type : {api::WorldFault::CpErrorE, api::WorldFault::DiodeFail, api::WorldFault::RcdError}) {
        WorldFaults faults;
        faults.inject({type, 30.0f, {}}, true);
        faults.unplug();
        if (type == api::WorldFault::CpErrorE) {
            CHECK_FALSE(faults.active());
            CHECK(faults.on_car_set_cp(EvCpState::B) == EvCpState::B);
        } else {
            CHECK(faults.active() == type);
        }
    }
}

TEST_CASE("EvDriverSim rejects CP E while unplugged but permits diode and RCD faults", "[evdriver]") {
    WorldFaults faults;
    const auto out = faults.inject({api::WorldFault::CpErrorE, {}, {}}, false);
    CHECK(out.rejection == "unplugged");
    CHECK_FALSE(out.to_board_cp);
    CHECK_FALSE(faults.active());
    CHECK(faults.inject({api::WorldFault::DiodeFail, {}, {}}, false).diode_fail == true);
    faults.clear();
    CHECK(faults.inject({api::WorldFault::RcdError, 30.0f, {}}, false).rcd_mA == 30.0);
}

TEST_CASE("EvDriverSim holds CP E and restores the latest car command on clear", "[evdriver]") {
    WorldFaults faults;
    CHECK(faults.on_car_set_cp(EvCpState::B) == EvCpState::B);
    faults.inject({api::WorldFault::CpErrorE, {}, {}}, true);
    CHECK(faults.on_car_set_cp(EvCpState::C) == EvCpState::E);
    CHECK(faults.on_car_set_cp(EvCpState::B) == EvCpState::E);
    CHECK(faults.clear().to_board_cp == EvCpState::B);
    CHECK(faults.on_car_set_cp(EvCpState::C) == EvCpState::C);
}

TEST_CASE("EvDriverSim RCD injection requires a finite nonnegative current", "[evdriver]") {
    for (const auto current : {std::optional<float>{}, std::optional<float>{-1.0f},
                               std::optional<float>{std::numeric_limits<float>::infinity()}}) {
        WorldFaults faults;
        const auto out = faults.inject({api::WorldFault::RcdError, current, {}}, true);
        CHECK(out.rejection.has_value());
        CHECK_FALSE(out.rcd_mA);
        CHECK_FALSE(faults.active());
    }
}

TEST_CASE("EvDriverSim rejects a second fault without changing the active fault", "[evdriver]") {
    WorldFaults faults;
    faults.inject({api::WorldFault::DiodeFail, {}, {}}, true);
    const auto out = faults.inject({api::WorldFault::CpErrorE, {}, {}}, true);
    CHECK(out.rejection.has_value());
    CHECK_FALSE(out.diode_fail);
    CHECK_FALSE(out.rcd_mA);
    CHECK_FALSE(out.to_board_cp);
    CHECK(faults.active() == api::WorldFault::DiodeFail);
    faults.clear();
    CHECK_FALSE(faults.inject({api::WorldFault::CpErrorE, {}, {}}, true).rejection);
}

TEST_CASE("EvDriverSim CP E commands the board and clears to the car state", "[evdriver]") {
    WorldFaults faults;
    faults.on_car_set_cp(EvCpState::C);
    const auto injected = faults.inject({api::WorldFault::CpErrorE, {}, {}}, true);
    CHECK(injected.to_board_cp == EvCpState::E);
    CHECK_FALSE(injected.diode_fail);
    CHECK_FALSE(injected.rcd_mA);
    CHECK(faults.active() == api::WorldFault::CpErrorE);
    CHECK(faults.clear().to_board_cp == EvCpState::C);
    CHECK_FALSE(faults.active());
}

TEST_CASE("EvDriverSim injects and clears RCD current", "[evdriver]") {
    WorldFaults faults;
    const auto injected = faults.inject({api::WorldFault::RcdError, 42.0f, {}}, true);
    CHECK(injected.rcd_mA == 42.0);
    CHECK_FALSE(injected.diode_fail);
    CHECK_FALSE(injected.rejection);
    CHECK(faults.active() == api::WorldFault::RcdError);
    CHECK(faults.clear().rcd_mA == 0.0);
    CHECK_FALSE(faults.active());
}

TEST_CASE("EvDriverSim injects and clears a diode fault", "[evdriver]") {
    WorldFaults faults;
    const auto injected = faults.inject({api::WorldFault::DiodeFail, {}, {}}, true);
    CHECK(injected.diode_fail == true);
    CHECK_FALSE(injected.rejection);
    CHECK_FALSE(injected.rcd_mA);
    CHECK_FALSE(injected.to_board_cp);
    CHECK(faults.active() == api::WorldFault::DiodeFail);
    const auto cleared = faults.clear();
    CHECK(cleared.diode_fail == false);
    CHECK_FALSE(faults.active());
}

TEST_CASE("EvDriverSim world diode fault holds and restores the car value", "[evdriver]") {
    WorldFaults faults;
    CHECK(faults.on_car_diode_fail(true).diode_fail == true);
    faults.inject({api::WorldFault::DiodeFail, {}, {}}, true);
    CHECK(faults.clear().diode_fail == true);
    faults.inject({api::WorldFault::DiodeFail, {}, {}}, true);
    CHECK_FALSE(faults.on_car_diode_fail(false).diode_fail);
    CHECK(faults.active() == api::WorldFault::DiodeFail);
    CHECK(faults.clear().diode_fail == false);
    faults.inject({api::WorldFault::RcdError, 30.0f, {}}, true);
    CHECK(faults.on_car_diode_fail(true).diode_fail == true);
}

TEST_CASE("EvDriverSim world RCD fault holds and restores the car current", "[evdriver]") {
    WorldFaults faults;
    CHECK(faults.on_car_set_rcd_error(12.0).rcd_mA == 12.0);
    faults.inject({api::WorldFault::RcdError, 30.0f, {}}, true);
    CHECK(faults.clear().rcd_mA == 12.0);
    faults.inject({api::WorldFault::RcdError, 30.0f, {}}, true);
    CHECK_FALSE(faults.on_car_set_rcd_error(0.0).rcd_mA);
    CHECK(faults.active() == api::WorldFault::RcdError);
    CHECK(faults.clear().rcd_mA == 0.0);
    faults.inject({api::WorldFault::DiodeFail, {}, {}}, true);
    CHECK(faults.on_car_set_rcd_error(7.0).rcd_mA == 7.0);
}
