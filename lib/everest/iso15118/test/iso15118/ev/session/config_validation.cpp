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

SCENARIO("ISO15118-20 EV config validation rejects a non-positive response timeout") {
    GIVEN("an EvConfig with a zero response timeout") {
        auto config = sane_config();
        config.response_timeout = 0ms;

        THEN("the problem is reported") {
            REQUIRE(ev::validate_config(config).size() == 1);
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

// The EVCC id goes on the wire as the EVCCID and the SECC keys the session on it; a
// non-MAC string is rejected by the SECC rather than silently tolerated.
SCENARIO("ISO15118-20 EV config validation rejects a non MAC-formatted evcc_id") {
    const auto reports_one_problem = [](const std::string& evcc_id) {
        auto config = sane_config();
        config.evcc_id = evcc_id;
        return ev::validate_config(config).size() == 1;
    };

    GIVEN("evcc_ids that are not MAC-formatted") {
        THEN("each is reported") {
            REQUIRE(reports_one_problem(""));
            REQUIRE(reports_one_problem("EVTESTID01"));
            REQUIRE(reports_one_problem("02:00:00:00:00"));
            REQUIRE(reports_one_problem("02:00:00:00:00:01:02"));
            REQUIRE(reports_one_problem("02-00-00-00-00-01"));
            REQUIRE(reports_one_problem("02:00:00:00:00:0g"));
            REQUIRE(reports_one_problem("020000000001"));
        }
    }

    GIVEN("MAC-formatted evcc_ids in either case") {
        THEN("neither is reported") {
            auto lower = sane_config();
            lower.evcc_id = "ab:cd:ef:01:23:45";
            REQUIRE(ev::validate_config(lower).empty());

            auto upper = sane_config();
            upper.evcc_id = "AB:CD:EF:01:23:45";
            REQUIRE(ev::validate_config(upper).empty());
        }
    }
}
