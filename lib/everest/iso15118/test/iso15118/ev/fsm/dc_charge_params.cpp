// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <atomic>
#include <optional>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include <everest/util/async/monitor.hpp>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;

namespace {

// Build a Context wired to the given DcChargeParams monitor (or nullptr).
struct ContextFixture {
    explicit ContextFixture(everest::lib::util::monitor<ev::DcChargeParams>* dc_params) :
        ctx(callbacks, msg_exch, logger, evcc_id, advertised_app_protocols, control_event, dc_params) {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    }

    ~ContextFixture() {
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    }

    ev::d20::Context& context() {
        return ctx;
    }

    ev::feedback::Callbacks callbacks{};
    ev::d20::MessageExchange msg_exch{};
    iso15118::session::SessionLogger logger{this};
    message_20::datatypes::Identifier evcc_id{"EVTESTID01"};
    std::vector<message_20::SupportedAppProtocol> advertised_app_protocols{
        {"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}};
    std::optional<ev::d20::ControlEvent> control_event{};
    ev::d20::Context ctx;
};

} // namespace

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

        everest::lib::util::monitor<ev::DcChargeParams> mon{ev::DcChargeParams{initial}};
        ContextFixture fixture{&mon};
        auto& ctx = fixture.context();

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
        everest::lib::util::monitor<ev::DcChargeParams> mon{ev::DcChargeParams{}};
        ContextFixture fixture{&mon};
        auto& ctx = fixture.context();

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

SCENARIO("ISO15118-20 EV DcChargeParams channel returns defaults for a null handle") {

    GIVEN("A Context constructed without a DcChargeParams monitor") {
        ContextFixture fixture{nullptr};
        auto& ctx = fixture.context();

        WHEN("The params are read through the Context getter") {
            const auto snapshot = ctx.get_dc_params();

            THEN("Every field is the default-constructed value") {
                const ev::DcChargeParams defaults{};
                REQUIRE(snapshot.max_charge_power == defaults.max_charge_power);
                REQUIRE(snapshot.max_charge_current == defaults.max_charge_current);
                REQUIRE(snapshot.max_voltage == defaults.max_voltage);
                REQUIRE(snapshot.min_voltage == defaults.min_voltage);
                REQUIRE(snapshot.energy_capacity == defaults.energy_capacity);
                REQUIRE(snapshot.target_voltage == defaults.target_voltage);
                REQUIRE(snapshot.target_current == defaults.target_current);
                REQUIRE(snapshot.present_soc == defaults.present_soc);
                REQUIRE(snapshot.present_voltage == defaults.present_voltage);
            }
        }
    }
}
