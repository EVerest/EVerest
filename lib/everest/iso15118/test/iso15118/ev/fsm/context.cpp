// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

#include <iso15118/ev/ac_charge_params.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV Context returns a locked-copy snapshot of the seeded AC charge params") {

    GIVEN("An EV d20 Context with a seeded AcChargeParams monitor") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        ev::AcChargeParams params{};
        params.max_charge_power = 11000.0f;
        params.min_charge_power = 1000.0f;
        params.three_phase = true;
        params.present_active_power = 5000.0f;
        helper.set_ac_params(params);

        auto& ctx = helper.get_context();

        WHEN("The AC params are read back through the Context getter") {

            const auto snapshot = ctx.get_ac_params();

            THEN("Every field matches the seeded params") {
                REQUIRE(snapshot.max_charge_power == params.max_charge_power);
                REQUIRE(snapshot.min_charge_power == params.min_charge_power);
                REQUIRE(snapshot.three_phase == params.three_phase);
                REQUIRE(snapshot.present_active_power == params.present_active_power);
            }
        }

        WHEN("The live present_active_power is mutated through the monitor handle") {
            {
                auto h = helper.get_ac_params_monitor().handle();
                (*h).present_active_power = 7500.0f;
            }

            THEN("A fresh Context snapshot reflects the new live value") {
                const auto snapshot = ctx.get_ac_params();
                REQUIRE(snapshot.present_active_power == 7500.0f);

                AND_THEN("The static fields are unchanged") {
                    REQUIRE(snapshot.max_charge_power == params.max_charge_power);
                    REQUIRE(snapshot.three_phase == params.three_phase);
                }
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Context tracks the selected energy service") {

    const ev::feedback::Callbacks callbacks{};

    GIVEN("A Context constructed with the default (DC) requested service") {

        FsmStateHelper helper{callbacks};
        auto& ctx = helper.get_context();

        THEN("selected_service() defaults to DC") {
            REQUIRE(ctx.selected_service() == message_20::datatypes::ServiceCategory::DC);
        }

        WHEN("set_selected_service(AC) is called") {
            ctx.set_selected_service(message_20::datatypes::ServiceCategory::AC);

            THEN("selected_service() returns AC") {
                REQUIRE(ctx.selected_service() == message_20::datatypes::ServiceCategory::AC);
            }
        }
    }

    GIVEN("A Context constructed with a requested AC service") {

        FsmStateHelper helper{
            callbacks, {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}}, message_20::datatypes::ServiceCategory::AC};
        auto& ctx = helper.get_context();

        THEN("selected_service() defaults to AC") {
            REQUIRE(ctx.selected_service() == message_20::datatypes::ServiceCategory::AC);
        }
    }
}

SCENARIO("ISO15118-20 EV Context encodes a request that round-trips back to the same message") {

    GIVEN("An EV d20 Context with an empty MessageExchange") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        WHEN("The Context responds with a SessionSetupRequest") {

            message_20::SessionSetupRequest req{message_20::Header{}, "EVTESTID01"};
            ctx.respond(req);

            THEN("The MessageExchange holds a pending request") {
                REQUIRE(mx.has_request());

                const auto taken = mx.take_request();
                REQUIRE(taken.has_value());
                const auto& [bytes, type] = *taken;

                AND_THEN("The payload type is Part20Main") {
                    REQUIRE(type == io::v2gtp::PayloadType::Part20Main);
                }

                AND_THEN("The EXI bytes decode back to the same SessionSetupRequest") {
                    message_20::Variant variant(type, io::StreamInputView{bytes.data(), bytes.size()});

                    REQUIRE(variant.get_type() == message_20::Type::SessionSetupReq);

                    const auto* decoded = variant.get_if<message_20::SessionSetupRequest>();
                    REQUIRE(decoded != nullptr);
                    REQUIRE(decoded->evccid == "EVTESTID01");
                }
            }
        }
    }
}
