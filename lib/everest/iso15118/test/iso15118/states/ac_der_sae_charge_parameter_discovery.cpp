// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/state/ac_der_sae_charge_parameter_discovery.hpp>

#include <iso15118/d20/config.hpp>
#include <iso15118/io/logging.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;
namespace sae = iso15118::sae;

using CpdRequest = message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest;
using CpdResponse = message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse;

namespace {

constexpr std::uint32_t bit_of(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

constexpr std::uint32_t CHARGE_AND_DISCHARGE_ONLY =
    bit_of(sae::DerBitMapFunctions::ChargeFunction) | bit_of(sae::DerBitMapFunctions::DischargeFunction);

constexpr auto MISSING_FUNCTIONS_LINE = "EV did not set both ChargeFunction and DischargeFunction in SupportedModes";

// The logging callback is process global, so it has to be uninstalled before the buffer it writes into dies.
class LoggingCapture {
public:
    explicit LoggingCapture(std::vector<std::pair<LogLevel, std::string>>& sink) {
        io::set_logging_callback(
            [&sink](LogLevel level, std::string message) { sink.emplace_back(level, std::move(message)); });
    }
    LoggingCapture(const LoggingCapture&) = delete;
    LoggingCapture& operator=(const LoggingCapture&) = delete;
    ~LoggingCapture() {
        io::set_logging_callback([](LogLevel, std::string) {});
    }
};

size_t count_lines(const std::vector<std::pair<LogLevel, std::string>>& lines, LogLevel level, const char* needle) {
    return static_cast<size_t>(
        std::count_if(lines.begin(), lines.end(), [level, needle](const std::pair<LogLevel, std::string>& entry) {
            return entry.first == level and entry.second.find(needle) != std::string::npos;
        }));
}

// Every quantity below carries its own exact mantissa so a dropped or cross-wired assignment fails.
// The per phase nominal and maximum pairs are tuned so each phase nominal exceeds the other phase maximum:
// that makes an L2 against L3 swap of the paired validation calls reject the good case.
d20::AcTransferLimits make_ac_limits() {
    d20::AcTransferLimits limits{};
    limits.charge_power = {{22, 3}, {10, 0}};
    limits.charge_power_L2 = d20::Limit<dt::RationalNumber>{{8, 3}, {11, 0}};
    limits.charge_power_L3 = d20::Limit<dt::RationalNumber>{{12, 3}, {12, 0}};
    limits.nominal_frequency = {50, 0};
    limits.max_power_asymmetry = dt::RationalNumber{13, 2};
    limits.power_ramp_limitation = dt::RationalNumber{14, 1};
    return limits;
}

d20::SaeDerTransferLimits make_sae_limits() {
    d20::SaeDerTransferLimits limits{};
    limits.nominal_charge_power = dt::RationalNumber{21, 3};
    limits.nominal_charge_power_L2 = dt::RationalNumber{75, 2};
    limits.nominal_charge_power_L3 = dt::RationalNumber{115, 2};
    limits.nominal_discharge_power = dt::RationalNumber{-19, 3};
    limits.nominal_discharge_power_L2 = dt::RationalNumber{-55, 2};
    limits.nominal_discharge_power_L3 = dt::RationalNumber{-95, 2};
    limits.max_discharge_power = {-20, 3};
    limits.max_discharge_power_L2 = dt::RationalNumber{-6, 3};
    limits.max_discharge_power_L3 = dt::RationalNumber{-10, 3};

    auto& reactive = limits.reactive_power_limits;
    reactive.maximum_var_absorption_during_charging = {31, 2};
    reactive.maximum_var_absorption_during_charging_L2 = dt::RationalNumber{32, 2};
    reactive.maximum_var_absorption_during_charging_L3 = dt::RationalNumber{33, 2};
    reactive.maximum_var_injection_during_charging = {34, 2};
    reactive.maximum_var_injection_during_charging_L2 = dt::RationalNumber{35, 2};
    reactive.maximum_var_injection_during_charging_L3 = dt::RationalNumber{36, 2};
    reactive.maximum_var_absorption_during_discharging = {37, 2};
    reactive.maximum_var_absorption_during_discharging_L2 = dt::RationalNumber{38, 2};
    reactive.maximum_var_absorption_during_discharging_L3 = dt::RationalNumber{39, 2};
    reactive.maximum_var_injection_during_discharging = {41, 2};
    reactive.maximum_var_injection_during_discharging_L2 = dt::RationalNumber{42, 2};
    reactive.maximum_var_injection_during_discharging_L3 = dt::RationalNumber{43, 2};

    auto& grid = limits.grid_limits;
    grid.nominal_frequency = {51, 0};
    grid.nominal_voltage = {231, 0};
    grid.nominal_voltage_offset = {2, 0};
    grid.min_frequency = dt::RationalNumber{47, 0};
    grid.max_frequency = dt::RationalNumber{52, 0};
    grid.maximum_voltage = {253, 0};
    grid.minimum_voltage = {207, 0};
    return limits;
}

d20::AcPresentPower make_present_power() {
    d20::AcPresentPower powers{};
    powers.present_active_power = dt::RationalNumber{15, 3};
    powers.present_active_power_L2 = dt::RationalNumber{16, 3};
    powers.present_active_power_L3 = dt::RationalNumber{17, 3};
    return powers;
}

d20::DerSaeSetupConfig make_config(sae::RequiredDEROperatingMode operating_mode,
                                   sae::GridConnectionMode connection_mode) {
    return d20::DerSaeSetupConfig{d20::get_default_sae_der_control(), operating_mode, connection_mode};
}

d20::Session make_session() {
    const d20::SelectedServiceParameters service_parameters(
        dt::ServiceCategory::AC_DER_SAE, dt::AcConnector::ThreePhase, dt::ControlMode::Scheduled,
        dt::MobilityNeedsMode::ProvidedByEvcc, dt::Pricing::NoPricing, 230);
    return d20::Session{service_parameters};
}

CpdRequest make_request(const d20::Session& session, dt::Processing processing, std::uint32_t supported_modes) {
    CpdRequest req{};
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;

    auto& mode = req.transfer_mode;
    mode.max_charge_power = {22, 3};
    mode.min_charge_power = {10, 0};
    mode.maximum_discharge_power = {-20, 3};
    mode.processing = processing;
    mode.supported_modes = supported_modes;
    mode.enabled_modes = 0;
    return req;
}

// Defaults for a good case. Each scenario overrides only what it exercises.
struct Inputs {
    d20::AcTransferLimits ac_limits{make_ac_limits()};
    d20::AcPresentPower present_powers{make_present_power()};
    std::optional<d20::SaeDerTransferLimits> sae_limits{make_sae_limits()};
    std::optional<d20::DerSaeSetupConfig> config{
        make_config(sae::RequiredDEROperatingMode::GridFollowing, sae::GridConnectionMode::GridConnected)};
};

CpdResponse call(const CpdRequest& req, d20::Session& session, const Inputs& in) {
    return d20::state::handle_request(req, session, in.ac_limits, in.present_powers, in.sae_limits, in.config);
}

float value_of(const dt::RationalNumber& in) {
    return dt::from_RationalNumber(in);
}

} // namespace

SCENARIO("SAE AC DER charge parameter discovery rejections") {

    auto session = make_session();
    const auto req = make_request(session, dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY);

    GIVEN("Bad case - unknown session") {
        auto other_session = make_session();

        THEN("ResponseCode: FAILED_UnknownSession") {
            REQUIRE(call(req, other_session, Inputs{}).response_code == dt::ResponseCode::FAILED_UnknownSession);
        }
    }

    GIVEN("Bad case - no SAE limits are configured") {
        Inputs in{};
        in.sae_limits.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter and no EV declaration is recorded") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
            REQUIRE(session.get_ev_supported_sae_functions().has_value() == false);
        }
    }

    GIVEN("Bad case - no SAE DER control values are configured") {
        Inputs in{};
        in.config.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter and no EV declaration is recorded") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
            REQUIRE(session.get_ev_supported_sae_functions().has_value() == false);
        }
    }

    GIVEN("Bad case - a nominal charge power above the maximum charge power") {
        Inputs in{};
        in.sae_limits.value().nominal_charge_power = dt::RationalNumber{23, 3};

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("A nominal charge power exactly at the maximum charge power") {
        Inputs in{};
        in.sae_limits.value().nominal_charge_power = dt::RationalNumber{22, 3};

        THEN("ResponseCode: OK, the check rejects only a strict excess") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::OK);
        }
    }

    GIVEN("Bad case - a nominal L2 charge power without an L2 maximum") {
        Inputs in{};
        in.ac_limits.charge_power_L2.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Bad case - a nominal L3 charge power without an L3 maximum") {
        Inputs in{};
        in.ac_limits.charge_power_L3.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Bad case - a nominal L2 discharge power without an L2 maximum") {
        Inputs in{};
        in.sae_limits.value().max_discharge_power_L2.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Bad case - a nominal L3 discharge power without an L3 maximum") {
        Inputs in{};
        in.sae_limits.value().max_discharge_power_L3.reset();

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Bad case - a nominal discharge power whose magnitude exceeds the maximum") {
        // Both are negative, so only a magnitude comparison catches this.
        Inputs in{};
        in.sae_limits.value().nominal_discharge_power = dt::RationalNumber{-21, 3};

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(value_of(in.sae_limits.value().nominal_discharge_power.value()) <
                    value_of(in.sae_limits.value().max_discharge_power));
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("Bad case - a nominal phase discharge power whose magnitude exceeds the phase maximum") {
        Inputs in{};
        in.sae_limits.value().nominal_discharge_power_L3 = dt::RationalNumber{-105, 2};

        THEN("ResponseCode: FAILED_WrongChargeParameter") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED_WrongChargeParameter);
        }
    }

    GIVEN("A nominal discharge power exactly at the maximum") {
        Inputs in{};
        in.sae_limits.value().nominal_discharge_power = dt::RationalNumber{-20, 3};

        THEN("ResponseCode: OK") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::OK);
        }
    }
}

SCENARIO("SAE AC DER charge parameter discovery answers the configured limits") {

    auto session = make_session();
    const auto req = make_request(session, dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY);
    // One instance, because each Inputs{} stamps der_control_update_time from the clock.
    const Inputs good_inputs{};
    const auto res = call(req, session, good_inputs);

    REQUIRE(res.response_code == dt::ResponseCode::OK);
    const auto& mode = res.transfer_mode;

    GIVEN("The AC charge power limits") {
        THEN("All three phases carry their own maximum and minimum") {
            REQUIRE(value_of(mode.max_charge_power) == 22000.0f);
            REQUIRE(value_of(mode.min_charge_power) == 10.0f);
            REQUIRE(value_of(mode.max_charge_power_L2.value()) == 8000.0f);
            REQUIRE(value_of(mode.min_charge_power_L2.value()) == 11.0f);
            REQUIRE(value_of(mode.max_charge_power_L3.value()) == 12000.0f);
            REQUIRE(value_of(mode.min_charge_power_L3.value()) == 12.0f);
        }
    }

    GIVEN("The remaining AC quantities") {
        THEN("The nominal frequency, the asymmetry and the ramp limitation are answered") {
            REQUIRE(value_of(mode.nominal_frequency) == 50.0f);
            REQUIRE(value_of(mode.max_power_asymmetry.value()) == 1300.0f);
            REQUIRE(value_of(mode.power_ramp_limitation.value()) == 140.0f);
        }
    }

    GIVEN("The present active power") {
        THEN("All three phases are reported back") {
            REQUIRE(value_of(mode.present_active_power.value()) == 15000.0f);
            REQUIRE(value_of(mode.present_active_power_L2.value()) == 16000.0f);
            REQUIRE(value_of(mode.present_active_power_L3.value()) == 17000.0f);
        }
    }

    GIVEN("The SAE nominal and maximum powers") {
        THEN("All three phases of every quantity are answered") {
            REQUIRE(value_of(mode.nominal_charge_power.value()) == 21000.0f);
            REQUIRE(value_of(mode.nominal_charge_power_L2.value()) == 7500.0f);
            REQUIRE(value_of(mode.nominal_charge_power_L3.value()) == 11500.0f);
            REQUIRE(value_of(mode.nominal_discharge_power.value()) == -19000.0f);
            REQUIRE(value_of(mode.nominal_discharge_power_L2.value()) == -5500.0f);
            REQUIRE(value_of(mode.nominal_discharge_power_L3.value()) == -9500.0f);
            REQUIRE(value_of(mode.maximum_discharge_power) == -20000.0f);
            REQUIRE(value_of(mode.maximum_discharge_power_L2.value()) == -6000.0f);
            REQUIRE(value_of(mode.maximum_discharge_power_L3.value()) == -10000.0f);
        }
    }

    GIVEN("The reactive power limits") {
        THEN("All twelve members are answered") {
            const auto& reactive = mode.reactive_power_limits;
            REQUIRE(value_of(reactive.maximum_var_absorption_during_charging) == 3100.0f);
            REQUIRE(value_of(reactive.maximum_var_absorption_during_charging_L2.value()) == 3200.0f);
            REQUIRE(value_of(reactive.maximum_var_absorption_during_charging_L3.value()) == 3300.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_charging) == 3400.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_charging_L2.value()) == 3500.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_charging_L3.value()) == 3600.0f);
            REQUIRE(value_of(reactive.maximum_var_absorption_during_discharging) == 3700.0f);
            REQUIRE(value_of(reactive.maximum_var_absorption_during_discharging_L2.value()) == 3800.0f);
            REQUIRE(value_of(reactive.maximum_var_absorption_during_discharging_L3.value()) == 3900.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_discharging) == 4100.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_discharging_L2.value()) == 4200.0f);
            REQUIRE(value_of(reactive.maximum_var_injection_during_discharging_L3.value()) == 4300.0f);
        }
    }

    GIVEN("The grid limits") {
        THEN("All seven members are answered, the nominal frequency separately from the AC one") {
            const auto& grid = mode.grid_limits;
            REQUIRE(value_of(grid.nominal_frequency) == 51.0f);
            REQUIRE(value_of(grid.nominal_voltage) == 231.0f);
            REQUIRE(value_of(grid.nominal_voltage_offset) == 2.0f);
            REQUIRE(value_of(grid.min_frequency.value()) == 47.0f);
            REQUIRE(value_of(grid.max_frequency.value()) == 52.0f);
            REQUIRE(value_of(grid.maximum_voltage) == 253.0f);
            REQUIRE(value_of(grid.minimum_voltage) == 207.0f);
        }
    }

    GIVEN("The configured update time") {
        THEN("It is answered so the EV can detect a later change") {
            REQUIRE(mode.update_time == good_inputs.config.value().der_control_update_time);
        }
    }
}

SCENARIO("SAE AC DER charge parameter discovery sets the operating and connection mode") {

    auto session = make_session();
    const auto req = make_request(session, dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY);

    GIVEN("A grid following and grid connected configuration") {
        Inputs in{};
        in.config = make_config(sae::RequiredDEROperatingMode::GridFollowing, sae::GridConnectionMode::GridConnected);

        const auto res = call(req, session, in);

        THEN("Both are answered as configured") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.transfer_mode.required_der_operating_mode == dt_sae::RequiredDEROperatingMode::GridFollowing);
            REQUIRE(res.transfer_mode.grid_connection_mode == dt_sae::GridConnectionMode::GridConnected);
        }
    }

    GIVEN("A grid forming and grid islanded configuration") {
        Inputs in{};
        in.config = make_config(sae::RequiredDEROperatingMode::GridForming, sae::GridConnectionMode::GridIslanded);

        const auto res = call(req, session, in);

        THEN("Both are answered as configured") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.transfer_mode.required_der_operating_mode == dt_sae::RequiredDEROperatingMode::GridForming);
            REQUIRE(res.transfer_mode.grid_connection_mode == dt_sae::GridConnectionMode::GridIslanded);
        }
    }
}

SCENARIO("SAE AC DER charge parameter discovery processing mapping") {

    auto session = make_session();

    GIVEN("The EV is still processing") {
        const auto req = make_request(session, dt::Processing::Ongoing, CHARGE_AND_DISCHARGE_ONLY);

        THEN("EVSEProcessing: Ongoing") {
            REQUIRE(call(req, session, Inputs{}).transfer_mode.processing == dt::Processing::Ongoing);
        }
    }

    GIVEN("The EV finished processing") {
        const auto req = make_request(session, dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY);

        THEN("EVSEProcessing: Finished") {
            REQUIRE(call(req, session, Inputs{}).transfer_mode.processing == dt::Processing::Finished);
        }
    }

    GIVEN("The EV is waiting for a customer interaction") {
        const auto req =
            make_request(session, dt::Processing::Ongoing_WaitingForCustomerInteraction, CHARGE_AND_DISCHARGE_ONLY);

        THEN("EVSEProcessing: Finished, only Ongoing keeps the EVSE waiting") {
            REQUIRE(call(req, session, Inputs{}).transfer_mode.processing == dt::Processing::Finished);
        }
    }
}

SCENARIO("SAE AC DER charge parameter discovery tolerates missing service functions") {

    std::vector<std::pair<LogLevel, std::string>> log_lines;
    auto session = make_session();
    LoggingCapture capture{log_lines};

    GIVEN("The EV declared both the charge and the discharge function") {
        const auto req = make_request(session, dt::Processing::Finished, CHARGE_AND_DISCHARGE_ONLY);

        const auto res = call(req, session, Inputs{});

        THEN("ResponseCode: OK and nothing is warned about") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, MISSING_FUNCTIONS_LINE) == 0);
        }
    }

    GIVEN("The EV declared the charge function only") {
        const auto req =
            make_request(session, dt::Processing::Finished, bit_of(sae::DerBitMapFunctions::ChargeFunction));

        const auto res = call(req, session, Inputs{});

        THEN("The session continues and the omission is warned about") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, MISSING_FUNCTIONS_LINE) == 1);
            REQUIRE(session.get_ev_supported_sae_functions().value_or(0) ==
                    bit_of(sae::DerBitMapFunctions::ChargeFunction));
        }
    }

    GIVEN("The EV declared neither the charge nor the discharge function") {
        const auto req = make_request(session, dt::Processing::Finished, 0);

        const auto res = call(req, session, Inputs{});

        THEN("The session continues and the omission is warned about") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(count_lines(log_lines, LogLevel::Warning, MISSING_FUNCTIONS_LINE) == 1);
            REQUIRE(session.get_ev_supported_sae_functions().value_or(0xFFFFFFFFu) == 0);
        }
    }
}
