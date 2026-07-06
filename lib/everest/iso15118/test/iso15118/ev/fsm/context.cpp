// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/variant.hpp>

#include "helper.hpp"

#include <iso15118/d20/der_functions.hpp>
#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/dc_charge_params.hpp>
#include <iso15118/ev/der_control_functions.hpp>

using namespace iso15118;

SCENARIO("ISO15118-20 EV Context returns a locked-copy snapshot of the seeded AC charge params") {

    GIVEN("An EV d20 Context with a seeded AcChargeParams monitor") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        ev::AcChargeParams params{};
        params.max_charge_power = 11000.0f;
        params.min_charge_power = 1000.0f;
        params.max_discharge_power = 9000.0f;
        params.min_discharge_power = 500.0f;
        params.three_phase = true;
        params.present_active_power = 5000.0f;
        helper.set_ac_params(params);

        auto& ctx = helper.get_context();

        WHEN("The AC params are read back through the Context getter") {

            const auto snapshot = ctx.get_ac_params();

            THEN("Every field matches the seeded params") {
                REQUIRE(snapshot.max_charge_power == params.max_charge_power);
                REQUIRE(snapshot.min_charge_power == params.min_charge_power);
                REQUIRE(snapshot.max_discharge_power == params.max_discharge_power);
                REQUIRE(snapshot.min_discharge_power == params.min_discharge_power);
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

SCENARIO("ISO15118-20 EV Context returns a locked-copy snapshot of the seeded DC discharge params") {

    GIVEN("An EV d20 Context with a seeded DcChargeParams monitor carrying discharge limits") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        ev::DcChargeParams params{};
        params.max_charge_power = 150000.0f;
        params.max_charge_current = 200.0f;
        params.max_discharge_power = 120000.0f;
        params.min_discharge_power = 1000.0f;
        params.max_discharge_current = 180.0f;
        helper.set_dc_params(params);

        auto& ctx = helper.get_context();

        WHEN("The DC params are read back through the Context getter") {

            const auto snapshot = ctx.get_dc_params();

            THEN("The discharge fields match the seeded params") {
                REQUIRE(snapshot.max_charge_power == params.max_charge_power);
                REQUIRE(snapshot.max_charge_current == params.max_charge_current);
                REQUIRE(snapshot.max_discharge_power == params.max_discharge_power);
                REQUIRE(snapshot.min_discharge_power == params.min_discharge_power);
                REQUIRE(snapshot.max_discharge_current == params.max_discharge_current);
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

SCENARIO("ISO15118-20 EV DerControlFunctions to_bitset maps flags to DERControlName positions") {
    using iso15118::iec::DERControlName;

    GIVEN("A DerControlFunctions with the two DSO setpoint flags set") {
        ev::DerControlFunctions functions{};
        functions.dso_q_setpoint_provision = true;
        functions.dso_cos_phi_setpoint_provision = true;

        const auto bits = functions.to_bitset();

        THEN("Exactly the DSO setpoint bit positions are set") {
            REQUIRE(bits.count() == 2);
            REQUIRE(bits.test(static_cast<size_t>(DERControlName::DSOQSetpointProvision)));
            REQUIRE(bits.test(static_cast<size_t>(DERControlName::DSOCosPhiSetpointProvision)));
            REQUIRE_FALSE(bits.test(static_cast<size_t>(DERControlName::OverFrequencyWattMode)));
        }
    }

    GIVEN("A DerControlFunctions with the first and last flags set") {
        ev::DerControlFunctions functions{};
        functions.over_frequency_watt_mode = true;
        functions.under_voltage_fault_ride_through_mode = true;

        const auto bits = functions.to_bitset();

        THEN("The bit positions match the enum head and tail") {
            REQUIRE(bits.test(static_cast<size_t>(DERControlName::OverFrequencyWattMode)));
            REQUIRE(bits.test(static_cast<size_t>(DERControlName::UnderVoltageFaultRideThroughMode)));
            REQUIRE(bits.count() == 2);
        }
    }
}

SCENARIO("ISO15118-20 EV Context exposes the configured DER supported functions") {
    using iso15118::iec::DERControlName;

    const ev::feedback::Callbacks callbacks{};

    GIVEN("A Context constructed with DSO setpoint DER support") {
        ev::DerControlFunctions functions{};
        functions.dso_q_setpoint_provision = true;
        functions.dso_cos_phi_setpoint_provision = true;

        FsmStateHelper helper{callbacks,
                              {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                              message_20::datatypes::ServiceCategory::AC_DER_IEC,
                              functions,
                              false};
        auto& ctx = helper.get_context();

        THEN("der_supported_functions() equals the configured bitset") {
            REQUIRE(ctx.der_supported_functions() == functions.to_bitset());
        }

        THEN("der_stop_on_unsupported_functions() reflects the ctor argument") {
            REQUIRE(ctx.der_stop_on_unsupported_functions() == false);
        }

        WHEN("a negotiated mask is recorded") {
            std::bitset<ev::DER_CONTROL_FUNCTION_COUNT> negotiated{};
            negotiated.set(static_cast<size_t>(DERControlName::DSOQSetpointProvision));
            ctx.set_der_negotiated_functions(negotiated);

            THEN("der_negotiated_functions() returns it") {
                REQUIRE(ctx.der_negotiated_functions() == negotiated);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Context encodes a request that round-trips back to the same message") {

    GIVEN("An EV d20 Context with an empty MessageExchange") {

        const ev::feedback::Callbacks callbacks{};
        FsmStateHelper helper{callbacks};

        auto& ctx = helper.get_context();
        auto& mx = helper.get_message_exchange();

        WHEN("The Context sends a SessionSetupRequest") {

            message_20::SessionSetupRequest req{message_20::Header{}, "EVTESTID01"};
            ctx.send_request(req);

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
