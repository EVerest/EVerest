// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <atomic>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "helper.hpp"

#include <iso15118/ev/dc_charge_params.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV DcChargeParams channel exposes a consistent snapshot via the Context") {

    GIVEN("A monitor initialized with static DcChargeParams wired into a Context") {
        ev::DcChargeParams initial{};
        initial.max_charge_power = 11000.0f;
        initial.max_charge_current = 200.0f;
        initial.max_voltage = 500.0f;
        initial.min_voltage = 150.0f;
        initial.energy_capacity = 60000.0f;
        initial.target_voltage = 400.0f;
        initial.target_current = 125.0f;
        initial.present_soc = 42.0;
        initial.present_voltage = 380.0f;

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};
        helper.set_dc_params(initial);
        auto& mon = helper.get_dc_params_monitor();
        auto& ctx = helper.get_context();

        WHEN("The static params are read back through the Context getter") {
            const auto snapshot = ctx.get_dc_params();

            THEN("Every static field matches the initial params") {
                REQUIRE(snapshot.max_charge_power == initial.max_charge_power);
                REQUIRE(snapshot.max_charge_current == initial.max_charge_current);
                REQUIRE(snapshot.max_voltage == initial.max_voltage);
                REQUIRE(snapshot.min_voltage == initial.min_voltage);
                REQUIRE(snapshot.energy_capacity == initial.energy_capacity);
                REQUIRE(snapshot.target_voltage == initial.target_voltage);
                REQUIRE(snapshot.target_current == initial.target_current);
                REQUIRE(snapshot.present_soc == initial.present_soc);
                REQUIRE(snapshot.present_voltage == initial.present_voltage);
            }
        }

        WHEN("A live field is mutated through the monitor handle (as update_present_* would)") {
            {
                auto h = mon.handle();
                (*h).present_soc = 77.0;
                (*h).present_voltage = 410.0f;
            }

            THEN("A fresh Context snapshot reflects the new live values") {
                const auto snapshot = ctx.get_dc_params();
                REQUIRE(snapshot.present_soc == 77.0);
                REQUIRE(snapshot.present_voltage == 410.0f);

                AND_THEN("The static fields are unchanged") {
                    REQUIRE(snapshot.max_charge_power == initial.max_charge_power);
                    REQUIRE(snapshot.target_current == initial.target_current);
                }
            }
        }
    }
}

SCENARIO("ISO15118-20 EV DcChargeParams channel is torn-read-free under concurrent access") {

    GIVEN("A monitor wired into a Context and a paired-value writer invariant") {
        // The writer keeps present_voltage == present_soc * 10. A torn read would
        // observe a snapshot where the invariant is broken; the lock must prevent it.
        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};
        auto& mon = helper.get_dc_params_monitor();
        auto& ctx = helper.get_context();

        WHEN("One thread writes paired live values while another reads snapshots") {
            std::atomic_bool stop{false};
            std::atomic_bool invariant_held{true};

            std::thread writer([&]() {
                for (double soc = 0.0; not stop.load(); soc += 1.0) {
                    if (soc > 100.0) {
                        soc = 0.0;
                    }
                    auto h = mon.handle();
                    (*h).present_soc = soc;
                    (*h).present_voltage = static_cast<float>(soc * 10.0);
                }
            });

            for (int i = 0; i < 100000; ++i) {
                const auto snapshot = ctx.get_dc_params();
                if (static_cast<double>(snapshot.present_voltage) != snapshot.present_soc * 10.0) {
                    invariant_held = false;
                    break;
                }
            }

            stop = true;
            writer.join();

            THEN("Every snapshot observed the invariant (no torn reads)") {
                REQUIRE(invariant_held.load());
            }
        }
    }
}
