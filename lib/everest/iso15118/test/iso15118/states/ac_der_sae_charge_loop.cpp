// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_test_macros.hpp>

#include <iso15118/detail/d20/state/ac_der_sae_charge_loop.hpp>

#include <iso15118/d20/config.hpp>

#include <cstdint>

using namespace iso15118;

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;
namespace sae = iso15118::sae;

using Scheduled_DER_Req = dt_sae::DER_Scheduled_AC_CLReqControlMode;
using Dynamic_DER_Req = dt_sae::DER_Dynamic_AC_CLReqControlMode;

using Scheduled_DER_Res = dt_sae::DER_Scheduled_AC_CLResControlMode;
using Dynamic_DER_Res = dt_sae::DER_Dynamic_AC_CLResControlMode;

using LogState = d20::state::SaeChargeLoopLogState;

namespace {

constexpr float NOMINAL_VOLTAGE_V = 230.0f;

constexpr std::uint32_t bit_of(sae::DerBitMapFunctions function) {
    return 1U << static_cast<std::uint32_t>(function);
}

constexpr std::uint32_t CHARGE_AND_DISCHARGE_ONLY =
    bit_of(sae::DerBitMapFunctions::ChargeFunction) | bit_of(sae::DerBitMapFunctions::DischargeFunction);
constexpr std::uint32_t WITH_TARGET_POWER_FUNCTIONS = CHARGE_AND_DISCHARGE_ONLY |
                                                      bit_of(sae::DerBitMapFunctions::EVSETargetActivePowerFunction) |
                                                      bit_of(sae::DerBitMapFunctions::EVSETargetReactivePowerFunction);
constexpr std::uint32_t WITH_GRID_CODE_FUNCTIONS =
    CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::EnterService) |
    bit_of(sae::DerBitMapFunctions::HighVoltageMustTripFunction) | bit_of(sae::DerBitMapFunctions::VoltVarFunction) |
    bit_of(sae::DerBitMapFunctions::ConstantActivePowerFunction);

// Every input below carries its own exact mantissa so a dropped or cross-wired phase assignment fails.
d20::AcTransferLimits make_ac_limits() {
    d20::AcTransferLimits limits;
    limits.charge_power = {{22, 3}, {10, 0}};
    limits.charge_power_L2 = d20::Limit<dt::RationalNumber>{{8, 3}, {10, 0}};
    limits.charge_power_L3 = d20::Limit<dt::RationalNumber>{{9, 3}, {10, 0}};
    limits.nominal_frequency = {50, 0};
    return limits;
}

d20::SaeDerTransferLimits make_sae_limits() {
    d20::SaeDerTransferLimits limits{};
    limits.max_discharge_power = {-11, 3};
    limits.max_discharge_power_L2 = dt::RationalNumber{-4, 3};
    limits.max_discharge_power_L3 = dt::RationalNumber{-5, 3};
    return limits;
}

d20::AcTargetPower make_target_power() {
    d20::AcTargetPower targets{};
    targets.target_active_power = dt::RationalNumber{7, 3};
    targets.target_active_power_L2 = dt::RationalNumber{3, 3};
    targets.target_active_power_L3 = dt::RationalNumber{4, 3};
    targets.target_reactive_power = dt::RationalNumber{2, 3};
    targets.target_reactive_power_L2 = dt::RationalNumber{1, 3};
    targets.target_reactive_power_L3 = dt::RationalNumber{6, 3};
    return targets;
}

d20::AcPresentPower make_present_power() {
    d20::AcPresentPower powers{};
    powers.present_active_power = dt::RationalNumber{15, 3};
    powers.present_active_power_L2 = dt::RationalNumber{16, 3};
    powers.present_active_power_L3 = dt::RationalNumber{17, 3};
    return powers;
}

d20::DerSaeSetupConfig config_from(sae::DERControl der_control) {
    return d20::DerSaeSetupConfig{std::move(der_control), sae::RequiredDEROperatingMode::GridFollowing,
                                  sae::GridConnectionMode::GridConnected};
}

d20::DerSaeSetupConfig make_grid_code_config() {
    auto der_control = d20::get_default_sae_der_control(NOMINAL_VOLTAGE_V);
    der_control.enter_service.permit_service = true;
    der_control.voltage_trip.over_voltage_must_trip_curve.enable = true;
    der_control.reactive_power_support.volt_var.enable = true;
    der_control.active_power_support.constant_watt.enable = true;
    return config_from(der_control);
}

d20::Session make_session(dt::ControlMode control_mode, dt::MobilityNeedsMode mobility_needs_mode,
                          std::optional<std::uint32_t> ev_supported_modes) {
    const d20::SelectedServiceParameters service_parameters(dt::ServiceCategory::AC_DER_SAE,
                                                            dt::AcConnector::ThreePhase, control_mode,
                                                            mobility_needs_mode, dt::Pricing::NoPricing, 230);
    d20::Session session{service_parameters};
    if (ev_supported_modes.has_value()) {
        session.set_ev_supported_sae_functions(ev_supported_modes.value());
    }
    return session;
}

message_20::DER_SAE_AC_ChargeLoopRequest make_scheduled_request(const d20::Session& session) {
    message_20::DER_SAE_AC_ChargeLoopRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.meter_info_requested = false;

    auto& req_mode = req.control_mode.emplace<Scheduled_DER_Req>();
    req_mode.present_active_power = {11, 3};
    req_mode.present_voltage = {230, 0};
    req_mode.present_frequency = {50, 0};
    return req;
}

message_20::DER_SAE_AC_ChargeLoopRequest make_dynamic_request(const d20::Session& session) {
    message_20::DER_SAE_AC_ChargeLoopRequest req;
    req.header.session_id = session.get_id();
    req.header.timestamp = 1691411798;
    req.meter_info_requested = false;

    auto& req_mode = req.control_mode.emplace<Dynamic_DER_Req>();
    req_mode.present_active_power = {11, 3};
    req_mode.max_charge_power = {11, 3};
    req_mode.min_charge_power = {4, 0};
    req_mode.present_reactive_power = {10, 0};
    req_mode.maximum_discharge_power = {-11, 3};
    req_mode.minimum_discharge_power = {-4, 0};
    req_mode.present_voltage = {230, 0};
    req_mode.present_frequency = {50, 0};
    return req;
}

// Defaults for a good case. Each scenario overrides only what it exercises.
struct Inputs {
    bool stop{false};
    bool pause{false};
    d20::AcTargetPower targets{make_target_power()};
    d20::AcPresentPower present_powers{make_present_power()};
    d20::UpdateDynamicModeParameters dynamic_parameters{};
    d20::AcTransferLimits ac_limits{make_ac_limits()};
    std::optional<d20::SaeDerTransferLimits> sae_limits{make_sae_limits()};
    std::optional<d20::DerSaeSetupConfig> der_config{d20::make_inert_default_sae_setup_config(NOMINAL_VOLTAGE_V)};
    bool changed_since_cpd{false};
};

message_20::DER_SAE_AC_ChargeLoopResponse call(const message_20::DER_SAE_AC_ChargeLoopRequest& req,
                                               const d20::Session& session, const Inputs& in, LogState& log_state) {
    return d20::state::handle_request(req, session, in.stop, in.pause, in.targets, in.present_powers,
                                      in.dynamic_parameters, in.ac_limits, in.sae_limits, in.der_config,
                                      in.changed_since_cpd, log_state);
}

message_20::DER_SAE_AC_ChargeLoopResponse call(const message_20::DER_SAE_AC_ChargeLoopRequest& req,
                                               const d20::Session& session, const Inputs& in) {
    LogState log_state{};
    return call(req, session, in, log_state);
}

float value_of(const dt::RationalNumber& in) {
    return dt::from_RationalNumber(in);
}

} // namespace

SCENARIO("SAE AC DER charge loop rejections") {

    GIVEN("Bad case - unknown session") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_scheduled_request(session);

        const auto other_session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto res = call(req, other_session, Inputs{});

        THEN("ResponseCode: FAILED_UnknownSession and no optional field is filled") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED_UnknownSession);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
            REQUIRE(res.target_frequency.has_value() == false);
            REQUIRE(std::holds_alternative<Scheduled_DER_Res>(res.control_mode));
        }
    }

    GIVEN("Bad case - a scheduled request while dynamic mode was selected") {
        const auto session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_scheduled_request(session);

        const auto res = call(req, session, Inputs{});

        THEN("ResponseCode: FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - a dynamic request while scheduled mode was selected") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_dynamic_request(session);

        const auto res = call(req, session, Inputs{});

        THEN("ResponseCode: FAILED") {
            REQUIRE(res.response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - no SAE limits are configured") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_scheduled_request(session);

        Inputs in{};
        in.sae_limits.reset();

        THEN("ResponseCode: FAILED") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - no SAE DER control values are configured") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_scheduled_request(session);

        Inputs in{};
        in.der_config.reset();

        THEN("ResponseCode: FAILED") {
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - the EV never declared its supported modes") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, std::nullopt);
        const auto req = make_scheduled_request(session);

        THEN("ResponseCode: FAILED instead of a fabricated DER control set") {
            REQUIRE(call(req, session, Inputs{}).response_code == dt::ResponseCode::FAILED);
        }
    }

    GIVEN("Bad case - dynamic mode without the mandatory target active power") {
        const auto session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_dynamic_request(session);

        THEN("ResponseCode: FAILED instead of a fabricated 0 W target") {
            Inputs in{};
            in.targets = d20::AcTargetPower{};

            // 0 W is an instruction to stop importing and exporting, not a neutral placeholder.
            REQUIRE(call(req, session, in).response_code == dt::ResponseCode::FAILED);
        }

        THEN("A present target active power is still sent") {
            const auto res = call(req, session, Inputs{});
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(value_of(std::get<Dynamic_DER_Res>(res.control_mode).target_active_power) == 7000.0f);
        }
    }
}

SCENARIO("SAE AC DER charge loop scheduled mode") {

    const auto session =
        make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
    const auto req = make_scheduled_request(session);

    GIVEN("Nothing changed since the charge parameter discovery") {
        const auto res = call(req, session, Inputs{});

        THEN("ResponseCode: OK and the scheduled response control mode is chosen") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(std::holds_alternative<Scheduled_DER_Res>(res.control_mode));
        }

        THEN("Only the mandatory enter service block of the DER control is sent") {
            const auto& der_control = std::get<Scheduled_DER_Res>(res.control_mode).der_control_cl_res;
            REQUIRE(der_control.voltage_trip.has_value() == false);
            REQUIRE(der_control.frequency_trip.has_value() == false);
            REQUIRE(der_control.reactive_power_support_cl_res.has_value() == false);
            REQUIRE(der_control.active_power_support_cl_res.has_value() == false);
        }

        THEN("All three charge power maxima are sent") {
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.evse_maximum_charge_power.value()) == 22000.0f);
            REQUIRE(value_of(res_mode.evse_maximum_charge_power_L2.value()) == 8000.0f);
            REQUIRE(value_of(res_mode.evse_maximum_charge_power_L3.value()) == 9000.0f);
        }

        THEN("All three discharge power maxima are sent") {
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.evse_maximum_discharge_power.value()) == -11000.0f);
            REQUIRE(value_of(res_mode.evse_maximum_discharge_power_L2.value()) == -4000.0f);
            REQUIRE(value_of(res_mode.evse_maximum_discharge_power_L3.value()) == -5000.0f);
        }

        THEN("All three present active powers are reported back") {
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.present_active_power.value()) == 15000.0f);
            REQUIRE(value_of(res_mode.present_active_power_L2.value()) == 16000.0f);
            REQUIRE(value_of(res_mode.present_active_power_L3.value()) == 17000.0f);
        }

        THEN("The target power triples the EV did not declare are not sent") {
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(res_mode.target_active_power.has_value() == false);
            REQUIRE(res_mode.target_active_power_L2.has_value() == false);
            REQUIRE(res_mode.target_active_power_L3.has_value() == false);
            REQUIRE(res_mode.target_reactive_power.has_value() == false);
            REQUIRE(res_mode.target_reactive_power_L2.has_value() == false);
            REQUIRE(res_mode.target_reactive_power_L3.has_value() == false);
        }

        THEN("The optional operating and connection modes are not repeated") {
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(res_mode.required_der_operating_mode.has_value() == false);
            REQUIRE(res_mode.grid_connection_mode.has_value() == false);
        }

        THEN("The conditional fields nothing set stay absent") {
            REQUIRE(res.target_frequency.has_value() == false);
            REQUIRE(res.status.has_value() == false);
            REQUIRE(res.meter_info.has_value() == false);
            REQUIRE(res.receipt.has_value() == false);
        }
    }

    GIVEN("The DER control changed since the charge parameter discovery") {
        Inputs in{};
        in.changed_since_cpd = true;

        const auto res = call(req, session, in);

        THEN("All DER control update blocks are sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& der_control = std::get<Scheduled_DER_Res>(res.control_mode).der_control_cl_res;
            REQUIRE(der_control.voltage_trip.has_value() == true);
            REQUIRE(der_control.frequency_trip.has_value() == true);
            REQUIRE(der_control.reactive_power_support_cl_res.has_value() == true);
            REQUIRE(der_control.active_power_support_cl_res.has_value() == true);
        }
    }

    GIVEN("The EV declared the target power functions") {
        const auto declaring_session = make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc,
                                                    WITH_TARGET_POWER_FUNCTIONS);
        const auto declaring_req = make_scheduled_request(declaring_session);

        const auto res = call(declaring_req, declaring_session, Inputs{});

        THEN("Both target power triples are sent in full") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& res_mode = std::get<Scheduled_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.target_active_power.value()) == 7000.0f);
            REQUIRE(value_of(res_mode.target_active_power_L2.value()) == 3000.0f);
            REQUIRE(value_of(res_mode.target_active_power_L3.value()) == 4000.0f);
            REQUIRE(value_of(res_mode.target_reactive_power.value()) == 2000.0f);
            REQUIRE(value_of(res_mode.target_reactive_power_L2.value()) == 1000.0f);
            REQUIRE(value_of(res_mode.target_reactive_power_L3.value()) == 6000.0f);
        }
    }
}

SCENARIO("SAE AC DER charge loop DER control enables follow the EV supported modes") {

    Inputs in{};
    in.der_config = make_grid_code_config();
    in.changed_since_cpd = true;

    GIVEN("The EV declared the matching grid code functions") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_GRID_CODE_FUNCTIONS);
        const auto req = make_scheduled_request(session);

        const auto res = call(req, session, in);

        THEN("The configured enables survive") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& der_control = std::get<Scheduled_DER_Res>(res.control_mode).der_control_cl_res;
            REQUIRE(der_control.enter_service_cl_res.permit_service == true);
            REQUIRE(der_control.voltage_trip.value().over_voltage_must_trip_curve.enable == true);
            REQUIRE(der_control.reactive_power_support_cl_res.value().volt_var.value().enable == true);
            REQUIRE(der_control.active_power_support_cl_res.value().constant_watt.value().enable == true);
        }
    }

    GIVEN("The EV declared only the charge and discharge functions") {
        const auto session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        const auto req = make_scheduled_request(session);

        const auto res = call(req, session, in);

        THEN("Every enable of an undeclared function is cleared") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& der_control = std::get<Scheduled_DER_Res>(res.control_mode).der_control_cl_res;
            REQUIRE(der_control.voltage_trip.value().over_voltage_must_trip_curve.enable == false);
            REQUIRE(der_control.reactive_power_support_cl_res.value().volt_var.value().enable == false);
            REQUIRE(der_control.active_power_support_cl_res.value().constant_watt.value().enable == false);
        }

        // permit_service is an authorization, not an Enable: not gated. See ADR-0023.
        THEN("The permit service authorization reaches the EV ungated") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& der_control = std::get<Scheduled_DER_Res>(res.control_mode).der_control_cl_res;
            REQUIRE(der_control.enter_service_cl_res.permit_service == true);
        }
    }
}

SCENARIO("SAE AC DER charge loop dynamic mode") {

    const auto session =
        make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
    const auto req = make_dynamic_request(session);

    GIVEN("The EV did not declare the target active power function") {
        const auto res = call(req, session, Inputs{});

        THEN("ResponseCode: OK and the dynamic response control mode is chosen") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(std::holds_alternative<Dynamic_DER_Res>(res.control_mode));
        }

        THEN("The mandatory base target active power is still sent") {
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.target_active_power) == 7000.0f);
        }

        THEN("The optional target power phase values are withheld") {
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(res_mode.target_active_power_L2.has_value() == false);
            REQUIRE(res_mode.target_active_power_L3.has_value() == false);
            REQUIRE(res_mode.target_reactive_power.has_value() == false);
            REQUIRE(res_mode.target_reactive_power_L2.has_value() == false);
            REQUIRE(res_mode.target_reactive_power_L3.has_value() == false);
        }

        THEN("The dynamic mode parameters are not sent while the EV provides the mobility needs") {
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(res_mode.departure_time.has_value() == false);
            REQUIRE(res_mode.target_soc.has_value() == false);
            REQUIRE(res_mode.minimum_soc.has_value() == false);
            REQUIRE(res_mode.ack_max_delay.has_value() == false);
        }
    }

    GIVEN("The EV declared the target active power function") {
        const auto declaring_session =
            make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedByEvcc, WITH_TARGET_POWER_FUNCTIONS);
        const auto declaring_req = make_dynamic_request(declaring_session);

        const auto res = call(declaring_req, declaring_session, Inputs{});

        THEN("The target active power phase values are sent too") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(value_of(res_mode.target_active_power) == 7000.0f);
            REQUIRE(value_of(res_mode.target_active_power_L2.value()) == 3000.0f);
            REQUIRE(value_of(res_mode.target_active_power_L3.value()) == 4000.0f);
        }
    }
}

SCENARIO("SAE AC DER charge loop dynamic mode parameters") {

    const auto session =
        make_session(dt::ControlMode::Dynamic, dt::MobilityNeedsMode::ProvidedBySecc, CHARGE_AND_DISCHARGE_ONLY);
    const auto req = make_dynamic_request(session);

    GIVEN("A departure time in the future") {
        Inputs in{};
        in.dynamic_parameters = {std::time(nullptr) + 40, 95, 80};

        const auto res = call(req, session, in);

        THEN("The relative departure time and both soc values are sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(res_mode.departure_time.value_or(0) >= 39);
            REQUIRE(res_mode.target_soc.value_or(0) == 95);
            REQUIRE(res_mode.minimum_soc.value_or(0) == 80);
            REQUIRE(res_mode.ack_max_delay.value_or(0) == 30);
        }
    }

    GIVEN("A departure time already in the past") {
        Inputs in{};
        in.dynamic_parameters = {std::time(nullptr) - 60, 95, 80};

        const auto res = call(req, session, in);

        THEN("No departure time is sent instead of an underflowed offset") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(res_mode.departure_time.has_value() == false);
            REQUIRE(res_mode.target_soc.value_or(0) == 95);
        }
    }

    GIVEN("A minimum soc above the target soc") {
        Inputs in{};
        in.dynamic_parameters = {std::nullopt, 80, 95};

        const auto res = call(req, session, in);

        THEN("The minimum soc is withheld and the target soc is still sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            const auto& res_mode = std::get<Dynamic_DER_Res>(res.control_mode);
            REQUIRE(res_mode.minimum_soc.has_value() == false);
            REQUIRE(res_mode.target_soc.value_or(0) == 80);
        }
    }
}

SCENARIO("SAE AC DER charge loop conditional response fields") {

    const auto session =
        make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
    const auto req = make_scheduled_request(session);

    GIVEN("The operator provided a target frequency") {
        Inputs in{};
        in.targets.target_frequency = dt::RationalNumber{50, 0};

        const auto res = call(req, session, in);

        THEN("The target frequency is sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(value_of(res.target_frequency.value()) == 50.0f);
        }
    }

    GIVEN("The operator provided no target frequency") {
        const auto res = call(req, session, Inputs{});

        THEN("No target frequency is sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.target_frequency.has_value() == false);
        }
    }

    GIVEN("The charger terminates the session") {
        Inputs in{};
        in.stop = true;

        const auto res = call(req, session, in);

        THEN("A terminate notification is sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Terminate);
            REQUIRE(res.status.value().notification_max_delay == 0);
        }
    }

    GIVEN("The charger pauses the session") {
        Inputs in{};
        in.pause = true;

        const auto res = call(req, session, in);

        THEN("A pause notification is sent") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Pause);
            REQUIRE(res.status.value().notification_max_delay == 60);
        }
    }

    GIVEN("The charger terminates and pauses at once") {
        Inputs in{};
        in.stop = true;
        in.pause = true;

        const auto res = call(req, session, in);

        THEN("The terminate notification wins") {
            REQUIRE(res.response_code == dt::ResponseCode::OK);
            REQUIRE(res.status.value().notification == dt::EvseNotification::Terminate);
            REQUIRE(res.status.value().notification_max_delay == 0);
        }
    }
}

SCENARIO("SAE AC DER charge loop EV state logging is latched") {

    const auto session =
        make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);

    GIVEN("An EV reporting a DER alarm") {
        auto req = make_scheduled_request(session);
        std::get<Scheduled_DER_Req>(req.control_mode).der_alarm_status = 0x00000004u;

        LogState log_state{};
        REQUIRE(call(req, session, Inputs{}, log_state).response_code == dt::ResponseCode::OK);

        THEN("The reported alarm value is recorded so an unchanged alarm is not repeated") {
            REQUIRE(log_state.reported_der_alarm_status.value_or(0) == 0x00000004u);
        }

        THEN("A cleared alarm is recorded too so the same alarm is reported again later") {
            auto cleared_req = make_scheduled_request(session);
            std::get<Scheduled_DER_Req>(cleared_req.control_mode).der_alarm_status = 0;
            call(cleared_req, session, Inputs{}, log_state);
            REQUIRE(log_state.reported_der_alarm_status.value_or(0xFFFFFFFFu) == 0);
        }
    }

    GIVEN("An EV whose enabled modes do not match the modes the SECC enabled") {
        auto enabling_session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        enabling_session.set_enabled_der_control_modes(bit_of(sae::DerBitMapFunctions::VoltVarFunction));

        auto req = make_scheduled_request(enabling_session);
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes = bit_of(sae::DerBitMapFunctions::ChargeFunction);

        LogState log_state{};
        REQUIRE(call(req, enabling_session, Inputs{}, log_state).response_code == dt::ResponseCode::OK);

        THEN("The mismatch is latched so it is not reported again") {
            REQUIRE(log_state.reported_enabled_modes_mismatch == true);
        }
    }

    GIVEN("An EV that enables a function the SECC never enabled") {
        auto enabling_session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        enabling_session.set_enabled_der_control_modes(bit_of(sae::DerBitMapFunctions::VoltVarFunction));

        auto req = make_scheduled_request(enabling_session);
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes =
            bit_of(sae::DerBitMapFunctions::VoltVarFunction) | bit_of(sae::DerBitMapFunctions::WattVarFunction);

        LogState log_state{};
        REQUIRE(call(req, enabling_session, Inputs{}, log_state).response_code == dt::ResponseCode::OK);

        THEN("The surplus acknowledge is a mismatch and is latched") {
            REQUIRE(log_state.reported_enabled_modes_mismatch == true);
        }
    }

    GIVEN("An EV whose enabled modes match the modes the SECC enabled") {
        auto enabling_session =
            make_session(dt::ControlMode::Scheduled, dt::MobilityNeedsMode::ProvidedByEvcc, CHARGE_AND_DISCHARGE_ONLY);
        enabling_session.set_enabled_der_control_modes(bit_of(sae::DerBitMapFunctions::VoltVarFunction));

        auto req = make_scheduled_request(enabling_session);
        std::get<Scheduled_DER_Req>(req.control_mode).enabled_modes =
            CHARGE_AND_DISCHARGE_ONLY | bit_of(sae::DerBitMapFunctions::VoltVarFunction);

        LogState log_state{};
        REQUIRE(call(req, enabling_session, Inputs{}, log_state).response_code == dt::ResponseCode::OK);

        THEN("Nothing is latched") {
            REQUIRE(log_state.reported_enabled_modes_mismatch == false);
        }
    }
}
