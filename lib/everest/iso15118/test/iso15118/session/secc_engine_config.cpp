// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
//
// The config mappers the DIN SPEC 70121 and ISO 15118-2 engines share between building the session
// config and applying a mid-session control event. EvseManager keeps pushing DC limits (energy
// management) and physical values (power-supply capabilities) for the whole session, so both paths must
// land on the same fields.
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <utility>
#include <vector>

#include <iso15118/detail/session/d2_secc_engine.hpp>
#include <iso15118/detail/session/din_secc_engine.hpp>

using namespace iso15118;

namespace {
namespace m20dt = message_20::datatypes;

d20::DcTransferLimits make_limits(float max_current, float max_power, float max_voltage, float min_current = 5.0f,
                                  float min_voltage = 150.0f) {
    d20::DcTransferLimits limits{};
    limits.charge_limits.current.max = m20dt::from_float(max_current);
    limits.charge_limits.current.min = m20dt::from_float(min_current);
    limits.charge_limits.power.max = m20dt::from_float(max_power);
    limits.charge_limits.power.min = m20dt::from_float(0.0f);
    limits.voltage.max = m20dt::from_float(max_voltage);
    limits.voltage.min = m20dt::from_float(min_voltage);
    return limits;
}
} // namespace

namespace {
session::SessionConfig make_session_config(std::vector<shared_datatypes::EnergyTransferMode> pre20_modes,
                                           std::vector<m20dt::ServiceCategory> services = {}) {
    session::EvseSetupConfig setup{};
    setup.evse_id = "DE*PNX*E12345*1";
    setup.pre20_energy_transfer_modes = std::move(pre20_modes);
    setup.supported_energy_services = std::move(services);
    return session::SessionConfig(setup);
}
} // namespace

SCENARIO("DIN SPEC 70121 SECC offered energy transfer mode") {
    using Mode = shared_datatypes::EnergyTransferMode;
    using Offered = message_din::datatypes::SupportedEnergyTransferMode;

    GIVEN("A module configured for DC_core only") {
        const auto cfg = make_din_config(make_session_config({Mode::DC_core}));
        THEN("DC_core is offered") {
            REQUIRE(cfg.energy_transfer_mode == Offered::DC_core);
        }
    }

    GIVEN("A module configured for both DC modes, DC_core listed first") {
        const auto cfg = make_din_config(make_session_config({Mode::DC_core, Mode::DC_extended}));
        THEN("DC_extended wins: the ChargeService advertises a single mode") {
            REQUIRE(cfg.energy_transfer_mode == Offered::DC_extended);
        }
    }

    GIVEN("A module configured for modes DIN does not serve") {
        const auto cfg = make_din_config(make_session_config({Mode::AC_three_phase_core, Mode::DC_combo_core}));
        THEN("The DC_extended default stands") {
            REQUIRE(cfg.energy_transfer_mode == Offered::DC_extended);
        }
    }

    GIVEN("A module that never configured pre-20 modes") {
        const auto cfg = make_din_config(make_session_config({}));
        THEN("The DC_extended default stands") {
            REQUIRE(cfg.energy_transfer_mode == Offered::DC_extended);
        }
    }
}

SCENARIO("ISO 15118-2 SECC advertised energy transfer modes") {
    using Mode = shared_datatypes::EnergyTransferMode;

    GIVEN("A module configured for DC_core and DC_unique") {
        const auto cfg = make_d2_config(make_session_config({Mode::DC_core, Mode::DC_unique}), false);
        THEN("Exactly those are advertised, in that order") {
            REQUIRE(cfg.supported_energy_transfer_modes.size() == 2);
            REQUIRE(cfg.supported_energy_transfer_modes[0] == Mode::DC_core);
            REQUIRE(cfg.supported_energy_transfer_modes[1] == Mode::DC_unique);
        }
    }

    GIVEN("A module listing the same mode twice") {
        const auto cfg = make_d2_config(make_session_config({Mode::DC_extended, Mode::DC_extended}), false);
        THEN("It is advertised once") {
            REQUIRE(cfg.supported_energy_transfer_modes.size() == 1);
        }
    }

    GIVEN("A module that only provided -20 service categories") {
        const auto cfg = make_d2_config(make_session_config({}, {m20dt::ServiceCategory::DC}), false);
        THEN("The lossy fallback advertises DC_extended and DC_core") {
            REQUIRE(cfg.supported_energy_transfer_modes.size() == 2);
            REQUIRE(cfg.supported_energy_transfer_modes[0] == Mode::DC_extended);
            REQUIRE(cfg.supported_energy_transfer_modes[1] == Mode::DC_core);
        }
    }
}

SCENARIO("DIN SPEC 70121 SECC config mapping") {

    GIVEN("A set of DC transfer limits") {
        din::SessionConfig config;
        apply_dc_limits(config, make_limits(250.0f, 120000.0f, 850.0f));

        THEN("The charge-loop limits follow them") {
            REQUIRE(config.evse_maximum_current_limit == 250.0);
            REQUIRE(config.evse_maximum_power_limit.value() == 120000.0);
            REQUIRE(config.evse_maximum_voltage_limit == 850.0);
        }

        // This is the mid-session path: EvseManager lowers the site limit and the next
        // CurrentDemandRes must announce the new value so the EV throttles; the
        // ChargeParameterDiscoveryRes offer (the capabilities below) stays untouched.
        WHEN("Energy management lowers the limits") {
            apply_dc_limits(config, make_limits(60.0f, 30000.0f, 850.0f));

            THEN("The lowered values replace the previous ones") {
                REQUIRE(config.evse_maximum_current_limit == 60.0);
                REQUIRE(config.evse_maximum_power_limit.value() == 30000.0);
            }
        }
    }

    GIVEN("DC transfer limits that carry no maxima or negative (invalid) values") {
        din::SessionConfig config;
        apply_dc_limits(config, make_limits(0.0f, -30000.0f, 0.0f, 0.0f, 0.0f));

        THEN("The advertised limits are 0, never an invented default (safety)") {
            REQUIRE(config.evse_maximum_current_limit == 0.0);
            REQUIRE(config.evse_maximum_power_limit.value() == 0.0);
            REQUIRE(config.evse_maximum_voltage_limit == 0.0);
        }
    }

    GIVEN("Power-supply capabilities") {
        din::SessionConfig config;
        apply_dc_capabilities(config, make_limits(500.0f, 250000.0f, 1000.0f));

        THEN("The ChargeParameterDiscoveryRes offer (maxima and minima) follows them") {
            REQUIRE(config.evse_capability_maximum_current_limit == 500.0);
            REQUIRE(config.evse_capability_maximum_power_limit.value() == 250000.0);
            REQUIRE(config.evse_capability_maximum_voltage_limit == 1000.0);
            REQUIRE(config.evse_minimum_current_limit == 5.0);
            REQUIRE(config.evse_minimum_voltage_limit == 150.0);
        }
    }

    GIVEN("Capabilities carrying negative (invalid) values") {
        din::SessionConfig config;
        apply_dc_capabilities(config, make_limits(-500.0f, -250000.0f, -1000.0f, -5.0f, -150.0f));

        THEN("They clamp to 0 -- an invented capability is never advertised (safety)") {
            REQUIRE(config.evse_capability_maximum_current_limit == 0.0);
            REQUIRE(config.evse_capability_maximum_power_limit.value() == 0.0);
            REQUIRE(config.evse_capability_maximum_voltage_limit == 0.0);
            REQUIRE(config.evse_minimum_current_limit == 0.0);
            REQUIRE(config.evse_minimum_voltage_limit == 0.0);
        }
    }

    GIVEN("Capabilities that were never reported") {
        din::SessionConfig config;
        apply_dc_capabilities(config, make_limits(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));

        THEN("The offer stays 0 instead of being lifted to a default (safety)") {
            REQUIRE(config.evse_capability_maximum_current_limit == 0.0);
            REQUIRE(config.evse_capability_maximum_power_limit.value() == 0.0);
            REQUIRE(config.evse_capability_maximum_voltage_limit == 0.0);
        }
    }

    GIVEN("Physical values reported by the module") {
        din::SessionConfig config;
        config.evse_peak_current_ripple = 1.0;

        d20::PhysicalValues values;
        values.dc_peak_current_ripple = 3.5f;
        values.dc_current_regulation_tolerance = 2.0f;
        values.dc_energy_to_be_delivered = 10000.0f;
        apply_physical_values(config, values);

        THEN("They replace the defaults and the optional elements become present") {
            REQUIRE(config.evse_peak_current_ripple == 3.5);
            REQUIRE(config.evse_current_regulation_tolerance.value() == 2.0);
            REQUIRE(config.evse_energy_to_be_delivered.value() == 10000.0);
        }
    }

    GIVEN("No physical values reported by the module") {
        din::SessionConfig config;
        config.evse_peak_current_ripple = 1.0;
        apply_physical_values(config, d20::PhysicalValues{});

        THEN("The mandatory ripple keeps its value and the optional elements stay absent") {
            REQUIRE(config.evse_peak_current_ripple == 1.0);
            REQUIRE_FALSE(config.evse_current_regulation_tolerance.has_value());
            REQUIRE_FALSE(config.evse_energy_to_be_delivered.has_value());
        }
    }
}

SCENARIO("ISO 15118-2 SECC config mapping") {

    GIVEN("A set of DC transfer limits") {
        d2::SessionConfig config;
        apply_dc_limits(config, make_limits(250.0f, 120000.0f, 850.0f));

        THEN("The charge-loop limits follow them") {
            REQUIRE(config.dc_max_current == 250.0f);
            REQUIRE(config.dc_max_power == 120000.0f);
            REQUIRE(config.dc_max_voltage == 850.0f);
        }

        WHEN("Energy management lowers the limits") {
            apply_dc_limits(config, make_limits(60.0f, 30000.0f, 850.0f));

            THEN("The lowered values replace the previous ones") {
                REQUIRE(config.dc_max_current == 60.0f);
                REQUIRE(config.dc_max_power == 30000.0f);
            }
        }
    }

    GIVEN("DC transfer limits that carry no maxima or negative (invalid) values") {
        d2::SessionConfig config;
        apply_dc_limits(config, make_limits(0.0f, -30000.0f, 0.0f, 0.0f, 0.0f));

        THEN("The advertised limits are 0, never an invented default (safety)") {
            REQUIRE(config.dc_max_current == 0.0f);
            REQUIRE(config.dc_max_power == 0.0f);
            REQUIRE(config.dc_max_voltage == 0.0f);
        }
    }

    GIVEN("Power-supply capabilities") {
        d2::SessionConfig config;
        apply_dc_capabilities(config, make_limits(500.0f, 250000.0f, 1000.0f));

        THEN("The ChargeParameterDiscoveryRes offer (maxima and minima) follows them") {
            REQUIRE(config.dc_capability_max_current == 500.0f);
            REQUIRE(config.dc_capability_max_power == 250000.0f);
            REQUIRE(config.dc_capability_max_voltage == 1000.0f);
            REQUIRE(config.dc_min_current == 5.0f);
            REQUIRE(config.dc_min_voltage == 150.0f);
        }
    }

    GIVEN("Capabilities carrying negative (invalid) values") {
        d2::SessionConfig config;
        apply_dc_capabilities(config, make_limits(-500.0f, -250000.0f, -1000.0f, -5.0f, -150.0f));

        THEN("They clamp to 0 -- an invented capability is never advertised (safety)") {
            REQUIRE(config.dc_capability_max_current == 0.0f);
            REQUIRE(config.dc_capability_max_power == 0.0f);
            REQUIRE(config.dc_capability_max_voltage == 0.0f);
            REQUIRE(config.dc_min_current == 0.0f);
            REQUIRE(config.dc_min_voltage == 0.0f);
        }
    }

    GIVEN("Capabilities that were never reported") {
        d2::SessionConfig config;
        apply_dc_capabilities(config, make_limits(0.0f, 0.0f, 0.0f, 0.0f, 0.0f));

        THEN("The offer stays 0 instead of being lifted to a default (safety)") {
            REQUIRE(config.dc_capability_max_current == 0.0f);
            REQUIRE(config.dc_capability_max_power == 0.0f);
            REQUIRE(config.dc_capability_max_voltage == 0.0f);
        }
    }

    GIVEN("A module reporting both power-supply capabilities and energy-management limits") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        setup.powersupply_limits = make_limits(500.0f, 250000.0f, 1000.0f);
        setup.dc_limits = make_limits(60.0f, 30000.0f, 850.0f);
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The CPD offer comes from the capabilities, the charge loop from the limits") {
            REQUIRE(cfg.dc_capability_max_current == 500.0f);
            REQUIRE(cfg.dc_capability_max_power == 250000.0f);
            REQUIRE(cfg.dc_max_current == 60.0f);
            REQUIRE(cfg.dc_max_power == 30000.0f);
        }
    }

    GIVEN("A module that never reported power-supply capabilities") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        setup.dc_limits = make_limits(60.0f, 30000.0f, 850.0f);
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The CPD offer falls back to the energy-management limits (fake-DC mode)") {
            REQUIRE(cfg.dc_capability_max_current == 60.0f);
            REQUIRE(cfg.dc_capability_max_power == 30000.0f);
            REQUIRE(cfg.dc_capability_max_voltage == 850.0f);
        }
    }

    GIVEN("A module reporting the hardware AC power and a lower live current limit") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        setup.ac_limits.charge_power.max = m20dt::from_float(7360.0f); // 32 A at 230 V
        setup.iso2_ac_max_current = 16.0f;
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The CPD capability is the nominal current, the charge loop the live limit") {
            REQUIRE(cfg.ac_capability_max_current == 32.0f);
            REQUIRE(cfg.ac_max_current == 16.0f);
        }
    }

    GIVEN("A three-phase AC charger reporting its hardware power") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        // update_ac_maximum_limits reports the power summed over all phases: 3 * 32 A * 230 V.
        setup.ac_limits.charge_power.max = m20dt::from_float(22080.0f);
        setup.pre20_energy_transfer_modes = {shared_datatypes::EnergyTransferMode::AC_single_phase_core,
                                             shared_datatypes::EnergyTransferMode::AC_three_phase_core};
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The capability current is per phase, so PMax over three phases is the hardware power") {
            REQUIRE(cfg.ac_capability_max_current == 32.0f);
        }
    }

    GIVEN("A single-phase AC charger reporting its hardware power") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        setup.ac_limits.charge_power.max = m20dt::from_float(7360.0f); // 1 * 32 A * 230 V
        setup.pre20_energy_transfer_modes = {shared_datatypes::EnergyTransferMode::AC_single_phase_core};
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The single phase carries the whole reported power") {
            REQUIRE(cfg.ac_capability_max_current == 32.0f);
        }
    }

    GIVEN("A module that only ever reported the live AC current limit") {
        session::EvseSetupConfig setup{};
        setup.evse_id = "DE*PNX*E12345*1";
        setup.iso2_ac_max_current = 16.0f;
        const auto cfg = make_d2_config(session::SessionConfig(setup), false);

        THEN("The live limit doubles as the capability instead of the 32 A default") {
            REQUIRE(cfg.ac_capability_max_current == 16.0f);
            REQUIRE(cfg.ac_max_current == 16.0f);
        }
    }

    GIVEN("Physical values reported by the module") {
        d2::SessionConfig config;

        d20::PhysicalValues values;
        values.ac_nominal_voltage = 400.0f;
        values.dc_peak_current_ripple = 3.5f;
        values.dc_current_regulation_tolerance = 2.0f;
        values.dc_energy_to_be_delivered = 10000.0f;
        apply_physical_values(config, values);

        THEN("They replace the defaults and the optional elements become present") {
            REQUIRE(config.ac_nominal_voltage == 400.0f);
            REQUIRE(config.dc_peak_current_ripple == 3.5f);
            REQUIRE(config.dc_current_regulation_tolerance.value() == 2.0f);
            REQUIRE(config.dc_energy_to_be_delivered.value() == 10000.0f);
        }
    }

    GIVEN("No physical values reported by the module") {
        d2::SessionConfig config;
        apply_physical_values(config, d20::PhysicalValues{});

        THEN("The mandatory elements keep their defaults and the optional ones stay absent") {
            REQUIRE(config.ac_nominal_voltage == 230.0f);
            REQUIRE_FALSE(config.dc_current_regulation_tolerance.has_value());
            REQUIRE_FALSE(config.dc_energy_to_be_delivered.has_value());
        }
    }
}

SCENARIO("Authorization Ongoing timeouts") {
    session::EvseSetupConfig setup{};
    setup.evse_id = "DE*EVR*E12345*1";

    GIVEN("The EvseV2G-compatible defaults") {
        const session::SessionConfig config(setup);
        THEN("EIM waits 300 s and PnC the 55 s V2G_SECC_Ongoing_Performance_Time") {
            REQUIRE(config.auth_timeout_eim_s == 300);
            REQUIRE(config.auth_timeout_pnc_s == 55);
        }
        THEN("both reach the ISO 15118-2 engine, in milliseconds") {
            const auto cfg = make_d2_config(config, false);
            REQUIRE(cfg.auth_timeout_eim_ms == 300000);
            REQUIRE(cfg.auth_timeout_pnc_ms == 55000);
        }
        THEN("the DIN engine gets the EIM value: it knows no other payment option") {
            REQUIRE(make_din_config(config).auth_timeout_eim_ms == 300000);
        }
    }

    GIVEN("Configured values") {
        setup.auth_timeout_eim_s = 120;
        setup.auth_timeout_pnc_s = 30;
        const session::SessionConfig config(setup);
        THEN("they reach both engines") {
            const auto d2_cfg = make_d2_config(config, false);
            REQUIRE(d2_cfg.auth_timeout_eim_ms == 120000);
            REQUIRE(d2_cfg.auth_timeout_pnc_ms == 30000);
            REQUIRE(make_din_config(config).auth_timeout_eim_ms == 120000);
        }
    }

    GIVEN("0, meaning wait indefinitely (EvseV2G parity)") {
        setup.auth_timeout_eim_s = 0;
        setup.auth_timeout_pnc_s = 0;
        const session::SessionConfig config(setup);
        THEN("0 passes through rather than becoming an immediately-expired window") {
            const auto d2_cfg = make_d2_config(config, false);
            REQUIRE(d2_cfg.auth_timeout_eim_ms == 0);
            REQUIRE(d2_cfg.auth_timeout_pnc_ms == 0);
            REQUIRE(make_din_config(config).auth_timeout_eim_ms == 0);
        }
    }

    GIVEN("A value whose millisecond conversion would overflow uint32_t") {
        THEN("it saturates instead of wrapping to a near-instant timeout") {
            static constexpr uint32_t MAX_S = std::numeric_limits<uint32_t>::max() / 1000;
            REQUIRE(session::auth_timeout_to_ms(MAX_S) == MAX_S * 1000);
            REQUIRE(session::auth_timeout_to_ms(MAX_S + 1) == MAX_S * 1000);
            REQUIRE(session::auth_timeout_to_ms(std::numeric_limits<uint32_t>::max()) == MAX_S * 1000);
        }
    }
}
