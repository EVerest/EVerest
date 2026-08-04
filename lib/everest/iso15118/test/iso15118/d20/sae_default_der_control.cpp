// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

#include <iso15118/d20/config.hpp>
#include <iso15118/detail/d20/config_validation.hpp>

namespace sae = iso15118::sae;

SCENARIO("Default SAE DER control is inert") {

    GIVEN("The default SAE DER control") {
        const auto der_control = iso15118::d20::get_default_sae_der_control();

        THEN("Every grid code function is disabled") {
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.enable == false);
            REQUIRE(der_control.voltage_trip.under_voltage_must_trip_curve.enable == false);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.enable == false);
            REQUIRE(der_control.frequency_trip.under_frequency_must_trip_curve.enable == false);

            REQUIRE(der_control.enter_service.permit_service == false);

            REQUIRE(der_control.reactive_power_support.constant_power_factor.enable == false);
            REQUIRE(der_control.reactive_power_support.volt_var.enable == false);
            REQUIRE(der_control.reactive_power_support.watt_var.enable == false);
            REQUIRE(der_control.reactive_power_support.constant_var.enable == false);

            REQUIRE(der_control.active_power_support.frequency_droop.enable == false);
            REQUIRE(der_control.active_power_support.volt_watt.enable == false);
            REQUIRE(der_control.active_power_support.constant_watt.enable == false);
            REQUIRE(der_control.active_power_support.limit_max_discharge_power.enable == false);

            REQUIRE(der_control.reactive_power_support.volt_var.autonomous_reference_voltage_adjustment_enable ==
                    false);
        }

        THEN("Every curve carries the expected units") {
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.x_unit == sae::DERUnit::s);
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.y_unit == sae::DERUnit::PercentageV);
            REQUIRE(der_control.voltage_trip.under_voltage_must_trip_curve.x_unit == sae::DERUnit::s);
            REQUIRE(der_control.voltage_trip.under_voltage_must_trip_curve.y_unit == sae::DERUnit::PercentageV);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.x_unit == sae::DERUnit::s);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.y_unit == sae::DERUnit::Hz);
            REQUIRE(der_control.frequency_trip.under_frequency_must_trip_curve.x_unit == sae::DERUnit::s);
            REQUIRE(der_control.frequency_trip.under_frequency_must_trip_curve.y_unit == sae::DERUnit::Hz);

            REQUIRE(der_control.reactive_power_support.volt_var.x_unit == sae::DERUnit::PercentageV);
            REQUIRE(der_control.reactive_power_support.volt_var.y_unit ==
                    sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
            REQUIRE(der_control.reactive_power_support.watt_var.x_unit ==
                    sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
            REQUIRE(der_control.reactive_power_support.watt_var.y_unit ==
                    sae::DERUnit::PercentageEVMaximumConfiguredReactivePower);
            REQUIRE(der_control.reactive_power_support.constant_var.unit == sae::DERUnit::var);

            REQUIRE(der_control.active_power_support.volt_watt.x_unit == sae::DERUnit::PercentageV);
            REQUIRE(der_control.active_power_support.volt_watt.y_unit ==
                    sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
            REQUIRE(der_control.active_power_support.constant_watt.unit ==
                    sae::DERUnit::PercentageEVMaximumConfiguredActivePower);
        }

        THEN("The trip curve thresholds and clearing times match the expected defaults") {
            const auto& over_voltage = der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points;
            REQUIRE(over_voltage[0].x_value == 2.0f);
            REQUIRE(over_voltage[0].y_value == 110.0f);
            REQUIRE(over_voltage[1].x_value == 0.16f);
            REQUIRE(over_voltage[1].y_value == 120.0f);

            const auto& under_voltage = der_control.voltage_trip.under_voltage_must_trip_curve.curve_data_points;
            REQUIRE(under_voltage[0].x_value == 2.0f);
            REQUIRE(under_voltage[0].y_value == 88.0f);
            REQUIRE(under_voltage[1].x_value == 0.16f);
            REQUIRE(under_voltage[1].y_value == 50.0f);

            const auto& over_frequency = der_control.frequency_trip.over_frequency_must_trip_curve.curve_data_points;
            REQUIRE(over_frequency[0].x_value == 300.0f);
            REQUIRE(over_frequency[0].y_value == 51.5f);
            REQUIRE(over_frequency[1].x_value == 0.16f);
            REQUIRE(over_frequency[1].y_value == 52.0f);

            const auto& under_frequency = der_control.frequency_trip.under_frequency_must_trip_curve.curve_data_points;
            REQUIRE(under_frequency[0].x_value == 300.0f);
            REQUIRE(under_frequency[0].y_value == 47.5f);
            REQUIRE(under_frequency[1].x_value == 0.16f);
            REQUIRE(under_frequency[1].y_value == 47.0f);
        }

        THEN("The enter service thresholds and setpoints match the expected defaults") {
            REQUIRE(der_control.enter_service.enter_service_voltage_high == 105.0f);
            REQUIRE(der_control.enter_service.enter_service_voltage_low == 91.7f);
            REQUIRE(der_control.enter_service.enter_service_frequency_high == 50.1f);
            REQUIRE(der_control.enter_service.enter_service_frequency_low == 49.5f);

            REQUIRE(der_control.reactive_power_support.constant_power_factor.power_factor_value == 1.0f);
            REQUIRE(der_control.reactive_power_support.constant_power_factor.power_factor_excitation ==
                    sae::PowerFactorExcitation::OverExcited);
            REQUIRE(der_control.reactive_power_support.constant_var.var_setpoint == 0.0f);
            REQUIRE(der_control.reactive_power_support.volt_var.reference_voltage == 100.0f);
            REQUIRE(der_control.active_power_support.constant_watt.watt_setpoint == 0.0f);
            REQUIRE(der_control.active_power_support.limit_max_discharge_power.percentage_value == 100);
        }

        THEN("Each mandatory trip curve carries the schema minimum of two data points") {
            REQUIRE(der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.voltage_trip.under_voltage_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.frequency_trip.over_frequency_must_trip_curve.curve_data_points.size() == 2);
            REQUIRE(der_control.frequency_trip.under_frequency_must_trip_curve.curve_data_points.size() == 2);
        }

        THEN("The optional trip curves are unset") {
            REQUIRE(der_control.voltage_trip.over_voltage_momentary_cessation_trip_curve.has_value() == false);
            REQUIRE(der_control.voltage_trip.under_voltage_momentary_cessation_trip_curve.has_value() == false);
            REQUIRE(der_control.voltage_trip.over_voltage_may_trip_curve.has_value() == false);
            REQUIRE(der_control.voltage_trip.under_voltage_may_trip_curve.has_value() == false);
            REQUIRE(der_control.frequency_trip.over_frequency_may_trip_curve.has_value() == false);
            REQUIRE(der_control.frequency_trip.under_frequency_may_trip_curve.has_value() == false);
        }

        THEN("The enter service delay and ramp values are unset") {
            REQUIRE(der_control.enter_service.enter_service_delay.has_value() == false);
            REQUIRE(der_control.enter_service.enter_service_ramp_time.has_value() == false);
        }

        THEN("The enter service randomized delay is present and zero") {
            // [V2G20-3364] requires either the randomized delay, the delay, or both.
            REQUIRE(der_control.enter_service.enter_service_randomized_delay.has_value() == true);
            REQUIRE(der_control.enter_service.enter_service_randomized_delay.value() == 0.0f);
        }
    }
}

namespace {

iso15118::d20::EvseSetupConfig
create_ac_der_sae_evse_setup(const std::optional<iso15118::d20::SaeDerTransferLimits>& sae_limits,
                             const std::optional<iso15118::d20::DerSaeSetupConfig>& sae_setup_config) {
    namespace dt = iso15118::message_20::datatypes;

    iso15118::d20::EvseSetupConfig setup{};
    setup.evse_id = "everest se";
    setup.supported_energy_services = {dt::ServiceCategory::AC_DER_SAE};
    setup.authorization_services = {dt::Authorization::EIM};
    setup.supported_vas_services = {};
    setup.enable_certificate_install_service = false;
    setup.dc_limits = {};
    setup.ac_limits = {};
    setup.der_iec_limits = std::nullopt;
    setup.der_sae_limits = sae_limits;
    setup.control_mobility_modes = {{dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc}};
    setup.der_sae_setup_config = sae_setup_config;
    setup.powersupply_limits = {};
    return setup;
}

} // namespace

SCENARIO("SessionConfig fills in a default SAE DER setup config") {
    namespace dt = iso15118::message_20::datatypes;

    const auto offers_ac_der_sae = [](const iso15118::d20::SessionConfig& config) {
        const auto& services = config.supported_energy_transfer_services;
        return std::find(services.begin(), services.end(), dt::ServiceCategory::AC_DER_SAE) != services.end();
    };

    GIVEN("AC_DER_SAE with limits but no setup config") {
        const auto config = iso15118::d20::SessionConfig(
            create_ac_der_sae_evse_setup(iso15118::d20::SaeDerTransferLimits{}, std::nullopt));

        THEN("The service is kept and the default DER control is filled in") {
            REQUIRE(offers_ac_der_sae(config) == true);
            REQUIRE(config.der_sae_setup_config.has_value() == true);
            REQUIRE(config.der_sae_setup_config->required_der_operating_mode ==
                    iso15118::sae::RequiredDEROperatingMode::GridFollowing);
            REQUIRE(config.der_sae_setup_config->grid_connection_mode ==
                    iso15118::sae::GridConnectionMode::GridConnected);
            REQUIRE(config.der_sae_setup_config->der_control.enter_service.permit_service == false);
        }
    }

    GIVEN("AC_DER_SAE without limits") {
        const auto config = iso15118::d20::SessionConfig(create_ac_der_sae_evse_setup(std::nullopt, std::nullopt));

        THEN("The service is stripped") {
            REQUIRE(offers_ac_der_sae(config) == false);
        }
    }

    GIVEN("AC_DER_SAE with a caller supplied setup config") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.enter_service.permit_service = true;
        der_control.reactive_power_support.volt_var.enable = true;

        const auto supplied = iso15118::d20::DerSaeSetupConfig(der_control, sae::RequiredDEROperatingMode::GridForming,
                                                               sae::GridConnectionMode::GridIslanded);

        const auto config =
            iso15118::d20::SessionConfig(create_ac_der_sae_evse_setup(iso15118::d20::SaeDerTransferLimits{}, supplied));

        THEN("The supplied config is kept rather than replaced by the default") {
            REQUIRE(offers_ac_der_sae(config) == true);
            REQUIRE(config.der_sae_setup_config.has_value() == true);
            REQUIRE(config.der_sae_setup_config->required_der_operating_mode ==
                    sae::RequiredDEROperatingMode::GridForming);
            REQUIRE(config.der_sae_setup_config->grid_connection_mode == sae::GridConnectionMode::GridIslanded);
            REQUIRE(config.der_sae_setup_config->der_control.enter_service.permit_service == true);
            REQUIRE(config.der_sae_setup_config->der_control.reactive_power_support.volt_var.enable == true);
        }
    }

    GIVEN("AC_DER_SAE with limits but no setup config") {
        const auto config = iso15118::d20::SessionConfig(
            create_ac_der_sae_evse_setup(iso15118::d20::SaeDerTransferLimits{}, std::nullopt));

        THEN("The filled in default carries a stamped update time") {
            REQUIRE(config.der_sae_setup_config.has_value() == true);
            REQUIRE(config.der_sae_setup_config->der_control_update_time > 0);
        }
    }
}

namespace {

iso15118::d20::SessionConfig make_session_config(const iso15118::d20::SaeDerTransferLimits& limits,
                                                 const sae::DERControl& der_control) {
    const auto setup = iso15118::d20::DerSaeSetupConfig(der_control, sae::RequiredDEROperatingMode::GridFollowing,
                                                        sae::GridConnectionMode::GridConnected);
    return iso15118::d20::SessionConfig(create_ac_der_sae_evse_setup(limits, setup));
}

bool offers_ac_der_sae(const iso15118::d20::SessionConfig& config) {
    namespace dt = iso15118::message_20::datatypes;

    const auto& services = config.supported_energy_transfer_services;
    return std::find(services.begin(), services.end(), dt::ServiceCategory::AC_DER_SAE) != services.end();
}

bool session_config_offers_ac_der_sae(const iso15118::d20::SaeDerTransferLimits& limits,
                                      const sae::DERControl& der_control) {
    return offers_ac_der_sae(make_session_config(limits, der_control));
}

sae::CurveDataPointsList single_point_curve() {
    sae::CurveDataPointsList points;
    points.push_back({100.0f, 1.0f});
    return points;
}

} // namespace

SCENARIO("SessionConfig validates a caller supplied SAE DER setup config") {

    const iso15118::d20::SaeDerTransferLimits valid_limits{};

    GIVEN("The unmodified shipped default") {
        THEN("The service is kept") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, iso15118::d20::get_default_sae_der_control()) ==
                    true);
        }
    }

    GIVEN("A must trip curve with a single data point") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points = single_point_curve();

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("A per phase must trip curve list with a single data point") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.frequency_trip.under_frequency_must_trip_curve.curve_data_points_L2 = single_point_curve();

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("A disabled volt var curve with a single data point") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.reactive_power_support.volt_var.enable = false;
        der_control.reactive_power_support.volt_var.curve_data_points = single_point_curve();

        THEN("The service is stripped because the block is sent regardless of its enable") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("An optional may trip curve with a single data point") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        auto may_trip = der_control.voltage_trip.over_voltage_must_trip_curve;
        may_trip.curve_data_points = single_point_curve();
        der_control.voltage_trip.over_voltage_may_trip_curve = may_trip;

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("Neither enter service delay set") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.enter_service.enter_service_delay = std::nullopt;
        der_control.enter_service.enter_service_randomized_delay = std::nullopt;

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("An enter service delay without a ramp time") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.enter_service.enter_service_delay = 10.0f;
        der_control.enter_service.enter_service_ramp_time = std::nullopt;

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == false);
        }
    }

    GIVEN("An enter service delay paired with a ramp time") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.enter_service.enter_service_delay = 10.0f;
        der_control.enter_service.enter_service_ramp_time = 5.0f;

        THEN("The service is kept") {
            REQUIRE(session_config_offers_ac_der_sae(valid_limits, der_control) == true);
        }
    }

    GIVEN("A positive var injection limit") {
        auto limits = valid_limits;
        limits.reactive_power_limits.maximum_var_injection_during_charging = {100, 0};

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(limits, iso15118::d20::get_default_sae_der_control()) == false);
        }
    }

    GIVEN("A negative var absorption limit") {
        auto limits = valid_limits;
        limits.reactive_power_limits.maximum_var_absorption_during_discharging_L3 = {-100, 0};

        THEN("The service is stripped") {
            REQUIRE(session_config_offers_ac_der_sae(limits, iso15118::d20::get_default_sae_der_control()) == false);
        }
    }

    GIVEN("A positive maximum discharge power") {
        auto limits = valid_limits;
        limits.max_discharge_power = {5, 3};

        const auto config = make_session_config(limits, iso15118::d20::get_default_sae_der_control());

        THEN("The service is stripped") {
            REQUIRE(offers_ac_der_sae(config) == false);
        }

        THEN("The setup config and the limits stay populated") {
            // The downstream readers are gated by the offered services, not by these optionals.
            REQUIRE(config.der_sae_setup_config.has_value() == true);
            REQUIRE(config.der_sae_limits.has_value() == true);
        }
    }

    GIVEN("Sign conformant limits") {
        auto limits = valid_limits;
        limits.max_discharge_power = {-5, 3};
        limits.nominal_discharge_power = {-3, 3};
        limits.reactive_power_limits.maximum_var_absorption_during_charging = {2, 3};
        limits.reactive_power_limits.maximum_var_injection_during_charging = {-2, 3};

        THEN("The service is kept") {
            REQUIRE(session_config_offers_ac_der_sae(limits, iso15118::d20::get_default_sae_der_control()) == true);
        }
    }
}

SCENARIO("Replacing the offered energy services revalidates the SAE DER setup") {
    namespace dt = iso15118::message_20::datatypes;

    const std::vector<dt::ServiceCategory> re_added = {dt::ServiceCategory::AC, dt::ServiceCategory::AC_DER_SAE};

    GIVEN("A session config whose non conformant AC_DER_SAE offer was stripped at construction") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points = single_point_curve();

        auto config = make_session_config(iso15118::d20::SaeDerTransferLimits{}, der_control);
        REQUIRE(offers_ac_der_sae(config) == false);

        WHEN("The offered services are replaced with a list that re-adds AC_DER_SAE") {
            config.set_supported_energy_transfer_services(re_added);

            THEN("AC_DER_SAE is stripped again and the other service is kept") {
                REQUIRE(offers_ac_der_sae(config) == false);
                REQUIRE(config.supported_energy_transfer_services ==
                        std::vector<dt::ServiceCategory>{dt::ServiceCategory::AC});
            }
        }
    }

    GIVEN("A session config without SAE DER limits") {
        auto config = iso15118::d20::SessionConfig(create_ac_der_sae_evse_setup(std::nullopt, std::nullopt));
        REQUIRE(offers_ac_der_sae(config) == false);

        WHEN("The offered services are replaced with a list that re-adds AC_DER_SAE") {
            config.set_supported_energy_transfer_services(re_added);

            THEN("AC_DER_SAE is stripped again") {
                REQUIRE(offers_ac_der_sae(config) == false);
            }
        }
    }

    GIVEN("A session config with a conformant AC_DER_SAE offer") {
        auto config =
            make_session_config(iso15118::d20::SaeDerTransferLimits{}, iso15118::d20::get_default_sae_der_control());
        REQUIRE(offers_ac_der_sae(config) == true);

        WHEN("The offered services are replaced with a list that contains AC_DER_SAE") {
            config.set_supported_energy_transfer_services(re_added);

            THEN("The replacement list is kept as it is") {
                REQUIRE(config.supported_energy_transfer_services == re_added);
            }
        }
    }
}

SCENARIO("The SAE DER validator names the violated rule") {

    const iso15118::d20::SaeDerTransferLimits valid_limits{};
    const iso15118::d20::DerSaeSetupConfig valid_setup{};

    GIVEN("A conformant setup") {
        THEN("No violation is reported") {
            REQUIRE(iso15118::d20::validate_sae_der_setup(valid_setup, valid_limits).has_value() == false);
        }
    }

    GIVEN("A per phase curve list with a single data point") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.reactive_power_support.watt_var.curve_data_points_L3 = single_point_curve();

        const auto setup = iso15118::d20::DerSaeSetupConfig(der_control, sae::RequiredDEROperatingMode::GridFollowing,
                                                            sae::GridConnectionMode::GridConnected);

        THEN("The violation names the curve and the phase") {
            const auto violation = iso15118::d20::validate_sae_der_setup(setup, valid_limits);
            REQUIRE(violation.has_value() == true);
            REQUIRE(violation.value().find("watt var curve L3") != std::string::npos);
        }
    }

    GIVEN("An enter service without any delay") {
        auto der_control = iso15118::d20::get_default_sae_der_control();
        der_control.enter_service.enter_service_randomized_delay = std::nullopt;

        const auto setup = iso15118::d20::DerSaeSetupConfig(der_control, sae::RequiredDEROperatingMode::GridFollowing,
                                                            sae::GridConnectionMode::GridConnected);

        THEN("The violation names the enter service rule") {
            const auto violation = iso15118::d20::validate_sae_der_setup(setup, valid_limits);
            REQUIRE(violation.has_value() == true);
            REQUIRE(violation.value().find("enter_service_randomized_delay") != std::string::npos);
        }
    }

    GIVEN("A sign violating limit") {
        auto limits = valid_limits;
        limits.reactive_power_limits.maximum_var_absorption_during_discharging_L2 = {-100, 0};

        THEN("The violation names the limit and the expected sign") {
            const auto violation = iso15118::d20::validate_sae_der_setup(valid_setup, limits);
            REQUIRE(violation.has_value() == true);
            REQUIRE(violation.value() == "maximum_var_absorption_during_discharging_L2 must be non-negative");
        }
    }
}
