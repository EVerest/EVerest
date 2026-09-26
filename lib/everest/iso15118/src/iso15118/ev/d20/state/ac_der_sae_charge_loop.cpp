// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <optional>

#include <iso15118/detail/d20/context_helper.hpp>
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_loop.hpp>
#include <iso15118/ev/d20/state/power_delivery.hpp>
#include <iso15118/ev/der_sae_control_validation.hpp>
#include <iso15118/ev/detail/d20/ac_target_power.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/detail/d20/sae_helper.hpp>
#include <iso15118/ev/sae_profile_emit.hpp>
#include <iso15118/message/ac_der_sae_charge_loop.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev::d20::state {

namespace {

namespace dt = message_20::datatypes;
namespace dt_sae = dt::sae;

message_20::DER_SAE_AC_ChargeLoopRequest make_request(const Context& ctx) {
    const auto params = ctx.get_ac_params();
    const auto& profile = ctx.sae_profile();

    message_20::DER_SAE_AC_ChargeLoopRequest req;
    setup_header(req.header, ctx.get_session());
    req.meter_info_requested = false;
    req.display_parameters = std::nullopt;
    req.control_mode = make_sae_cl_control_mode(
        profile, params, ctx.ac_connector(), params.present_voltage.value_or(profile.nominal_voltage_v),
        params.present_frequency.value_or(profile.nominal_frequency_hz), params.der_alarm_status,
        ctx.sae_enabled_modes(), ctx.sae_permit_service(), ctx.sae_settings_update_time());
    return req;
}

} // namespace

void AC_DER_SAE_ChargeLoop::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeLoop");
    m_ctx.send_request(make_request(m_ctx));
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

    if (res->status.has_value()) {
        if (res->status->notification == dt::EvseNotification::Terminate) {
            m_ctx.feedback.stop_from_charger();
            return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
        }
        if (res->status->notification == dt::EvseNotification::Pause) {
            // [V2G20-3317]: pause per [V2G20-1540]; SessionStop carries Pause.
            m_ctx.feedback.pause_from_charger();
            m_ctx.set_pause_charging_requested(true);
            return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
        }
    }

    if (m_ctx.is_stop_charging_requested() or m_ctx.is_pause_charging_requested()) {
        return m_ctx.create_state<PowerDelivery>(dt::Progress::Stop);
    }

    const auto& control = mode->der_control_cl_res;

    auto problems = validate_der_control(control);
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
    const bool enabled_changed = enabled != m_ctx.sae_enabled_modes();
    if (enabled_changed) {
        logf_info("SECC enabled SAE DER functions: %s", sae::sae_function_names(enabled).c_str());
        m_ctx.feedback.der_enabled_modes(enabled);
        m_ctx.set_sae_enabled_modes(enabled);
    }

    // [V2G20-3366]: without PermitService the next request reports Off/Disconnected; charging continues.
    const bool permit = control.enter_service_cl_res.permit_service;
    const bool permit_changed = permit != m_ctx.sae_permit_service();
    if (permit_changed and permit) {
        logf_info("SECC granted PermitService: resuming AC DER");
    } else if (permit_changed) {
        logf_info("SECC withdrew PermitService: stopping AC DER [V2G20-3366]");
    }
    m_ctx.set_sae_permit_service(permit);

    if (enabled_changed or permit_changed) {
        m_ctx.set_sae_settings_update_time(iso15118::d20::now_in_secc_time());
    }

    m_ctx.feedback.sae_der_control(*mode, last_problems_);
    m_ctx.feedback.ac_target_power(make_ac_target_power(*mode, res->target_frequency));

    m_ctx.send_request(make_request(m_ctx));
    return Result::awaiting();
}

} // namespace iso15118::ev::d20::state
