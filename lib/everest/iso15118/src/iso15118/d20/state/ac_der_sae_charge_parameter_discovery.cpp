// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/d20/state/schedule_exchange.hpp>
#include <iso15118/d20/state/service_detail.hpp>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/d20/state/ac_der_sae_convert.hpp>
#include <iso15118/detail/d20/state/service_discovery.hpp>
#include <iso15118/detail/d20/state/session_stop.hpp>
#include <iso15118/detail/helper.hpp>

#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>

#include <everest/util/vector/fixed_vector.hpp>

#include <cmath>

namespace iso15118::d20::state {

namespace dt = message_20::datatypes;

namespace {

void convert_sae_limits(dt::sae::DER_SAE_AC_CPDResEnergyTransferMode& out, const d20::SaeDerTransferLimits& in) {
    out.nominal_charge_power = in.nominal_charge_power;
    out.nominal_charge_power_L2 = in.nominal_charge_power_L2;
    out.nominal_charge_power_L3 = in.nominal_charge_power_L3;
    out.nominal_discharge_power = in.nominal_discharge_power;
    out.nominal_discharge_power_L2 = in.nominal_discharge_power_L2;
    out.nominal_discharge_power_L3 = in.nominal_discharge_power_L3;
    out.maximum_discharge_power = in.max_discharge_power;
    out.maximum_discharge_power_L2 = in.max_discharge_power_L2;
    out.maximum_discharge_power_L3 = in.max_discharge_power_L3;

    const auto& reactive_power_limits = in.reactive_power_limits;
    out.reactive_power_limits.maximum_var_absorption_during_charging =
        reactive_power_limits.maximum_var_absorption_during_charging;
    out.reactive_power_limits.maximum_var_absorption_during_charging_L2 =
        reactive_power_limits.maximum_var_absorption_during_charging_L2;
    out.reactive_power_limits.maximum_var_absorption_during_charging_L3 =
        reactive_power_limits.maximum_var_absorption_during_charging_L3;
    out.reactive_power_limits.maximum_var_injection_during_charging =
        reactive_power_limits.maximum_var_injection_during_charging;
    out.reactive_power_limits.maximum_var_injection_during_charging_L2 =
        reactive_power_limits.maximum_var_injection_during_charging_L2;
    out.reactive_power_limits.maximum_var_injection_during_charging_L3 =
        reactive_power_limits.maximum_var_injection_during_charging_L3;
    out.reactive_power_limits.maximum_var_absorption_during_discharging =
        reactive_power_limits.maximum_var_absorption_during_discharging;
    out.reactive_power_limits.maximum_var_absorption_during_discharging_L2 =
        reactive_power_limits.maximum_var_absorption_during_discharging_L2;
    out.reactive_power_limits.maximum_var_absorption_during_discharging_L3 =
        reactive_power_limits.maximum_var_absorption_during_discharging_L3;
    out.reactive_power_limits.maximum_var_injection_during_discharging =
        reactive_power_limits.maximum_var_injection_during_discharging;
    out.reactive_power_limits.maximum_var_injection_during_discharging_L2 =
        reactive_power_limits.maximum_var_injection_during_discharging_L2;
    out.reactive_power_limits.maximum_var_injection_during_discharging_L3 =
        reactive_power_limits.maximum_var_injection_during_discharging_L3;

    const auto& grid_limits = in.grid_limits;
    out.grid_limits.nominal_frequency = grid_limits.nominal_frequency;
    out.grid_limits.nominal_voltage = grid_limits.nominal_voltage;
    out.grid_limits.nominal_voltage_offset = grid_limits.nominal_voltage_offset;
    out.grid_limits.min_frequency = grid_limits.min_frequency;
    out.grid_limits.max_frequency = grid_limits.max_frequency;
    out.grid_limits.maximum_voltage = grid_limits.maximum_voltage;
    out.grid_limits.minimum_voltage = grid_limits.minimum_voltage;
}

// Bits 2, 9, 25 and 27 to 31 are unused by the specification and must be ignored.
constexpr uint32_t SAE_MODE_BITMAP_MASK = 0x05FFFDFBu;

bool is_function_set(uint32_t bitmap, sae::DerBitMapFunctions function) {
    return (bitmap & (1U << message_20::to_underlying_value(function))) != 0U;
}

void gate_enable(bool& enable, bool supported, const char* function_name) {
    if (enable and not supported) {
        logf_warning("Clearing enable of %s: EV did not declare support for it", function_name);
        enable = false;
    }
}

void gate_optional_curve(std::optional<dt::sae::DERCurve>& curve, bool supported, const char* function_name) {
    if (curve.has_value()) {
        gate_enable(curve.value().enable, supported, function_name);
    }
}

// The SECC expresses itself only through the per-function Enable flags, so a function the EV does not
// list in SupportedModes must not be enabled. This follows a semantics statement, not a numbered
// requirement.
// Masked bits without a gate: 0 and 1 (ChargeFunction, DischargeFunction) are inherent to the service,
// 21 and 22 (EVSETargetReactivePowerFunction, EVSETargetActivePowerFunction) are charge loop targets
// with no Enable in DERControlCPDRes.
// The call order below follows the DERControlCPDRes declaration order so it can be audited side by side.
void gate_enables_by_supported_modes(dt::sae::DERControlCPDRes& out, uint32_t supported_modes) {
    using F = sae::DerBitMapFunctions;

    auto& voltage_trip = out.voltage_trip;
    gate_enable(voltage_trip.over_voltage_must_trip_curve.enable,
                is_function_set(supported_modes, F::HighVoltageMustTripFunction), "over voltage must trip curve");
    gate_enable(voltage_trip.under_voltage_must_trip_curve.enable,
                is_function_set(supported_modes, F::LowVoltageMustTripFunction), "under voltage must trip curve");
    gate_optional_curve(voltage_trip.over_voltage_momentary_cessation_trip_curve,
                        is_function_set(supported_modes, F::HighVoltageMomentaryCessationFunction),
                        "over voltage momentary cessation trip curve");
    gate_optional_curve(voltage_trip.under_voltage_momentary_cessation_trip_curve,
                        is_function_set(supported_modes, F::LowVoltageMomentaryCessationFunction),
                        "under voltage momentary cessation trip curve");
    gate_optional_curve(voltage_trip.over_voltage_may_trip_curve,
                        is_function_set(supported_modes, F::HighVoltageMayTripFunction), "over voltage may trip curve");
    gate_optional_curve(voltage_trip.under_voltage_may_trip_curve,
                        is_function_set(supported_modes, F::LowVoltageMayTripFunction), "under voltage may trip curve");

    auto& frequency_trip = out.frequency_trip;
    gate_enable(frequency_trip.over_frequency_must_trip_curve.enable,
                is_function_set(supported_modes, F::HighFrequencyMustTripFunction), "over frequency must trip curve");
    gate_enable(frequency_trip.under_frequency_must_trip_curve.enable,
                is_function_set(supported_modes, F::LowFrequencyMustTripFunction), "under frequency must trip curve");
    gate_optional_curve(frequency_trip.over_frequency_may_trip_curve,
                        is_function_set(supported_modes, F::HighFrequencyMayTripFunction),
                        "over frequency may trip curve");
    gate_optional_curve(frequency_trip.under_frequency_may_trip_curve,
                        is_function_set(supported_modes, F::LowFrequencyMayTripFunction),
                        "under frequency may trip curve");

    gate_enable(out.enter_service_cpd_res.permit_service, is_function_set(supported_modes, F::EnterService),
                "enter service");

    auto& reactive = out.reactive_power_support_cpd_res;
    // Either excitation direction keeps the constant power factor function alive.
    gate_enable(reactive.constant_power_factor.enable,
                is_function_set(supported_modes, F::ConstantPowerFactorUnderExcitedFunction) or
                    is_function_set(supported_modes, F::ConstantPowerFactorOverExcitedFunction),
                "constant power factor");
    gate_enable(reactive.volt_var.enable, is_function_set(supported_modes, F::VoltVarFunction), "volt var");
    gate_enable(reactive.watt_var.enable, is_function_set(supported_modes, F::WattVarFunction), "watt var");
    gate_enable(reactive.constant_var.enable, is_function_set(supported_modes, F::ConstantReactivePowerFunction),
                "constant var");

    auto& active = out.active_power_support_cpd_res;
    gate_enable(active.frequency_droop.enable, is_function_set(supported_modes, F::FrequencyDroopFunction),
                "frequency droop");
    gate_enable(active.volt_watt.enable, is_function_set(supported_modes, F::VoltWattFunction), "volt watt");
    gate_enable(active.constant_watt.enable, is_function_set(supported_modes, F::ConstantActivePowerFunction),
                "constant watt");
    gate_enable(active.limit_max_discharge_power.enable,
                is_function_set(supported_modes, F::LimitMaximumActiveDischargePowerFunction),
                "limit maximum discharge power");
}

std::optional<dt::RationalNumber> phase_maximum(const std::optional<Limit<dt::RationalNumber>>& limit) {
    if (not limit.has_value()) {
        return std::nullopt;
    }
    return limit.value().max;
}

bool check_nominal_within_maximum(const std::optional<dt::RationalNumber>& nominal,
                                  const std::optional<dt::RationalNumber>& maximum, const char* quantity,
                                  bool use_magnitude) {
    if (not nominal.has_value()) {
        return true;
    }

    if (not maximum.has_value()) {
        logf_error("Nominal %s is configured but the maximum %s is not", quantity, quantity);
        return false;
    }

    auto value_of = [use_magnitude](const dt::RationalNumber& number) {
        const auto value = dt::from_RationalNumber(number);
        return use_magnitude ? std::fabs(value) : value;
    };

    const auto nominal_value = value_of(nominal.value());
    const auto maximum_value = value_of(maximum.value());

    if (nominal_value > maximum_value) {
        logf_error("Nominal %s %f exceeds maximum %s %f", quantity, nominal_value, quantity, maximum_value);
        return false;
    }

    return true;
}

message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse
handle_request(const message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest& req, d20::Session& session,
               const d20::AcTransferLimits& limits, const d20::AcPresentPower& powers,
               const std::optional<d20::SaeDerTransferLimits>& sae_limits,
               const std::optional<DerSaeSetupConfig>& config) {

    message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse res;

    if (not validate_and_setup_header(res.header, session, req.header.session_id)) {
        return response_with_code(res, message_20::datatypes::ResponseCode::FAILED_UnknownSession);
    }

    if (not sae_limits.has_value()) {
        logf_error("No SAE limits are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    if (not config.has_value()) {
        logf_error("No SAE DER control values are provided. Shutdown the session");
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    // NOTE(SL): At this point, it's clear that it can only be DER TransferMode

    const auto& sae_config = config.value();
    const auto& der_limits = sae_limits.value();

    // Validated before the session is touched and before the DER dictate is built.
    const bool nominals_within_maxima =
        check_nominal_within_maximum(der_limits.nominal_charge_power, limits.charge_power.max, "charge power",
                                     false) and
        check_nominal_within_maximum(der_limits.nominal_charge_power_L2, phase_maximum(limits.charge_power_L2),
                                     "charge power L2", false) and
        check_nominal_within_maximum(der_limits.nominal_charge_power_L3, phase_maximum(limits.charge_power_L3),
                                     "charge power L3", false) and
        check_nominal_within_maximum(der_limits.nominal_discharge_power, der_limits.max_discharge_power,
                                     "discharge power", true) and
        check_nominal_within_maximum(der_limits.nominal_discharge_power_L2, der_limits.max_discharge_power_L2,
                                     "discharge power L2", true) and
        check_nominal_within_maximum(der_limits.nominal_discharge_power_L3, der_limits.max_discharge_power_L3,
                                     "discharge power L3", true);

    if (not nominals_within_maxima) {
        return response_with_code(res, dt::ResponseCode::FAILED_WrongChargeParameter);
    }

    auto& mode = res.transfer_mode;
    mode.min_charge_power = limits.charge_power.min;
    mode.max_charge_power = limits.charge_power.max;

    if (limits.charge_power_L2.has_value()) {
        mode.min_charge_power_L2 = limits.charge_power_L2.value().min;
        mode.max_charge_power_L2 = limits.charge_power_L2.value().max;
    }

    if (limits.charge_power_L3.has_value()) {
        mode.min_charge_power_L3 = limits.charge_power_L3.value().min;
        mode.max_charge_power_L3 = limits.charge_power_L3.value().max;
    }

    mode.nominal_frequency = limits.nominal_frequency;
    mode.max_power_asymmetry = limits.max_power_asymmetry;
    mode.power_ramp_limitation = limits.power_ramp_limitation;
    mode.present_active_power = powers.present_active_power;
    mode.present_active_power_L2 = powers.present_active_power_L2;
    mode.present_active_power_L3 = powers.present_active_power_L3;

    // mode.status
    const auto ev_supported_modes = req.transfer_mode.supported_modes & SAE_MODE_BITMAP_MASK;

    // req.transfer_mode.enabled_modes is deliberately not consumed: the SECC dictates through the Enable
    // flags, so an inequality between SupportedModes and EnabledModes is not an error.

    if (not(is_function_set(ev_supported_modes, sae::DerBitMapFunctions::ChargeFunction) and
            is_function_set(ev_supported_modes, sae::DerBitMapFunctions::DischargeFunction))) {
        logf_warning("EV did not set both ChargeFunction and DischargeFunction in SupportedModes (0x%08x), "
                     "continuing the session anyway",
                     req.transfer_mode.supported_modes);
    }

    session.set_ev_supported_sae_functions(ev_supported_modes);

    convert(mode.der_control_cpd_res, sae_config.der_control);
    gate_enables_by_supported_modes(mode.der_control_cpd_res, ev_supported_modes);

    mode.processing =
        req.transfer_mode.processing == dt::Processing::Ongoing ? dt::Processing::Ongoing : dt::Processing::Finished;

    convert_sae_limits(mode, der_limits);

    if (sae_config.required_der_operating_mode == sae::RequiredDEROperatingMode::GridFollowing) {
        mode.required_der_operating_mode = dt::sae::RequiredDEROperatingMode::GridFollowing;
    } else if (sae_config.required_der_operating_mode == sae::RequiredDEROperatingMode::GridForming) {
        mode.required_der_operating_mode = dt::sae::RequiredDEROperatingMode::GridForming;
    }

    if (sae_config.grid_connection_mode == sae::GridConnectionMode::GridConnected) {
        mode.grid_connection_mode = dt::sae::GridConnectionMode::GridConnected;
    } else if (sae_config.grid_connection_mode == sae::GridConnectionMode::GridIslanded) {
        mode.grid_connection_mode = dt::sae::GridConnectionMode::GridIslanded;
    }

    mode.update_time = sae_config.der_control_update_time;

    return response_with_code(res, message_20::datatypes::ResponseCode::OK);
}

} // namespace

void AC_DER_SAE_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeParameterDiscovery");
    present_powers = m_ctx.cache_ac_present_power.value_or(AcPresentPower{});
}

Result AC_DER_SAE_ChargeParameterDiscovery::feed(Event ev) {

    if (ev == Event::CONTROL_MESSAGE) {
        if (const auto* control_data = m_ctx.get_control_event<AcPresentPower>()) {
            present_powers = *control_data;
        }
        return {};
    }

    if (ev != Event::V2GTP_MESSAGE) {
        return {};
    }

    const auto variant = m_ctx.pull_request();

    if (const auto* const req = variant->get_if<message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest>()) {

        // m_ctx.session_ev_info.ev_transfer_limits.emplace<dt::DER_AC_CPDReqEnergyTransferMode>(req->transfer_mode);

        const auto res = handle_request(*req, m_ctx.session, m_ctx.session_config.ac_limits, present_powers,
                                        m_ctx.session_config.der_sae_limits, m_ctx.session_config.der_sae_setup_config);

        m_ctx.respond(res);

        if (res.response_code >= message_20::datatypes::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        // m_ctx.feedback.ac_limits(req->transfer_mode);

        if (req->transfer_mode.processing == dt::Processing::Finished and
            res.transfer_mode.processing == dt::Processing::Finished) {
            return m_ctx.create_state<ScheduleExchange>(); // [V2G20-]
        }
        return {}; // [V2G20-3150]: Stay in the state because ev set processing to Ongoing
    }

    // The EV may restart service selection if it does not accept the dictated DER control settings.
    if (const auto* const req = variant->get_if<message_20::ServiceDiscoveryRequest>()) {
        const auto res =
            handle_request(*req, m_ctx.session, m_ctx.session_config.supported_energy_transfer_services,
                           m_ctx.session_config.supported_vas_services, m_ctx.session_ev_info.ev_energy_services);

        m_ctx.respond(res);

        if (res.response_code >= message_20::datatypes::ResponseCode::FAILED) {
            m_ctx.session_stopped = true;
            return {};
        }

        return m_ctx.create_state<ServiceDetail>();
    }

    if (const auto* const req = variant->get_if<message_20::SessionStopRequest>()) {
        const auto res = handle_request(*req, m_ctx.session);

        m_ctx.respond(res);
        m_ctx.session_stopped = true;

        return {};
    }

    logf_warning("expected DER_SAE_AC_ChargeParameterDiscovery! But code type id: %d", variant->get_type());
    m_ctx.session_stopped = true;

    // Sequence Error
    const message_20::Type req_type = variant->get_type();
    send_sequence_error(req_type, m_ctx);

    return {};
}

} // namespace iso15118::d20::state
