// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include <iso15118/ev/ac_charge_params.hpp>
#include <iso15118/ev/config.hpp>
#include <iso15118/ev/config_validation.hpp>
#include <iso15118/ev/dc_charge_params.hpp>

using namespace iso15118;
using namespace std::chrono_literals;

namespace {

ev::EvConfig sane_config() {
    ev::EvConfig config{};
    config.interface_name = "lo";
    config.evcc_id = "02:00:00:00:00:01";
    return config;
}

ev::AcChargeParams sane_ac_params() {
    ev::AcChargeParams params{};
    params.min_charge_power = 1380.0f;
    params.max_charge_power = 11040.0f;
    params.min_discharge_power = 1380.0f;
    params.max_discharge_power = 11040.0f;
    return params;
}

ev::DcChargeParams sane_dc_params() {
    ev::DcChargeParams params{};
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
            REQUIRE(ev::validate_config(sane_config()).empty());
            REQUIRE(ev::validate_ac_charge_params(sane_ac_params()).empty());
            REQUIRE(ev::validate_dc_charge_params(sane_dc_params()).empty());
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
            REQUIRE(ev::validate_ac_charge_params(params).size() == 1);
        }
    }

    GIVEN("AC charge params whose min discharge power exceeds the max") {
        auto params = sane_ac_params();
        params.min_discharge_power = 20000.0f;
        params.max_discharge_power = 11040.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_ac_charge_params(params).size() == 1);
        }
    }

    GIVEN("DC charge params whose min discharge power exceeds the max") {
        auto params = sane_dc_params();
        params.min_discharge_power = 200000.0f;
        params.max_discharge_power = 150000.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_dc_charge_params(params).size() == 1);
        }
    }

    GIVEN("DC charge params whose min voltage exceeds the max") {
        auto params = sane_dc_params();
        params.min_voltage = 1000.0f;
        params.max_voltage = 900.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_dc_charge_params(params).size() == 1);
        }
    }
}

SCENARIO("ISO15118-20 EV config validation rejects negative power values") {
    GIVEN("AC charge params with a negative max charge power") {
        auto params = sane_ac_params();
        params.min_charge_power = -1.0f;
        params.max_charge_power = -1.0f;

        THEN("both negative values are reported") {
            REQUIRE(ev::validate_ac_charge_params(params).size() == 2);
        }
    }

    GIVEN("DC charge params with a negative max discharge current") {
        auto params = sane_dc_params();
        params.max_discharge_current = -1.0f;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_dc_charge_params(params).size() == 1);
        }
    }
}

// The connector preference and the per-line split only model one or three lines.
SCENARIO("ISO15118-20 EV config validation rejects an AC phase count other than 1 or 3") {
    GIVEN("AC charge params with a phase count of 2") {
        auto params = sane_ac_params();
        params.phase_count = 2;

        THEN("the problem is reported") {
            const auto problems = ev::validate_ac_charge_params(params);
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
            REQUIRE(ev::validate_config(config).empty());
        }
    }

    GIVEN("an EvConfig with a negative response timeout") {
        auto config = sane_config();
        config.response_timeout = -1ms;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_config(config).size() == 1);
        }
    }
}

// enforce_tls must hold on the direct-endpoint path too: there is no SDP security byte to reject.
SCENARIO("ISO15118-20 EV config validation rejects enforce_tls on a plaintext direct endpoint") {
    GIVEN("an EvConfig with enable_sdp false, enforce_tls set and no transport security") {
        auto config = sane_config();
        config.enable_sdp = false;
        config.direct_secc_endpoint = io::Ipv6EndPoint{};
        config.direct_security = io::v2gtp::Security::NO_TRANSPORT_SECURITY;
        config.tls.enforce_tls = true;

        THEN("the problem is reported") {
            const auto problems = ev::validate_config(config);
            REQUIRE(problems.size() == 1);
            REQUIRE(problems.front() == "enforce_tls is set but direct_security is not TLS");
        }
    }

    GIVEN("the same config with a TLS direct endpoint") {
        auto config = sane_config();
        config.enable_sdp = false;
        config.direct_secc_endpoint = io::Ipv6EndPoint{};
        config.direct_security = io::v2gtp::Security::TLS;
        config.tls.enforce_tls = true;

        THEN("nothing is reported") {
            REQUIRE(ev::validate_config(config).empty());
        }
    }

    GIVEN("enforce_tls with SDP enabled") {
        auto config = sane_config();
        config.tls.enforce_tls = true;

        THEN("nothing is reported: the SDP response carries the security byte") {
            REQUIRE(ev::validate_config(config).empty());
        }
    }
}

// -20 identifierType: 1..255 characters; a MAC string is one valid form.
SCENARIO("ISO15118-20 EV config validation rejects an empty or oversized evcc_id") {
    const auto reports_one_problem = [](const std::string& evcc_id) {
        auto config = sane_config();
        config.evcc_id = evcc_id;
        return ev::validate_config(config).size() == 1;
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
            REQUIRE(ev::validate_config(mac).empty());

            auto wmi = sane_config();
            wmi.evcc_id = "WMIV1234567890ABCDEX";
            REQUIRE(ev::validate_config(wmi).empty());
        }
    }
}
