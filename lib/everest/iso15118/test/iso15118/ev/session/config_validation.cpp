// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <iso15118/ev/config/config.hpp>
#include <iso15118/ev/config/validation.hpp>
#include <iso15118/ev/params/ac.hpp>
#include <iso15118/ev/params/dc.hpp>

using namespace iso15118;
using namespace std::chrono_literals;

namespace {

using Problems = std::vector<std::string>;

constexpr float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();
constexpr float INF_VALUE = std::numeric_limits<float>::infinity();

ev::config::EvConfig sane_config() {
    ev::config::EvConfig config{};
    config.interface_name = "lo";
    config.evcc_id = "02:00:00:00:00:01";
    return config;
}

ev::config::EvConfig sae_config() {
    auto config = sane_config();
    config.energy_service = message_20::datatypes::ServiceCategory::AC_DER_SAE;
    return config;
}

ev::params::AcChargeParams sane_ac_params() {
    ev::params::AcChargeParams params{};
    params.min_charge_power = 1380.0f;
    params.max_charge_power = 11040.0f;
    params.min_discharge_power = 1380.0f;
    params.max_discharge_power = 11040.0f;
    return params;
}

ev::params::DcChargeParams sane_dc_params() {
    ev::params::DcChargeParams params{};
    params.max_charge_power = 150000.0f;
    params.max_charge_current = 300.0f;
    params.min_discharge_power = 0.0f;
    params.max_discharge_power = 150000.0f;
    params.max_discharge_current = 300.0f;
    params.min_voltage = 200.0f;
    params.max_voltage = 900.0f;
    return params;
}

} // namespace

SCENARIO("ISO15118-20 EV config validation accepts a sane configuration") {
    GIVEN("a sane EvConfig and sane charge params") {
        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(sane_config()).empty());
            REQUIRE(ev::config::validate_ac_charge_params(sane_ac_params()).empty());
            REQUIRE(ev::config::validate_dc_charge_params(sane_dc_params()).empty());
        }
    }
}

// An inverted power window is advertised verbatim to the SECC, which then either
// rejects the ChargeParameterDiscovery or negotiates against an impossible window.
SCENARIO("ISO15118-20 EV config validation rejects a min power above its max") {
    GIVEN("AC charge params whose min charge power exceeds the max") {
        auto params = sane_ac_params();
        params.min_charge_power = 20000.0f;
        params.max_charge_power = 11040.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_ac_charge_params(params).size() == 1);
        }
    }

    GIVEN("AC charge params whose min discharge power exceeds the max") {
        auto params = sane_ac_params();
        params.min_discharge_power = 20000.0f;
        params.max_discharge_power = 11040.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_ac_charge_params(params).size() == 1);
        }
    }

    GIVEN("DC charge params whose min discharge power exceeds the max") {
        auto params = sane_dc_params();
        params.min_discharge_power = 200000.0f;
        params.max_discharge_power = 150000.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_dc_charge_params(params).size() == 1);
        }
    }

    GIVEN("DC charge params whose min voltage exceeds the max") {
        auto params = sane_dc_params();
        params.min_voltage = 1000.0f;
        params.max_voltage = 900.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_dc_charge_params(params).size() == 1);
        }
    }
}

SCENARIO("ISO15118-20 EV config validation rejects negative power values") {
    GIVEN("AC charge params with a negative max charge power") {
        auto params = sane_ac_params();
        params.min_charge_power = -1.0f;
        params.max_charge_power = -1.0f;

        THEN("both negative values are reported") {
            REQUIRE(ev::config::validate_ac_charge_params(params).size() == 2);
        }
    }

    GIVEN("DC charge params with a negative max discharge current") {
        auto params = sane_dc_params();
        params.max_discharge_current = -1.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_dc_charge_params(params).size() == 1);
        }
    }
}

// A DC session advertises these limits to the SECC; zero, as left by a missing or partial
// set_dc_params, would announce a vehicle that takes no power at all.
SCENARIO("ISO15118-20 EV config validation rejects DC charge limits that are not positive") {
    GIVEN("DC charge params left at their defaults") {
        THEN("the max charge power, max charge current and max voltage are reported") {
            const auto problems = ev::config::validate_dc_charge_params(ev::params::DcChargeParams{});
            REQUIRE(problems.size() == 3);
            REQUIRE(problems.at(0) == "dc max_charge_power must be positive (is 0.000000)");
            REQUIRE(problems.at(1) == "dc max_charge_current must be positive (is 0.000000)");
            REQUIRE(problems.at(2) == "dc max_voltage must be positive (is 0.000000)");
        }
    }

    GIVEN("DC charge params with a negative max voltage") {
        auto params = sane_dc_params();
        params.min_voltage = -2.0f;
        params.max_voltage = -1.0f;

        THEN("each value is reported once") {
            REQUIRE(ev::config::validate_dc_charge_params(params).size() == 2);
        }
    }
}

// The connector preference and the per-line split only model one or three lines.
SCENARIO("ISO15118-20 EV config validation rejects an AC phase count other than 1 or 3") {
    GIVEN("AC charge params with a phase count of 2") {
        auto params = sane_ac_params();
        params.phase_count = 2;

        THEN("the problem is reported") {
            const auto problems = ev::config::validate_ac_charge_params(params);
            REQUIRE(problems.size() == 1);
            REQUIRE(problems.front() == "ac phase_count must be 1 or 3 (is 2)");
        }
    }
}

SCENARIO("ISO15118-20 EV config validation rejects a negative response timeout") {
    GIVEN("an EvConfig with a zero response timeout (per-message table)") {
        auto config = sane_config();
        config.response_timeout = 0ms;

        THEN("nothing is reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }

    GIVEN("an EvConfig with a negative response timeout") {
        auto config = sane_config();
        config.response_timeout = -1ms;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_config(config).size() == 1);
        }
    }
}

SCENARIO("ISO15118-20 EV config validation rejects zero cpd_rounds") {
    GIVEN("an EvConfig with cpd_rounds set to zero") {
        auto config = sane_config();
        config.d20.cpd_rounds = 0;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_config(config) == Problems{"cpd_rounds must be positive (is 0)"});
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE voltage window") {
    GIVEN("the default profile") {
        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(sae_config()).empty());
        }
    }

    GIVEN("a maximum voltage equal to the minimum") {
        auto config = sae_config();
        config.d20.sae_profile.maximum_voltage_v = 207.0f;
        config.d20.sae_profile.minimum_voltage_v = 207.0f;
        config.d20.sae_profile.nominal_voltage_v = 207.0f;

        THEN("only the inverted window is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile maximum_voltage_v (207.000000) must exceed minimum_voltage_v (207.000000)"});
        }
    }

    GIVEN("a maximum voltage below the minimum and a nominal voltage above both") {
        auto config = sae_config();
        config.d20.sae_profile.maximum_voltage_v = 100.0f;

        THEN("only the inverted window is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile maximum_voltage_v (100.000000) must exceed minimum_voltage_v (207.000000)"});
        }
    }

    GIVEN("a nominal voltage above the window") {
        auto config = sae_config();
        config.d20.sae_profile.nominal_voltage_v = 300.0f;

        THEN("the nominal voltage is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile nominal_voltage_v (300.000000) must lie within [minimum_voltage_v, "
                             "maximum_voltage_v] = [207.000000, 253.000000]"});
        }
    }

    GIVEN("a nominal voltage below the window") {
        auto config = sae_config();
        config.d20.sae_profile.nominal_voltage_v = 100.0f;

        THEN("the nominal voltage is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile nominal_voltage_v (100.000000) must lie within [minimum_voltage_v, "
                             "maximum_voltage_v] = [207.000000, 253.000000]"});
        }
    }

    GIVEN("a zero minimum voltage") {
        auto config = sae_config();
        config.d20.sae_profile.minimum_voltage_v = 0.0f;

        THEN("only the voltage is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile minimum_voltage_v must be finite and positive (is 0.000000)"});
        }
    }

    GIVEN("a NaN maximum voltage") {
        auto config = sae_config();
        config.d20.sae_profile.maximum_voltage_v = NAN_VALUE;

        THEN("only the voltage is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile maximum_voltage_v must be finite and positive (is nan)"});
        }
    }

    GIVEN("an infinite nominal voltage") {
        auto config = sae_config();
        config.d20.sae_profile.nominal_voltage_v = INF_VALUE;

        THEN("only the voltage is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile nominal_voltage_v must be finite and positive (is inf)"});
        }
    }

    GIVEN("a negative nominal voltage offset") {
        auto config = sae_config();
        config.d20.sae_profile.nominal_voltage_offset_v = -5.0f;

        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }

    GIVEN("a NaN nominal voltage offset") {
        auto config = sae_config();
        config.d20.sae_profile.nominal_voltage_offset_v = NAN_VALUE;

        THEN("the offset is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile nominal_voltage_offset_v must be finite (is nan)"});
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE nominal frequency") {
    const auto frequency_problems = [](float frequency) {
        auto config = sae_config();
        config.d20.sae_profile.nominal_frequency_hz = frequency;
        return ev::config::validate_config(config);
    };

    GIVEN("a zero, a NaN and an infinite nominal frequency") {
        THEN("each is reported") {
            REQUIRE(frequency_problems(0.0f) ==
                    Problems{"sae_profile nominal_frequency_hz must be finite and positive (is 0.000000)"});
            REQUIRE(frequency_problems(NAN_VALUE) ==
                    Problems{"sae_profile nominal_frequency_hz must be finite and positive (is nan)"});
            REQUIRE(frequency_problems(INF_VALUE) ==
                    Problems{"sae_profile nominal_frequency_hz must be finite and positive (is inf)"});
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE power factors") {
    GIVEN("both power factors at exactly 1") {
        auto config = sae_config();
        config.d20.sae_profile.over_excited_power_factor = 1.0f;
        config.d20.sae_profile.under_excited_power_factor = 1.0f;

        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }

    GIVEN("out-of-range and non-finite power factors") {
        const auto over_problems = [](float power_factor) {
            auto config = sae_config();
            config.d20.sae_profile.over_excited_power_factor = power_factor;
            return ev::config::validate_config(config);
        };
        const auto under_problems = [](float power_factor) {
            auto config = sae_config();
            config.d20.sae_profile.under_excited_power_factor = power_factor;
            return ev::config::validate_config(config);
        };

        THEN("each is reported") {
            REQUIRE(over_problems(90.0f) ==
                    Problems{"sae_profile over_excited_power_factor must be in (0, 1] (is 90.000000)"});
            REQUIRE(over_problems(NAN_VALUE) ==
                    Problems{"sae_profile over_excited_power_factor must be in (0, 1] (is nan)"});
            REQUIRE(over_problems(INF_VALUE) ==
                    Problems{"sae_profile over_excited_power_factor must be in (0, 1] (is inf)"});
            REQUIRE(under_problems(0.0f) ==
                    Problems{"sae_profile under_excited_power_factor must be in (0, 1] (is 0.000000)"});
            REQUIRE(under_problems(-0.5f) ==
                    Problems{"sae_profile under_excited_power_factor must be in (0, 1] (is -0.500000)"});
        }
    }

    GIVEN("a bad power factor and a nominal voltage outside the window") {
        auto config = sae_config();
        config.d20.sae_profile.over_excited_power_factor = 90.0f;
        config.d20.sae_profile.nominal_voltage_v = 300.0f;

        THEN("both are reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"sae_profile nominal_voltage_v (300.000000) must lie within [minimum_voltage_v, "
                             "maximum_voltage_v] = [207.000000, 253.000000]",
                             "sae_profile over_excited_power_factor must be in (0, 1] (is 90.000000)"});
        }
    }
}

SCENARIO("ISO15118-20 EV SAE profile validation stands on its own") {
    GIVEN("the default profile") {
        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_sae_profile(ev::d20::der::SaeInverterProfile{}).empty());
        }
    }

    GIVEN("a profile with a bad power factor and a nominal voltage outside the window") {
        auto config = sae_config();
        config.d20.sae_profile.over_excited_power_factor = 0.0f;
        config.d20.sae_profile.nominal_voltage_v = 300.0f;

        THEN("it reports what validate_config reports for an AC_DER_SAE session") {
            const auto problems = ev::config::validate_sae_profile(config.d20.sae_profile);
            REQUIRE(problems ==
                    Problems{"sae_profile nominal_voltage_v (300.000000) must lie within [minimum_voltage_v, "
                             "maximum_voltage_v] = [207.000000, 253.000000]",
                             "sae_profile over_excited_power_factor must be in (0, 1] (is 0.000000)"});
            REQUIRE(ev::config::validate_config(config) == problems);
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE per-phase totals") {
    struct Total {
        float ev::d20::der::SaeInverterProfile::*field;
        const char* name;
    };
    const std::vector<Total> totals{
        {&ev::d20::der::SaeInverterProfile::max_apparent_power_charging_var_absorption_va,
         "max_apparent_power_charging_var_absorption_va"},
        {&ev::d20::der::SaeInverterProfile::max_apparent_power_charging_var_injection_va,
         "max_apparent_power_charging_var_injection_va"},
        {&ev::d20::der::SaeInverterProfile::max_apparent_power_discharging_var_absorption_va,
         "max_apparent_power_discharging_var_absorption_va"},
        {&ev::d20::der::SaeInverterProfile::max_apparent_power_discharging_var_injection_va,
         "max_apparent_power_discharging_var_injection_va"},
        {&ev::d20::der::SaeInverterProfile::max_var_absorption_charging_var, "max_var_absorption_charging_var"},
        {&ev::d20::der::SaeInverterProfile::max_var_injection_charging_var, "max_var_injection_charging_var"},
        {&ev::d20::der::SaeInverterProfile::max_var_absorption_discharging_var, "max_var_absorption_discharging_var"},
        {&ev::d20::der::SaeInverterProfile::max_var_injection_discharging_var, "max_var_injection_discharging_var"},
        {&ev::d20::der::SaeInverterProfile::reactive_susceptance_s, "reactive_susceptance_s"},
        {&ev::d20::der::SaeInverterProfile::over_excited_discharge_power_w, "over_excited_discharge_power_w"},
        {&ev::d20::der::SaeInverterProfile::under_excited_discharge_power_w, "under_excited_discharge_power_w"},
    };

    GIVEN("each total set negative, then NaN") {
        THEN("each is reported by name") {
            for (const auto& total : totals) {
                auto config = sae_config();
                config.d20.sae_profile.*total.field = -1.0f;
                REQUIRE(ev::config::validate_config(config) ==
                        Problems{std::string{"sae_profile "} + total.name +
                                 " must be finite and not negative (is -1.000000)"});

                config.d20.sae_profile.*total.field = NAN_VALUE;
                REQUIRE(ev::config::validate_config(config) == Problems{std::string{"sae_profile "} + total.name +
                                                                        " must be finite and not negative (is nan)"});
            }
        }
    }

    GIVEN("every total at zero") {
        auto config = sae_config();
        for (const auto& total : totals) {
            config.d20.sae_profile.*total.field = 0.0f;
        }

        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE supported_modes") {
    const auto modes_problems = [](std::uint32_t modes) {
        auto config = sae_config();
        config.d20.sae_profile.supported_modes = modes;
        return ev::config::validate_config(config);
    };

    GIVEN("ChargeFunction, DischargeFunction and EnterService") {
        THEN("no problems are reported") {
            REQUIRE(modes_problems(0x0000000Bu).empty());
        }
    }

    GIVEN("every bit set") {
        THEN("only the unused bits are reported") {
            REQUIRE(modes_problems(0xFFFFFFFFu) == Problems{"sae_profile supported_modes sets unused bits 0xFA000204"});
        }
    }

    GIVEN("ChargeFunction without DischargeFunction, and no bits at all") {
        THEN("each is reported") {
            REQUIRE(
                modes_problems(0x00000001u) ==
                Problems{"sae_profile supported_modes must set ChargeFunction and DischargeFunction (is 0x00000001)"});
            REQUIRE(
                modes_problems(0x00000000u) ==
                Problems{"sae_profile supported_modes must set ChargeFunction and DischargeFunction (is 0x00000000)"});
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE inverter string lengths") {
    struct Text {
        void (*set)(ev::d20::der::SaeInverterProfile&, const std::string&);
        const char* name;
    };
    const std::vector<Text> texts{
        {[](ev::d20::der::SaeInverterProfile& p, const std::string& s) { p.inverter_sw_version = s; },
         "inverter_sw_version"},
        {[](ev::d20::der::SaeInverterProfile& p, const std::string& s) { p.inverter_hw_version = s; },
         "inverter_hw_version"},
        {[](ev::d20::der::SaeInverterProfile& p, const std::string& s) { p.inverter_manufacturer = s; },
         "inverter_manufacturer"},
        {[](ev::d20::der::SaeInverterProfile& p, const std::string& s) { p.inverter_model = s; }, "inverter_model"},
        {[](ev::d20::der::SaeInverterProfile& p, const std::string& s) { p.inverter_serial_number = s; },
         "inverter_serial_number"},
    };

    GIVEN("each string at 32 bytes, then 33") {
        THEN("only 33 is reported by name") {
            for (const auto& text : texts) {
                auto config = sae_config();
                text.set(config.d20.sae_profile, std::string(32, 'A'));
                REQUIRE(ev::config::validate_config(config).empty());

                text.set(config.d20.sae_profile, std::string(33, 'A'));
                REQUIRE(ev::config::validate_config(config) ==
                        Problems{std::string{"sae_profile "} + text.name + " must be at most 32 bytes (is 33)"});
            }
        }
    }

    GIVEN("no inverter_hw_version") {
        auto config = sae_config();
        config.d20.sae_profile.inverter_hw_version.reset();

        THEN("no problems are reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }
}

SCENARIO("ISO15118-20 EV config validation checks the SAE profile for AC_DER_SAE only") {
    const auto broken = [](message_20::datatypes::ServiceCategory service) {
        auto config = sane_config();
        config.energy_service = service;
        config.d20.sae_profile.maximum_voltage_v = 100.0f;
        config.d20.sae_profile.nominal_frequency_hz = NAN_VALUE;
        config.d20.sae_profile.over_excited_power_factor = 90.0f;
        config.d20.sae_profile.max_var_injection_charging_var = -1.0f;
        config.d20.sae_profile.supported_modes = 0;
        return config;
    };

    GIVEN("DC and AC_DER_IEC configs with a broken profile") {
        THEN("the profile is not checked") {
            REQUIRE(ev::config::validate_config(broken(message_20::datatypes::ServiceCategory::DC)).empty());
            REQUIRE(ev::config::validate_config(broken(message_20::datatypes::ServiceCategory::AC_DER_IEC)).empty());
        }
    }

    GIVEN("the same profile under AC_DER_SAE") {
        THEN("it is checked") {
            REQUIRE(ev::config::validate_config(broken(message_20::datatypes::ServiceCategory::AC_DER_SAE)) ==
                    Problems{
                        "sae_profile supported_modes must set ChargeFunction and DischargeFunction (is 0x00000000)",
                        "sae_profile maximum_voltage_v (100.000000) must exceed minimum_voltage_v (207.000000)",
                        "sae_profile nominal_frequency_hz must be finite and positive (is nan)",
                        "sae_profile over_excited_power_factor must be in (0, 1] (is 90.000000)",
                        "sae_profile max_var_injection_charging_var must be finite and not negative (is -1.000000)",
                    });
        }
    }
}

// tls require must hold on the direct-endpoint path too: there is no SDP security byte to reject.
SCENARIO("ISO15118-20 EV config validation rejects tls require on a plaintext direct endpoint") {
    GIVEN("an EvConfig with enable_sdp false, tls require and no transport security") {
        auto config = sane_config();
        config.enable_sdp = false;
        config.direct_secc_endpoint = io::Ipv6EndPoint{};
        config.direct_security = v2gtp::Security::NO_TRANSPORT_SECURITY;
        config.tls.mode = ev::config::TlsMode::Require;
        config.tls.client_cert_chain_path = "chain.pem";
        config.tls.client_key_path = "key.pem";

        THEN("the problem is reported") {
            const auto problems = ev::config::validate_config(config);
            REQUIRE(problems.size() == 1);
            REQUIRE(problems.front() == "tls is require but direct_security is not TLS");
        }
    }

    GIVEN("the same config with a TLS direct endpoint") {
        auto config = sane_config();
        config.enable_sdp = false;
        config.direct_secc_endpoint = io::Ipv6EndPoint{};
        config.direct_security = v2gtp::Security::TLS;
        config.tls.mode = ev::config::TlsMode::Require;
        config.tls.client_cert_chain_path = "chain.pem";
        config.tls.client_key_path = "key.pem";

        THEN("nothing is reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }

    GIVEN("tls require with SDP enabled") {
        auto config = sane_config();
        config.tls.mode = ev::config::TlsMode::Require;
        config.tls.client_cert_chain_path = "chain.pem";
        config.tls.client_key_path = "key.pem";

        THEN("nothing is reported: the SDP response carries the security byte") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }
}

// The Controller refuses to construct without an offer, so the check runs on the offer it would send.
SCENARIO("EV config validation rejects a configuration with nothing to offer") {
    const auto nothing_to_offer = Problems{"no supported protocol can be offered in SupportedAppProtocolReq"};

    GIVEN("no supported protocol") {
        auto config = sane_config();
        config.supported_protocols.clear();

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_config(config) == nothing_to_offer);
        }
    }

    GIVEN("DIN SPEC 70121 only, for an AC service") {
        auto config = sane_config();
        config.supported_protocols = {ProtocolId::DIN70121};
        config.energy_service = message_20::datatypes::ServiceCategory::AC;

        THEN("the problem is reported: DIN is DC only") {
            REQUIRE(ev::config::validate_config(config) == nothing_to_offer);
        }
    }

    GIVEN("DIN SPEC 70121 only, over TLS") {
        auto config = sane_config();
        config.supported_protocols = {ProtocolId::DIN70121};
        config.tls.mode = ev::config::TlsMode::Prefer;

        THEN("the problem is reported: DIN is plaintext only") {
            REQUIRE(ev::config::validate_config(config) == nothing_to_offer);
        }
    }

    GIVEN("DIN SPEC 70121 only, for a plaintext DC service") {
        auto config = sane_config();
        config.supported_protocols = {ProtocolId::DIN70121};

        THEN("nothing is reported") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }
}

SCENARIO("EV config validation rejects disabled SDP without a direct endpoint") {
    GIVEN("an EvConfig with enable_sdp false and no direct_secc_endpoint") {
        auto config = sane_config();
        config.enable_sdp = false;

        THEN("the problem is reported") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"enable_sdp is false but no direct_secc_endpoint is configured"});
        }
    }
}

// -20 identifierType: 1..255 characters; a MAC string is one valid form.
SCENARIO("ISO15118-20 EV config validation rejects an empty or oversized evcc_id") {
    const auto reports_one_problem = [](const std::string& evcc_id) {
        auto config = sane_config();
        config.evcc_id = evcc_id;
        return ev::config::validate_config(config).size() == 1;
    };

    GIVEN("an empty and an oversized evcc_id") {
        THEN("each is reported") {
            REQUIRE(reports_one_problem(""));
            REQUIRE(reports_one_problem(std::string(256, 'A')));
        }
    }

    GIVEN("a MAC-formatted and a WMI-style evcc_id") {
        THEN("neither is reported") {
            auto mac = sane_config();
            mac.evcc_id = "ab:cd:ef:01:23:45";
            REQUIRE(ev::config::validate_config(mac).empty());

            auto wmi = sane_config();
            wmi.evcc_id = "WMIV1234567890ABCDEX";
            REQUIRE(ev::config::validate_config(wmi).empty());
        }
    }
}

SCENARIO("EV config validation keeps the inclusive evcc_id length boundaries") {
    auto config = sane_config();
    config.evcc_id = "A";
    REQUIRE(ev::config::validate_config(config).empty());
    config.evcc_id = std::string(255, 'A');
    REQUIRE(ev::config::validate_config(config).empty());
}

SCENARIO("EV welding detection config rejects invalid bounds") {
    auto config = sane_config();
    for (const auto voltage : {0.0f, -1.0f, NAN_VALUE, INF_VALUE}) {
        config.welding_detection.safe_voltage_v = voltage;
        REQUIRE_FALSE(ev::config::validate_config(config).empty());
    }
    config = sane_config();
    config.welding_detection.max_cycles = 0;
    REQUIRE_FALSE(ev::config::validate_config(config).empty());
    config.welding_detection.max_cycles = -1;
    REQUIRE_FALSE(ev::config::validate_config(config).empty());
    config = sane_config();
    config.welding_detection.timeout = 0ms;
    REQUIRE_FALSE(ev::config::validate_config(config).empty());
    config.welding_detection.timeout = -1ms;
    REQUIRE_FALSE(ev::config::validate_config(config).empty());
    config = sane_config();
    config.welding_detection = {42.0f, 1, 120s};
    REQUIRE(ev::config::validate_config(config).empty());
}

SCENARIO("EV tls_deviations names a protocol a pinned tls_version runs outside its TLS version") {
    using ev::config::TlsMode;
    using ev::config::TlsVersion;

    auto config = sane_config();
    config.supported_protocols = {ProtocolId::ISO15118_20, ProtocolId::ISO15118_2};
    config.tls.mode = TlsMode::Prefer;

    GIVEN("tls_version pinned to 1.3") {
        config.tls.version = TlsVersion::Tls1_3;

        THEN("ISO 15118-2 over TLS 1.3 is named") {
            REQUIRE(ev::config::tls_deviations(config) ==
                    Problems{"tls_version 1.3 runs ISO 15118-2 over TLS 1.3; ISO 15118-2 requires TLS 1.2"});
        }
    }

    GIVEN("tls_version pinned to 1.2") {
        config.tls.version = TlsVersion::Tls1_2;

        THEN("ISO 15118-20 over TLS 1.2 is named") {
            REQUIRE(ev::config::tls_deviations(config) ==
                    Problems{"tls_version 1.2 runs ISO 15118-20 over TLS 1.2; ISO 15118-20 requires TLS 1.3"});
        }
    }

    GIVEN("tls_version auto") {
        THEN("nothing deviates: the offer follows the negotiated version") {
            REQUIRE(ev::config::tls_deviations(config).empty());
        }
    }

    GIVEN("a pinned tls_version with tls off") {
        config.tls.mode = TlsMode::Off;
        config.tls.version = TlsVersion::Tls1_3;

        THEN("nothing deviates: no TLS runs") {
            REQUIRE(ev::config::tls_deviations(config).empty());
        }
    }

    GIVEN("a pinned version matching the only enabled protocol") {
        config.supported_protocols = {ProtocolId::ISO15118_2};
        config.tls.version = TlsVersion::Tls1_2;

        THEN("nothing deviates") {
            REQUIRE(ev::config::tls_deviations(config).empty());
        }
    }
}

SCENARIO("EV config validation rejects tls_version auto with no TLS version to offer") {
    using ev::config::TlsMode;
    using ev::config::TlsVersion;

    auto config = sane_config();
    config.tls.mode = TlsMode::Prefer;
    config.tls.version = TlsVersion::Auto;

    GIVEN("only ISO 15118-20 enabled and no vehicle chain configured") {
        config.supported_protocols = {ProtocolId::ISO15118_20};

        THEN("the config is rejected, naming the protocols and why TLS 1.3 is unavailable") {
            REQUIRE(ev::config::validate_config(config) ==
                    Problems{"tls_version auto has no TLS version to offer for the enabled protocols "
                             "(ISO15118-20): TLS 1.2 needs ISO 15118-2 in the offer; TLS 1.3 is unavailable: "
                             "no vehicle certificate chain or key configured"});
        }
    }

    GIVEN("only ISO 15118-20 enabled and a vehicle chain configured") {
        config.supported_protocols = {ProtocolId::ISO15118_20};
        config.tls.client_cert_chain_path = "chain.pem";
        config.tls.client_key_path = "key.pem";

        THEN("the config is accepted; the files are checked at connect") {
            REQUIRE(ev::config::validate_config(config).empty());
        }

        THEN("a chain problem found at connect names the files") {
            REQUIRE(ev::config::auto_tls_problem(config, "vehicle key key.pem is missing or unreadable") ==
                    "tls_version auto has no TLS version to offer for the enabled protocols (ISO15118-20): "
                    "TLS 1.2 needs ISO 15118-2 in the offer; TLS 1.3 is unavailable: vehicle key key.pem is "
                    "missing or unreadable");
        }
    }

    GIVEN("only ISO 15118-2 enabled and no vehicle chain configured") {
        config.supported_protocols = {ProtocolId::ISO15118_2};

        THEN("the config is accepted: TLS 1.2 is left") {
            REQUIRE(ev::config::validate_config(config).empty());
        }
    }

    GIVEN("a pinned tls_version") {
        config.supported_protocols = {ProtocolId::ISO15118_20};
        config.tls.version = TlsVersion::Tls1_3;

        THEN("nothing is reported") {
            REQUIRE(ev::config::auto_tls_problem(config, "no vehicle certificate chain or key configured").empty());
        }
    }
}
