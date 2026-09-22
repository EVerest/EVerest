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
#include <iso15118/ev/sae_inverter_profile.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/sae_modes.hpp>

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
                    REQUIRE(snapshot.min_charge_power == params.min_charge_power);
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

SCENARIO("ISO15118-20 EV Context reports the requested energy service") {

    const ev::feedback::Callbacks callbacks{};

    GIVEN("A Context constructed with the default (DC) requested service") {

        FsmStateHelper helper{callbacks};
        auto& ctx = helper.get_context();

        THEN("selected_service() is DC and is_ac_family() is false") {
            REQUIRE(ctx.selected_service() == message_20::datatypes::ServiceCategory::DC);
            REQUIRE(ctx.is_ac_family() == false);
        }
    }

    GIVEN("A Context constructed with a requested AC service") {

        FsmStateHelper helper{
            callbacks, {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}}, message_20::datatypes::ServiceCategory::AC};
        auto& ctx = helper.get_context();

        THEN("selected_service() is AC and is_ac_family() is true") {
            REQUIRE(ctx.selected_service() == message_20::datatypes::ServiceCategory::AC);
            REQUIRE(ctx.is_ac_family() == true);
        }
    }

    GIVEN("A Context constructed with a requested AC_BPT service") {

        FsmStateHelper helper{
            callbacks, {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}}, message_20::datatypes::ServiceCategory::AC_BPT};
        auto& ctx = helper.get_context();

        THEN("is_ac_family() is true, so the AC branches take AC_BPT too") {
            REQUIRE(ctx.is_ac_family() == true);
        }
    }

    GIVEN("A Context constructed with a requested AC_DER_IEC service") {

        FsmStateHelper helper{callbacks,
                              {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                              message_20::datatypes::ServiceCategory::AC_DER_IEC};
        auto& ctx = helper.get_context();

        THEN("is_ac_family() is true, so the AC branches take AC_DER_IEC too") {
            REQUIRE(ctx.is_ac_family() == true);
        }
    }

    GIVEN("A Context constructed with a requested MCS service") {

        FsmStateHelper helper{
            callbacks, {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}}, message_20::datatypes::ServiceCategory::MCS};
        auto& ctx = helper.get_context();

        THEN("is_dc_family() is true and is_ac_family()/is_bpt() are false") {
            REQUIRE(ctx.is_dc_family() == true);
            REQUIRE(ctx.is_ac_family() == false);
            REQUIRE(ctx.is_bpt() == false);
        }
    }

    GIVEN("A Context constructed with a requested MCS_BPT service") {

        FsmStateHelper helper{
            callbacks, {{"urn:iso:std:iso:15118:-20:DC", 1, 0, 1, 1}}, message_20::datatypes::ServiceCategory::MCS_BPT};
        auto& ctx = helper.get_context();

        THEN("is_dc_family() and is_bpt() are true, so the DC and BPT branches take it") {
            REQUIRE(ctx.is_dc_family() == true);
            REQUIRE(ctx.is_bpt() == true);
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
            ctx.set_der_demanded_functions(negotiated);

            THEN("der_demanded_functions() returns it") {
                REQUIRE(ctx.der_demanded_functions() == negotiated);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Context exposes the SAE session options") {

    const ev::feedback::Callbacks callbacks{};

    GIVEN("A Context constructed with default SessionOptions") {

        FsmStateHelper helper{callbacks,
                              {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                              message_20::datatypes::ServiceCategory::AC_DER_SAE};
        auto& ctx = helper.get_context();

        THEN("the accessors return the SessionOptions defaults") {
            REQUIRE(ctx.sae_profile().supported_modes == ev::SaeInverterProfile{}.supported_modes);
            REQUIRE(ctx.sae_supported_modes() == ev::SaeInverterProfile{}.supported_modes);
            REQUIRE(ctx.cpd_rounds() == 1);
            REQUIRE(ctx.der_stop_on_invalid_control() == false);
        }
    }

    GIVEN("A Context constructed with SAE SessionOptions and unused SupportedModes bits") {

        ev::d20::SessionOptions options{};
        options.sae_profile.supported_modes = sae::sae_function_bit(sae::DerBitMapFunctions::ChargeFunction) |
                                              sae::sae_function_bit(sae::DerBitMapFunctions::VoltVarFunction) |
                                              (1U << 2) | (1U << 25) | (1U << 31);
        options.sae_profile.inverter_model = "TEST-MODEL";
        options.cpd_rounds = 3;
        options.der_stop_on_invalid_control = true;

        FsmStateHelper helper{callbacks,
                              {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                              message_20::datatypes::ServiceCategory::AC_DER_SAE,
                              options};
        auto& ctx = helper.get_context();

        THEN("the accessors return the configured values") {
            REQUIRE(ctx.sae_profile().inverter_model == "TEST-MODEL");
            REQUIRE(ctx.sae_profile().supported_modes == options.sae_profile.supported_modes);
            REQUIRE(ctx.cpd_rounds() == 3);
            REQUIRE(ctx.der_stop_on_invalid_control() == true);
        }

        THEN("sae_supported_modes() drops the bits AMD1 Table M.6 does not use") {
            REQUIRE(ctx.sae_supported_modes() == (sae::sae_function_bit(sae::DerBitMapFunctions::ChargeFunction) |
                                                  sae::sae_function_bit(sae::DerBitMapFunctions::VoltVarFunction)));
        }

        WHEN("more CPD rounds are noted than cpd_rounds()") {
            REQUIRE(ctx.cpd_rounds_sent() == 0);

            THEN("the count rises by one per round and saturates at cpd_rounds()") {
                for (const int expected : {1, 2, 3, 3, 3}) {
                    ctx.note_cpd_round_sent();
                    REQUIRE(ctx.cpd_rounds_sent() == expected);
                }
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Context holds the session SAE state") {

    const ev::feedback::Callbacks callbacks{};

    GIVEN("A freshly constructed Context") {

        FsmStateHelper helper{callbacks,
                              {{"urn:iso:std:iso:15118:-20:AC", 1, 0, 1, 1}},
                              message_20::datatypes::ServiceCategory::AC_DER_SAE};
        auto& ctx = helper.get_context();

        THEN("nothing is enabled, the service is permitted and no update time is recorded") {
            REQUIRE(ctx.sae_enabled_modes() == 0);
            REQUIRE(ctx.cpd_rounds_sent() == 0);
            REQUIRE(ctx.sae_permit_service() == true);
            REQUIRE(ctx.sae_settings_update_time() == 0);
        }

        WHEN("each value is set") {
            const auto modes = sae::sae_function_bit(sae::DerBitMapFunctions::EnterService) |
                               sae::sae_function_bit(sae::DerBitMapFunctions::VoltWattFunction);
            ctx.set_sae_enabled_modes(modes);
            ctx.set_sae_permit_service(false);
            ctx.set_sae_settings_update_time(1691411798000000U);

            THEN("its accessor returns it") {
                REQUIRE(ctx.sae_enabled_modes() == modes);
                REQUIRE(ctx.sae_permit_service() == false);
                REQUIRE(ctx.sae_settings_update_time() == 1691411798000000U);
            }
        }
    }
}

SCENARIO("ISO15118-20 EV Codec sends AC_DER_SAE requests as Part20DerSae") {
    STATIC_REQUIRE(ev::d20::Codec::payload_type_of<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>() ==
                   io::v2gtp::PayloadType::Part20DerSae);
    STATIC_REQUIRE(ev::d20::Codec::payload_type_of<message_20::DER_SAE_AC_ChargeLoopRequest>() ==
                   io::v2gtp::PayloadType::Part20DerSae);
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

SCENARIO("ISO15118-20 EV Context classifies the energy service families") {
    using message_20::datatypes::ServiceCategory;

    GIVEN("The AC family") {
        THEN("every AC service is in it, and no DC one is") {
            REQUIRE(ev::is_ac_family(ServiceCategory::AC));
            REQUIRE(ev::is_ac_family(ServiceCategory::AC_BPT));
            REQUIRE(ev::is_ac_family(ServiceCategory::AC_DER_IEC));
            REQUIRE_FALSE(ev::is_ac_family(ServiceCategory::DC));
            REQUIRE_FALSE(ev::is_ac_family(ServiceCategory::DC_BPT));
        }
    }

    GIVEN("The DC family") {
        THEN("every DC service is in it, and no AC one is") {
            REQUIRE(ev::is_dc_family(ServiceCategory::DC));
            REQUIRE(ev::is_dc_family(ServiceCategory::DC_BPT));
            REQUIRE_FALSE(ev::is_dc_family(ServiceCategory::AC));
            REQUIRE_FALSE(ev::is_dc_family(ServiceCategory::AC_DER_IEC));
        }
    }

    GIVEN("The bidirectional services") {
        THEN("both BPT services are in it, whichever family they belong to") {
            REQUIRE(ev::is_bpt(ServiceCategory::AC_BPT));
            REQUIRE(ev::is_bpt(ServiceCategory::DC_BPT));
            REQUIRE_FALSE(ev::is_bpt(ServiceCategory::AC));
            REQUIRE_FALSE(ev::is_bpt(ServiceCategory::DC));
            REQUIRE_FALSE(ev::is_bpt(ServiceCategory::AC_DER_IEC));
        }
    }
}
