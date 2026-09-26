// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <limits>

#include <iso15118/ev/sae_profile_emit.hpp>

using namespace iso15118;
namespace dt = message_20::datatypes;

namespace {

bool same_rational(const dt::RationalNumber& a, const dt::RationalNumber& b) {
    return a.value == b.value and a.exponent == b.exponent;
}

bool equals_float(const dt::RationalNumber& a, float value) {
    return same_rational(a, dt::from_float(value));
}

// A three-line EV on a ThreePhase connector sends each total on the base element alone.
void require_total(const dt::RationalNumber& base, const std::optional<dt::RationalNumber>& l2,
                   const std::optional<dt::RationalNumber>& l3, float expected) {
    REQUIRE(equals_float(base, expected));
    REQUIRE_FALSE(l2.has_value());
    REQUIRE_FALSE(l3.has_value());
}

void require_total(const std::optional<dt::RationalNumber>& base, const std::optional<dt::RationalNumber>& l2,
                   const std::optional<dt::RationalNumber>& l3, float expected) {
    REQUIRE(base.has_value());
    require_total(*base, l2, l3, expected);
}

// A single-line EV on a ThreePhase connector states each total on L1, with zero L2 and L3.
void require_line_one(const dt::RationalNumber& base, const std::optional<dt::RationalNumber>& l2,
                      const std::optional<dt::RationalNumber>& l3, float expected) {
    REQUIRE(equals_float(base, expected));
    REQUIRE(l2.has_value());
    REQUIRE(equals_float(*l2, 0.0f));
    REQUIRE(l3.has_value());
    REQUIRE(equals_float(*l3, 0.0f));
}

void require_line_one(const std::optional<dt::RationalNumber>& base, const std::optional<dt::RationalNumber>& l2,
                      const std::optional<dt::RationalNumber>& l3, float expected) {
    REQUIRE(base.has_value());
    require_line_one(*base, l2, l3, expected);
}

void require_replicated(const dt::RationalNumber& base, const std::optional<dt::RationalNumber>& l2,
                        const std::optional<dt::RationalNumber>& l3) {
    REQUIRE(l2.has_value());
    REQUIRE(same_rational(base, *l2));
    REQUIRE(l3.has_value());
    REQUIRE(same_rational(base, *l3));
}

// Every total is distinct and divisible by 3, so a swapped mapping changes an asserted base
// element and a SinglePhase connector divides it exactly.
ev::AcChargeParams three_phase_params() {
    ev::AcChargeParams params;
    params.phase_count = 3;
    params.max_charge_power = 11040.0f;
    params.min_charge_power = 690.0f;
    params.max_discharge_power = 10800.0f;
    params.min_discharge_power = 1380.0f;
    params.present_active_power = 3300.0f;
    return params;
}

constexpr std::uint32_t DISTINCT_SUPPORTED_MODES = 0x0B; // Charge, Discharge, EnterService

// The two power factors differ.
ev::SaeInverterProfile distinct_profile() {
    ev::SaeInverterProfile profile;
    profile.supported_modes = DISTINCT_SUPPORTED_MODES;
    profile.max_apparent_power_charging_var_absorption_va = 11100.0f;
    profile.max_apparent_power_charging_var_injection_va = 11250.0f;
    profile.max_apparent_power_discharging_var_absorption_va = 11400.0f;
    profile.max_apparent_power_discharging_var_injection_va = 11550.0f;
    profile.max_var_absorption_charging_var = 5100.0f;
    profile.max_var_injection_charging_var = 5250.0f;
    profile.max_var_absorption_discharging_var = 5400.0f;
    profile.max_var_injection_discharging_var = 5550.0f;
    profile.reactive_susceptance_s = 6.0f;
    profile.over_excited_power_factor = 0.9f;
    profile.over_excited_discharge_power_w = 9990.0f;
    profile.under_excited_power_factor = 0.85f;
    profile.under_excited_discharge_power_w = 9600.0f;
    profile.j3072_certified = true;
    profile.j3072_certification_date = 1'700'000'000;
    return profile;
}

} // namespace

SCENARIO("ISO15118-20 EV emits the SAE CPD transfer mode from the inverter profile") {

    GIVEN("A three-phase EV with per-field distinct limits") {
        const auto profile = distinct_profile();
        const auto params = three_phase_params();

        WHEN("the selected connector is ThreePhase") {
            const auto mode = ev::make_sae_cpd_transfer_mode(profile, params, dt::AcConnector::ThreePhase,
                                                             dt::Processing::Ongoing, 0x1, 42);

            THEN("each charge and discharge limit is its own total") {
                require_total(mode.max_charge_power, mode.max_charge_power_L2, mode.max_charge_power_L3, 11040.0f);
                require_total(mode.min_charge_power, mode.min_charge_power_L2, mode.min_charge_power_L3, 690.0f);
                require_total(mode.maximum_discharge_power, mode.maximum_discharge_power_L2,
                              mode.maximum_discharge_power_L3, 10800.0f);
                require_total(mode.minimum_discharge_power, mode.minimum_discharge_power_L2,
                              mode.minimum_discharge_power_L3, 1380.0f);
            }

            THEN("each apparent power maximum is its own total") {
                const auto& ap = mode.apparent_power_limits;
                require_total(ap.maximum_apparent_power_during_charging_and_var_absorption,
                              ap.maximum_apparent_power_during_charging_and_var_absorption_L2,
                              ap.maximum_apparent_power_during_charging_and_var_absorption_L3, 11100.0f);
                require_total(ap.maximum_apparent_power_during_charging_and_var_injection,
                              ap.maximum_apparent_power_during_charging_and_var_injection_L2,
                              ap.maximum_apparent_power_during_charging_and_var_injection_L3, 11250.0f);
                require_total(ap.maximum_apparent_power_during_discharging_and_var_absorption,
                              ap.maximum_apparent_power_during_discharging_and_var_absorption_L2,
                              ap.maximum_apparent_power_during_discharging_and_var_absorption_L3, 11400.0f);
                require_total(ap.maximum_apparent_power_during_discharging_and_var_injection,
                              ap.maximum_apparent_power_during_discharging_and_var_injection_L2,
                              ap.maximum_apparent_power_during_discharging_and_var_injection_L3, 11550.0f);
            }

            THEN("each reactive power maximum is its own total") {
                const auto& rp = mode.reactive_power_limits;
                require_total(rp.maximum_var_absorption_during_charging, rp.maximum_var_absorption_during_charging_L2,
                              rp.maximum_var_absorption_during_charging_L3, 5100.0f);
                require_total(rp.maximum_var_injection_during_charging, rp.maximum_var_injection_during_charging_L2,
                              rp.maximum_var_injection_during_charging_L3, 5250.0f);
                require_total(rp.maximum_var_absorption_during_discharging,
                              rp.maximum_var_absorption_during_discharging_L2,
                              rp.maximum_var_absorption_during_discharging_L3, 5400.0f);
                require_total(rp.maximum_var_injection_during_discharging,
                              rp.maximum_var_injection_during_discharging_L2,
                              rp.maximum_var_injection_during_discharging_L3, 5550.0f);
            }

            THEN("reactive susceptance is a total") {
                const auto& rp = mode.reactive_power_limits;
                require_total(rp.reactive_susceptance, rp.reactive_susceptance_L2, rp.reactive_susceptance_L3, 6.0f);
            }

            THEN("the power factors keep their distinct values, byte-identical per line") {
                const auto& ex = mode.excitation_limits;
                REQUIRE(equals_float(ex.specified_over_excited_power_factor, 0.9f));
                require_replicated(ex.specified_over_excited_power_factor, ex.specified_over_excited_power_factor_L2,
                                   ex.specified_over_excited_power_factor_L3);
                REQUIRE(equals_float(ex.specified_under_excited_power_factor, 0.85f));
                require_replicated(ex.specified_under_excited_power_factor, ex.specified_under_excited_power_factor_L2,
                                   ex.specified_under_excited_power_factor_L3);
            }

            THEN("the excitation discharge powers split rather than replicate") {
                const auto& ex = mode.excitation_limits;
                require_total(ex.specified_over_excited_discharge_power, ex.specified_over_excited_discharge_power_L2,
                              ex.specified_over_excited_discharge_power_L3, 9990.0f);
                require_total(ex.specified_under_excited_discharge_power, ex.specified_under_excited_discharge_power_L2,
                              ex.specified_under_excited_discharge_power_L3, 9600.0f);
            }

            THEN("supported modes come from the profile, the rest from the arguments") {
                REQUIRE(mode.supported_modes == DISTINCT_SUPPORTED_MODES);
                REQUIRE(mode.enabled_modes == 0x1);
                REQUIRE(mode.processing == dt::Processing::Ongoing);
                REQUIRE(mode.update_time == 42);
            }

            THEN("J3072 is certified, with the date in microseconds of SECC time") {
                REQUIRE(mode.j3072_certified);
                REQUIRE(mode.j3072_certification_date == 1'700'000'000'000'000ULL);
            }
        }

        WHEN("the profile sets an unused mode bit and a date beyond the microsecond range") {
            auto wide_profile = profile;
            wide_profile.supported_modes |= 1u << 2; // unused in AMD1 Table M.6
            wide_profile.j3072_certification_date = std::numeric_limits<std::uint64_t>::max() / 1'000'000 + 1;
            const auto mode = ev::make_sae_cpd_transfer_mode(wide_profile, params, dt::AcConnector::ThreePhase,
                                                             dt::Processing::Ongoing, 0x1, 42);

            THEN("the unused bit is masked out and the date saturates") {
                REQUIRE(mode.supported_modes == DISTINCT_SUPPORTED_MODES);
                REQUIRE(mode.j3072_certification_date == std::numeric_limits<std::uint64_t>::max());
            }
        }

        WHEN("the selected connector is SinglePhase") {
            const auto mode = ev::make_sae_cpd_transfer_mode(profile, params, dt::AcConnector::SinglePhase,
                                                             dt::Processing::Finished, 0x3, 0);

            THEN("only the base elements are emitted") {
                REQUIRE(equals_float(mode.max_charge_power, 3680.0f));
                REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
                REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
                REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
                REQUIRE_FALSE(mode.maximum_discharge_power_L2.has_value());
                REQUIRE_FALSE(mode.minimum_discharge_power_L2.has_value());
                REQUIRE_FALSE(mode.reactive_power_limits.reactive_susceptance_L2.has_value());
            }

            THEN("each total is divided across the EV's three lines and the power factors are not") {
                const auto& ex = mode.excitation_limits;
                REQUIRE(equals_float(
                    mode.apparent_power_limits.maximum_apparent_power_during_charging_and_var_absorption, 3700.0f));
                REQUIRE(equals_float(mode.reactive_power_limits.maximum_var_absorption_during_charging, 1700.0f));
                REQUIRE(equals_float(mode.reactive_power_limits.reactive_susceptance, 2.0f));
                REQUIRE(equals_float(ex.specified_over_excited_discharge_power, 3330.0f));
                REQUIRE(equals_float(ex.specified_over_excited_power_factor, 0.9f));
                REQUIRE(equals_float(ex.specified_under_excited_power_factor, 0.85f));
            }

            THEN("the power factors have no L2/L3") {
                const auto& ex = mode.excitation_limits;
                REQUIRE_FALSE(ex.specified_over_excited_power_factor_L2.has_value());
                REQUIRE_FALSE(ex.specified_over_excited_power_factor_L3.has_value());
                REQUIRE_FALSE(ex.specified_under_excited_power_factor_L2.has_value());
                REQUIRE_FALSE(ex.specified_under_excited_power_factor_L3.has_value());
            }
        }

        WHEN("a single-phase EV selects a ThreePhase connector") {
            auto single_phase_params = params;
            single_phase_params.phase_count = 1;
            const auto mode = ev::make_sae_cpd_transfer_mode(profile, single_phase_params, dt::AcConnector::ThreePhase,
                                                             dt::Processing::Finished, 0x3, 0);

            THEN("each charge and discharge total lands on the base element with explicit zero peers") {
                require_line_one(mode.max_charge_power, mode.max_charge_power_L2, mode.max_charge_power_L3, 11040.0f);
                require_line_one(mode.min_charge_power, mode.min_charge_power_L2, mode.min_charge_power_L3, 690.0f);
                require_line_one(mode.maximum_discharge_power, mode.maximum_discharge_power_L2,
                                 mode.maximum_discharge_power_L3, 10800.0f);
                require_line_one(mode.minimum_discharge_power, mode.minimum_discharge_power_L2,
                                 mode.minimum_discharge_power_L3, 1380.0f);
            }

            THEN("each profile total lands on the base element with explicit zero peers") {
                const auto& ap = mode.apparent_power_limits;
                require_line_one(ap.maximum_apparent_power_during_charging_and_var_absorption,
                                 ap.maximum_apparent_power_during_charging_and_var_absorption_L2,
                                 ap.maximum_apparent_power_during_charging_and_var_absorption_L3, 11100.0f);
                require_line_one(ap.maximum_apparent_power_during_charging_and_var_injection,
                                 ap.maximum_apparent_power_during_charging_and_var_injection_L2,
                                 ap.maximum_apparent_power_during_charging_and_var_injection_L3, 11250.0f);
                require_line_one(ap.maximum_apparent_power_during_discharging_and_var_absorption,
                                 ap.maximum_apparent_power_during_discharging_and_var_absorption_L2,
                                 ap.maximum_apparent_power_during_discharging_and_var_absorption_L3, 11400.0f);
                require_line_one(ap.maximum_apparent_power_during_discharging_and_var_injection,
                                 ap.maximum_apparent_power_during_discharging_and_var_injection_L2,
                                 ap.maximum_apparent_power_during_discharging_and_var_injection_L3, 11550.0f);
                const auto& rp = mode.reactive_power_limits;
                require_line_one(rp.maximum_var_absorption_during_charging,
                                 rp.maximum_var_absorption_during_charging_L2,
                                 rp.maximum_var_absorption_during_charging_L3, 5100.0f);
                require_line_one(rp.maximum_var_injection_during_charging, rp.maximum_var_injection_during_charging_L2,
                                 rp.maximum_var_injection_during_charging_L3, 5250.0f);
                require_line_one(rp.maximum_var_absorption_during_discharging,
                                 rp.maximum_var_absorption_during_discharging_L2,
                                 rp.maximum_var_absorption_during_discharging_L3, 5400.0f);
                require_line_one(rp.maximum_var_injection_during_discharging,
                                 rp.maximum_var_injection_during_discharging_L2,
                                 rp.maximum_var_injection_during_discharging_L3, 5550.0f);
                require_line_one(rp.reactive_susceptance, rp.reactive_susceptance_L2, rp.reactive_susceptance_L3, 6.0f);
                const auto& ex = mode.excitation_limits;
                require_line_one(ex.specified_over_excited_discharge_power,
                                 ex.specified_over_excited_discharge_power_L2,
                                 ex.specified_over_excited_discharge_power_L3, 9990.0f);
                require_line_one(ex.specified_under_excited_discharge_power,
                                 ex.specified_under_excited_discharge_power_L2,
                                 ex.specified_under_excited_discharge_power_L3, 9600.0f);
            }

            THEN("each power factor carries the same ratio on L2 and L3") {
                const auto& ex = mode.excitation_limits;
                REQUIRE(equals_float(ex.specified_over_excited_power_factor, 0.9f));
                require_replicated(ex.specified_over_excited_power_factor, ex.specified_over_excited_power_factor_L2,
                                   ex.specified_over_excited_power_factor_L3);
                REQUIRE(equals_float(ex.specified_under_excited_power_factor, 0.85f));
                require_replicated(ex.specified_under_excited_power_factor, ex.specified_under_excited_power_factor_L2,
                                   ex.specified_under_excited_power_factor_L3);
            }
        }
    }

    GIVEN("A default-constructed profile") {
        const ev::SaeInverterProfile profile;
        const auto params = three_phase_params();

        WHEN("a transfer mode is built for a ThreePhase connector") {
            const auto mode = ev::make_sae_cpd_transfer_mode(profile, params, dt::AcConnector::ThreePhase,
                                                             dt::Processing::Finished, 0x3, 0);

            THEN("the mandatory rational members are all set from the profile defaults") {
                REQUIRE(equals_float(
                    mode.apparent_power_limits.maximum_apparent_power_during_charging_and_var_absorption, 11040.0f));
                REQUIRE(equals_float(mode.reactive_power_limits.maximum_var_absorption_during_charging, 5000.0f));
                REQUIRE(equals_float(mode.excitation_limits.specified_over_excited_power_factor, 0.9f));
                REQUIRE(equals_float(mode.nominal_voltage, 230.0f));
                REQUIRE(equals_float(mode.maximum_voltage, 253.0f));
                REQUIRE(equals_float(mode.minimum_voltage, 207.0f));
                REQUIRE(equals_float(mode.nominal_voltage_offset, 0.0f));
            }

            THEN("the IEEE 1547 and energy members come from the profile, not the wire defaults") {
                REQUIRE(mode.ieee1547_normal_category == dt::sae::IEEE1547NormalCategory::CategoryB);
                REQUIRE(mode.ieee1547_abnormal_category == dt::sae::IEEE1547AbnormalCategory::CategoryII);
                REQUIRE(mode.useable_watt_hours == 60000);
            }

            THEN("the inverter details carry the profile strings") {
                REQUIRE(mode.inverter_details.inverter_sw_version == profile.inverter_sw_version);
                REQUIRE(mode.inverter_details.inverter_manufacturer == profile.inverter_manufacturer);
                REQUIRE(mode.inverter_details.inverter_model == profile.inverter_model);
                REQUIRE(mode.inverter_details.inverter_serial_number == profile.inverter_serial_number);
                REQUIRE_FALSE(mode.inverter_details.inverter_hw_version.has_value());
            }

            THEN("all 13 optionals the inputs do not carry are absent") {
                const auto& rp = mode.reactive_power_limits;
                REQUIRE_FALSE(mode.session_total_discharge_energy_available.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_charging.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_charging_L2.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_charging_L3.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_charging.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_charging_L2.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_charging_L3.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_discharging.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_discharging_L2.has_value());
                REQUIRE_FALSE(rp.minimum_var_absorption_during_discharging_L3.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_discharging.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_discharging_L2.has_value());
                REQUIRE_FALSE(rp.minimum_var_injection_during_discharging_L3.has_value());
            }
        }
    }
}

SCENARIO("ISO15118-20 EV emits the SAE dynamic CL control mode") {

    GIVEN("A three-phase EV on a ThreePhase connector") {
        const auto profile = distinct_profile();
        const auto params = three_phase_params();

        WHEN("service is permitted with the profile's default On/Connected state") {
            const auto mode = ev::make_sae_cl_control_mode(profile, params, dt::AcConnector::ThreePhase, 231.5f, 50.02f,
                                                           0x0, 0x3, true, 7);

            THEN("the DER state reads On/Connected") {
                REQUIRE(mode.der_operational_state == dt::sae::DEROperationalState::On);
                REQUIRE(mode.der_connection_status == dt::sae::DERConnectionStatus::Connected);
            }

            THEN("charge, discharge and present active power are each their own total") {
                require_total(mode.max_charge_power, mode.max_charge_power_L2, mode.max_charge_power_L3, 11040.0f);
                require_total(mode.min_charge_power, mode.min_charge_power_L2, mode.min_charge_power_L3, 690.0f);
                require_total(mode.maximum_discharge_power, mode.maximum_discharge_power_L2,
                              mode.maximum_discharge_power_L3, 10800.0f);
                require_total(mode.minimum_discharge_power, mode.minimum_discharge_power_L2,
                              mode.minimum_discharge_power_L3, 1380.0f);
                require_total(mode.present_active_power, mode.present_active_power_L2, mode.present_active_power_L3,
                              3300.0f);
            }

            THEN("present reactive power is zero on the base element alone") {
                REQUIRE(same_rational(mode.present_reactive_power, {0, 0}));
                REQUIRE_FALSE(mode.present_reactive_power_L2.has_value());
                REQUIRE_FALSE(mode.present_reactive_power_L3.has_value());
            }

            THEN("present voltage and frequency come from the arguments") {
                REQUIRE(same_rational(mode.present_voltage, dt::from_float(231.5f)));
                REQUIRE(same_rational(mode.present_frequency, dt::from_float(50.02f)));
            }

            THEN("energy requests are zero and the optional blocks are empty") {
                REQUIRE(same_rational(mode.target_energy_request, {0, 0}));
                REQUIRE(same_rational(mode.max_energy_request, {0, 0}));
                REQUIRE(same_rational(mode.min_energy_request, {0, 0}));
                REQUIRE_FALSE(mode.departure_time.has_value());
                REQUIRE_FALSE(mode.apparent_power.has_value());
                REQUIRE_FALSE(mode.reactive_power.has_value());
                REQUIRE_FALSE(mode.excitation.has_value());
                REQUIRE_FALSE(mode.maximum_v2x_energy_request.has_value());
                REQUIRE_FALSE(mode.minimum_v2x_energy_request.has_value());
                REQUIRE_FALSE(mode.session_total_discharge_energy_available.has_value());
            }

            THEN("durations come from the profile, status words from the arguments") {
                REQUIRE(mode.minimum_charging_duration == profile.minimum_charging_duration_s);
                REQUIRE(mode.duration_maximum_charge_rate == profile.duration_maximum_charge_rate_s);
                REQUIRE(mode.duration_maximum_discharge_rate == profile.duration_maximum_discharge_rate_s);
                REQUIRE(mode.der_alarm_status == 0x0);
                REQUIRE(mode.enabled_modes == 0x3);
                REQUIRE(mode.update_time == 7);
            }
        }

        WHEN("service is permitted but the profile declares Off/Disconnected") {
            auto off_profile = profile;
            off_profile.operational_state = dt::sae::DEROperationalState::Off;
            off_profile.connection_status = dt::sae::DERConnectionStatus::Disconnected;
            const auto mode = ev::make_sae_cl_control_mode(off_profile, params, dt::AcConnector::ThreePhase, 230.0f,
                                                           50.0f, 0x0, 0x3, true, 9);

            THEN("the DER state reads the profile's Off/Disconnected") {
                REQUIRE(mode.der_operational_state == dt::sae::DEROperationalState::Off);
                REQUIRE(mode.der_connection_status == dt::sae::DERConnectionStatus::Disconnected);
            }
        }

        WHEN("service is not permitted") {
            const auto mode = ev::make_sae_cl_control_mode(profile, params, dt::AcConnector::ThreePhase, 230.0f, 50.0f,
                                                           0x1, 0x3, false, 8);

            THEN("the DER state reads Off/Disconnected regardless of the profile") {
                REQUIRE(mode.der_operational_state == dt::sae::DEROperationalState::Off);
                REQUIRE(mode.der_connection_status == dt::sae::DERConnectionStatus::Disconnected);
                REQUIRE(mode.der_alarm_status == 0x1);
            }
        }

        WHEN("the selected connector is SinglePhase") {
            const auto mode = ev::make_sae_cl_control_mode(profile, params, dt::AcConnector::SinglePhase, 230.0f, 50.0f,
                                                           0x0, 0x3, true, 10);

            THEN("limits are divided, the present power is not, and no L2/L3 elements are emitted") {
                REQUIRE(equals_float(mode.max_charge_power, 3680.0f));
                REQUIRE(equals_float(mode.present_active_power, 3300.0f));
                REQUIRE_FALSE(mode.max_charge_power_L2.has_value());
                REQUIRE_FALSE(mode.max_charge_power_L3.has_value());
                REQUIRE_FALSE(mode.min_charge_power_L2.has_value());
                REQUIRE_FALSE(mode.min_charge_power_L3.has_value());
                REQUIRE_FALSE(mode.maximum_discharge_power_L2.has_value());
                REQUIRE_FALSE(mode.maximum_discharge_power_L3.has_value());
                REQUIRE_FALSE(mode.minimum_discharge_power_L2.has_value());
                REQUIRE_FALSE(mode.minimum_discharge_power_L3.has_value());
                REQUIRE_FALSE(mode.present_active_power_L2.has_value());
                REQUIRE_FALSE(mode.present_active_power_L3.has_value());
            }
        }
    }
}
