// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <optional>
#include <utility>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/der/sae_requests.hpp>
#include <iso15118/ev/d20/der/sae_validation.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/ev/d20/state/leave_charge_loop.hpp>
#include <iso15118/ev/detail/d20/ac_target_power.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/detail/d20/sae_helper.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev::d20::state {

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;

message_20::DER_SAE_AC_ChargeLoopRequest
AC_DER_SAE_ChargeLoop::make_request(const der::SaeInverterProfile& profile, const params::AcChargeParams& params,
                                    dt::AcConnector connector, std::uint32_t enabled_modes, bool permit_service,
                                    std::uint64_t settings_update_time, const EnergyRequest& energy) {
    message_20::DER_SAE_AC_ChargeLoopRequest req;
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;
    req.control_mode = d20::der::make_sae_cl_control_mode(
        profile, params, connector, params.present_voltage.value_or(profile.nominal_voltage_v),
        params.present_frequency.value_or(profile.nominal_frequency_hz), params.der_alarm_status, enabled_modes,
        permit_service, settings_update_time, energy);
    return req;
}

message_20::DER_SAE_AC_ChargeLoopRequest AC_DER_SAE_ChargeLoop::make_request(const Context& ctx,
                                                                             const der::SaeNegotiation& negotiation) {
    return make_request(ctx.sae_profile(), ctx.get_ac_params(), ctx.ac_connector(), negotiation.enabled_modes,
                        negotiation.permit_service, negotiation.settings_update_time, ctx.energy_request());
}

void AC_DER_SAE_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeLoop");
}

Result AC_DER_SAE_ChargeLoop::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_SAE_AC_ChargeLoopResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    const auto* mode = std::get_if<dt_sae::DER_Dynamic_AC_CLResControlMode>(&res->control_mode);
    if (mode == nullptr) {
        logf_error("DER_SAE_AC_ChargeLoopResponse offers a control mode the EV did not request");
        m_ctx.stop_session();
        return Result::stopping();
    }

    // [V2G20-3317]: an EVSE Pause pauses per [V2G20-1540], unless the EV has latched a stop, which terminates.
    if (auto stop = leave_charge_loop(m_ctx, res->status)) {
        return std::move(*stop);
    }

    const auto& control = mode->der_control_cl_res;

    auto problems = d20::der::validate_der_control(control);
    const bool stopping = not problems.empty() and m_ctx.der_stop_on_invalid_control();
    if (stopping or problems != last_problems_) {
        for (const auto& problem : problems) {
            logf_warning("SAE DER control: %s", problem.c_str());
        }
        if (problems.empty()) {
            logf_info("SAE DER control problems cleared");
        } else if (not stopping) {
            logf_warning("Continuing despite %zu SAE DER control problems", problems.size());
        }
    }
    if (stopping) {
        logf_error("Stopping the session on invalid SAE DER control");
        // The owner still sees the block that ends the session.
        m_ctx.feedback.sae_der_control(*mode, problems);
        m_ctx.stop_session();
        return Result::stopping();
    }
    last_problems_ = std::move(problems);

    const auto enabled = supported_enabled_modes(m_ctx, control);
    const bool enabled_changed = enabled != negotiation_.enabled_modes;
    if (enabled_changed) {
        logf_info("SECC enabled SAE DER functions: %s", sae::sae_function_names(enabled).c_str());
        m_ctx.feedback.der_enabled_modes(enabled);
        negotiation_.enabled_modes = enabled;
    }

    // [V2G20-3366]: without PermitService the next request reports Off/Disconnected; charging continues.
    const bool permit = control.enter_service_cl_res.permit_service;
    const bool permit_changed = permit != negotiation_.permit_service;
    if (permit_changed and permit) {
        logf_info("SECC granted PermitService: resuming AC DER");
    } else if (permit_changed) {
        logf_info("SECC withdrew PermitService: stopping AC DER [V2G20-3366]");
    }
    negotiation_.permit_service = permit;

    if (enabled_changed or permit_changed) {
        negotiation_.settings_update_time = m_ctx.secc_clock().now();
    }

    m_ctx.feedback.sae_der_control(*mode, last_problems_);
    m_ctx.feedback.ac_target_power(make_ac_target_power(*mode, res->target_frequency));

    m_ctx.send_request(make_request(m_ctx, negotiation_));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
