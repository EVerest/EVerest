// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/d20/session.hpp>
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>
#include <iso15118/io/logging.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinRel;

namespace dt = iso15118::message_20::datatypes;
namespace dt_sae = dt::sae;
namespace sae = iso15118::sae;

using iso15118::LogLevel;

namespace {

// The logging callback is process global, so it has to be uninstalled before the buffer it writes into dies.
class LoggingCapture {
public:
    explicit LoggingCapture(std::vector<std::pair<LogLevel, std::string>>& sink) {
        iso15118::io::set_logging_callback(
            [&sink](LogLevel level, std::string message) { sink.emplace_back(level, std::move(message)); });
    }
    LoggingCapture(const LoggingCapture&) = delete;
    LoggingCapture& operator=(const LoggingCapture&) = delete;
    ~LoggingCapture() {
        iso15118::io::set_logging_callback([](LogLevel, std::string) {});
    }
};

size_t count_level(const std::vector<std::pair<LogLevel, std::string>>& lines, LogLevel level) {
    return static_cast<size_t>(
        std::count_if(lines.begin(), lines.end(),
                      [level](const std::pair<LogLevel, std::string>& entry) { return entry.first == level; }));
}

size_t count_lines(const std::vector<std::pair<LogLevel, std::string>>& lines, LogLevel level, const char* needle) {
    return static_cast<size_t>(
        std::count_if(lines.begin(), lines.end(), [level, needle](const std::pair<LogLevel, std::string>& entry) {
            return entry.first == level and entry.second.find(needle) != std::string::npos;
        }));
}

constexpr auto UNPAIRED_DELAY_LINE = "enter_service_delay is configured without enter_service_ramp_time";

// The rational number mantissa is built by integer truncation, so the recovered value carries a small
// relative error.
constexpr auto TOLERANCE = 1e-3f;
constexpr float NOMINAL_VOLTAGE_V = 230.0f;

constexpr std::uint32_t bit_of(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

constexpr std::uint32_t ALL_FUNCTIONS = 0xFFFFFFFFu;
constexpr std::uint32_t CHARGE_AND_DISCHARGE_ONLY =
    bit_of(sae::DerBitMapFunctions::ChargeFunction) | bit_of(sae::DerBitMapFunctions::DischargeFunction);

float value_of(const dt::RationalNumber& in) {
    return dt::from_RationalNumber(in);
}

iso15118::d20::DerSaeSetupConfig config_from(sae::DERControl der_control) {
    return iso15118::d20::DerSaeSetupConfig{std::move(der_control), sae::RequiredDEROperatingMode::GridFollowing,
                                            sae::GridConnectionMode::GridConnected};
}

} // namespace

SCENARIO("The charge loop DER control is built from the setup config") {

    GIVEN("The default inert SAE setup config") {
        const auto config = iso15118::d20::make_inert_default_sae_setup_config(NOMINAL_VOLTAGE_V);

        WHEN("Nothing changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, false, ALL_FUNCTIONS);

            THEN("Only the mandatory permit service flag is sent") {
                REQUIRE(out.voltage_trip.has_value() == false);
                REQUIRE(out.frequency_trip.has_value() == false);
                REQUIRE(out.reactive_power_support_cl_res.has_value() == false);
                REQUIRE(out.active_power_support_cl_res.has_value() == false);

                REQUIRE(out.enter_service_cl_res.permit_service == false);
                REQUIRE(out.enter_service_cl_res.enter_service_voltage_high.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_voltage_low.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_frequency_high.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_frequency_low.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_randomized_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_ramp_time.has_value() == false);
            }
        }

        WHEN("The DER control changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, ALL_FUNCTIONS);

            THEN("All five blocks are populated") {
                REQUIRE(out.voltage_trip.has_value() == true);
                REQUIRE(out.frequency_trip.has_value() == true);
                REQUIRE(out.reactive_power_support_cl_res.has_value() == true);
                REQUIRE(out.active_power_support_cl_res.has_value() == true);
            }

            THEN("Every optional leaf block of the two power support blocks is engaged") {
                const auto& reactive = out.reactive_power_support_cl_res.value();
                REQUIRE(reactive.constant_power_factor.has_value() == true);
                REQUIRE(reactive.volt_var.has_value() == true);
                REQUIRE(reactive.watt_var.has_value() == true);
                REQUIRE(reactive.constant_var.has_value() == true);

                const auto& active = out.active_power_support_cl_res.value();
                REQUIRE(active.frequency_droop.has_value() == true);
                REQUIRE(active.volt_watt.has_value() == true);
                REQUIRE(active.constant_watt.has_value() == true);
                REQUIRE(active.limit_max_discharge_power.has_value() == true);
            }

            THEN("The enter service thresholds are carried over") {
                REQUIRE(out.enter_service_cl_res.permit_service == false);
                REQUIRE(out.enter_service_cl_res.enter_service_voltage_high.has_value() == true);
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_voltage_high.value()),
                             WithinRel(241.5f, TOLERANCE));
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_voltage_low.value()),
                             WithinRel(210.91f, TOLERANCE));
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_frequency_high.value()),
                             WithinRel(50.1f, TOLERANCE));
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_frequency_low.value()),
                             WithinRel(49.5f, TOLERANCE));
            }
        }
    }

    GIVEN("A SAE setup config whose values differ from the defaults") {
        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
        der_control.voltage_trip.over_voltage_must_trip_curve.curve_data_points = {{113.0f, 3.0f}, {123.0f, 0.24f}};
        der_control.frequency_trip.under_frequency_must_trip_curve.curve_data_points = {{47.1f, 200.0f},
                                                                                        {46.5f, 0.32f}};
        der_control.enter_service.enter_service_voltage_high = 106.5f;
        der_control.reactive_power_support.volt_var.curve_data_points = {{97.0f, 12.0f}, {103.0f, -12.0f}};
        der_control.reactive_power_support.constant_var.var_setpoint = 250.0f;
        der_control.active_power_support.volt_watt.curve_data_points = {{101.0f, 90.0f}, {111.0f, 45.0f}};
        der_control.active_power_support.constant_watt.watt_setpoint = 33.0f;
        der_control.active_power_support.limit_max_discharge_power.percentage_value = 70;

        const auto config = config_from(der_control);

        WHEN("The DER control changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, ALL_FUNCTIONS);

            THEN("The configured trip curve points are sent instead of the defaults") {
                const auto& voltage_points = out.voltage_trip.value().over_voltage_must_trip_curve.curve_data_points;
                REQUIRE_THAT(value_of(voltage_points[0].x_value), WithinRel(113.0f, TOLERANCE));
                REQUIRE_THAT(value_of(voltage_points[0].y_value), WithinRel(3.0f, TOLERANCE));
                REQUIRE_THAT(value_of(voltage_points[1].x_value), WithinRel(123.0f, TOLERANCE));
                REQUIRE_THAT(value_of(voltage_points[1].y_value), WithinRel(0.24f, TOLERANCE));

                const auto& frequency_points =
                    out.frequency_trip.value().under_frequency_must_trip_curve.curve_data_points;
                REQUIRE_THAT(value_of(frequency_points[0].x_value), WithinRel(47.1f, TOLERANCE));
                REQUIRE_THAT(value_of(frequency_points[1].y_value), WithinRel(0.32f, TOLERANCE));
            }

            THEN("The configured power support values are sent instead of the defaults") {
                const auto& reactive = out.reactive_power_support_cl_res.value();
                const auto& volt_var_points = reactive.volt_var.value().curve_data_points;
                REQUIRE_THAT(value_of(volt_var_points[0].x_value), WithinRel(97.0f, TOLERANCE));
                REQUIRE_THAT(value_of(volt_var_points[0].y_value), WithinRel(12.0f, TOLERANCE));
                REQUIRE_THAT(value_of(volt_var_points[1].y_value), WithinRel(-12.0f, TOLERANCE));
                REQUIRE_THAT(value_of(reactive.constant_var.value().var_setpoint), WithinRel(250.0f, TOLERANCE));

                const auto& active = out.active_power_support_cl_res.value();
                const auto& volt_watt_points = active.volt_watt.value().curve_data_points;
                REQUIRE_THAT(value_of(volt_watt_points[1].x_value), WithinRel(111.0f, TOLERANCE));
                REQUIRE_THAT(value_of(volt_watt_points[1].y_value), WithinRel(45.0f, TOLERANCE));
                REQUIRE_THAT(value_of(active.constant_watt.value().watt_setpoint), WithinRel(33.0f, TOLERANCE));
                REQUIRE(active.limit_max_discharge_power.value().percentage_value == 70);
            }

            THEN("The configured enter service threshold is sent instead of the default") {
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_voltage_high.value()),
                             WithinRel(106.5f, TOLERANCE));
            }
        }
    }

    GIVEN("A SAE setup config with the enter service delay and ramp values set") {
        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
        der_control.enter_service.enter_service_delay = 5.0f;
        der_control.enter_service.enter_service_randomized_delay = 12.0f;
        der_control.enter_service.enter_service_ramp_time = 30.0f;

        const auto config = config_from(der_control);

        WHEN("The DER control changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, ALL_FUNCTIONS);

            THEN("Each delay and ramp value lands in its own field") {
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_delay.value()),
                             WithinRel(5.0f, TOLERANCE));
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_randomized_delay.value()),
                             WithinRel(12.0f, TOLERANCE));
                REQUIRE_THAT(value_of(out.enter_service_cl_res.enter_service_ramp_time.value()),
                             WithinRel(30.0f, TOLERANCE));
            }
        }

        WHEN("Nothing changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, false, ALL_FUNCTIONS);

            THEN("The delay and ramp values are not repeated") {
                REQUIRE(out.enter_service_cl_res.enter_service_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_randomized_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_ramp_time.has_value() == false);
            }
        }
    }

    GIVEN("A response that already carries a full DER control update") {
        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
        der_control.enter_service.permit_service = true;
        der_control.enter_service.enter_service_ramp_time = 30.0f;

        const auto config = config_from(der_control);

        dt_sae::DERControlCLRes out{};
        iso15118::d20::state::build_der_control_cl_res(out, config, true, ALL_FUNCTIONS);
        REQUIRE(out.voltage_trip.has_value() == true);
        REQUIRE(out.enter_service_cl_res.enter_service_ramp_time.has_value() == true);

        WHEN("The same response is rebuilt without a change") {
            iso15118::d20::state::build_der_control_cl_res(out, config, false, ALL_FUNCTIONS);

            THEN("The stale update blocks are cleared") {
                REQUIRE(out.voltage_trip.has_value() == false);
                REQUIRE(out.frequency_trip.has_value() == false);
                REQUIRE(out.reactive_power_support_cl_res.has_value() == false);
                REQUIRE(out.active_power_support_cl_res.has_value() == false);
            }

            THEN("The mandatory permit service flag stays and its optional members are cleared") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
                REQUIRE(out.enter_service_cl_res.enter_service_voltage_high.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_voltage_low.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_frequency_high.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_frequency_low.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_randomized_delay.has_value() == false);
                REQUIRE(out.enter_service_cl_res.enter_service_ramp_time.has_value() == false);
            }
        }
    }
}

SCENARIO("The charge loop DER control is gated by the EV supported modes") {

    GIVEN("A SAE setup config with grid code functions enabled") {
        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
        der_control.enter_service.permit_service = true;
        der_control.voltage_trip.over_voltage_must_trip_curve.enable = true;
        der_control.reactive_power_support.volt_var.enable = true;
        der_control.active_power_support.constant_watt.enable = true;

        const auto config = config_from(der_control);

        WHEN("The EV declared only the charge and discharge functions") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, CHARGE_AND_DISCHARGE_ONLY);

            THEN("Every enable of an undeclared function is cleared") {
                REQUIRE(out.voltage_trip.value().over_voltage_must_trip_curve.enable == false);
                REQUIRE(out.reactive_power_support_cl_res.value().volt_var.value().enable == false);
                REQUIRE(out.active_power_support_cl_res.value().constant_watt.value().enable == false);
            }

            // permit_service is an authorization, not an Enable: not gated. See ADR-0023.
            THEN("The permit service authorization passes through ungated") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
            }
        }

        WHEN("The EV declared only the charge and discharge functions and nothing changed") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, false, CHARGE_AND_DISCHARGE_ONLY);

            THEN("The always sent permit service flag is ungated on the minimal path too") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
            }
        }

        WHEN("The EV declared the matching functions") {
            const auto supported_modes = CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::EnterService) |
                                         bit_of(sae::DerBitMapFunctions::HighVoltageMustTripFunction) |
                                         bit_of(sae::DerBitMapFunctions::VoltVarFunction) |
                                         bit_of(sae::DerBitMapFunctions::ConstantActivePowerFunction);

            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, supported_modes);

            THEN("The enables survive") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
                REQUIRE(out.voltage_trip.value().over_voltage_must_trip_curve.enable == true);
                REQUIRE(out.reactive_power_support_cl_res.value().volt_var.value().enable == true);
                REQUIRE(out.active_power_support_cl_res.value().constant_watt.value().enable == true);
            }

            THEN("The enables of the functions the config leaves off stay off") {
                REQUIRE(out.reactive_power_support_cl_res.value().watt_var.value().enable == false);
                REQUIRE(out.active_power_support_cl_res.value().volt_watt.value().enable == false);
            }
        }
    }
}

SCENARIO("The unchanged charge loop DER control path does not log") {

    // Declared before the capture guard so the guard is destroyed while this is still alive.
    std::vector<std::pair<LogLevel, std::string>> log_lines;

    GIVEN("A config that would warn twice, against an EV that did not declare the enter service function") {
        auto der_control = iso15118::d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
        der_control.enter_service.permit_service = true;
        der_control.enter_service.enter_service_delay = 5.0f;

        const auto config = config_from(der_control);

        LoggingCapture capture{log_lines};

        WHEN("The charge loop rebuilds the response many times without a change") {
            dt_sae::DERControlCLRes out{};
            constexpr size_t LOOPS = 5;
            for (size_t loop = 0; loop < LOOPS; ++loop) {
                iso15118::d20::state::build_der_control_cl_res(out, config, false, CHARGE_AND_DISCHARGE_ONLY);
            }

            THEN("Not a single line is logged, so the once per second loop cannot flood") {
                REQUIRE(count_lines(log_lines, LogLevel::Warning, UNPAIRED_DELAY_LINE) == 0);
                REQUIRE(count_level(log_lines, LogLevel::Warning) == 0);
            }

            THEN("The mandatory permit service flag carries the configured authorization") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
            }
        }

        WHEN("The DER control changed since the charge parameter discovery") {
            dt_sae::DERControlCLRes out{};
            iso15118::d20::state::build_der_control_cl_res(out, config, true, CHARGE_AND_DISCHARGE_ONLY);

            THEN("The warning is emitted once on the path that actually sends the control set") {
                REQUIRE(count_lines(log_lines, LogLevel::Warning, UNPAIRED_DELAY_LINE) == 1);
            }

            THEN("An EV that never declared the function is still authorized") {
                REQUIRE(out.enter_service_cl_res.permit_service == true);
            }
        }
    }
}
