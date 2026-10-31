// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/d20/der/sae_requests.hpp>
#include <iso15118/ev/d20/der/sae_validation.hpp>
#include <iso15118/ev/d20/state/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/ev/d20/state/schedule_exchange.hpp>
#include <iso15118/ev/d20/state/stop_before_start.hpp>
#include <iso15118/ev/detail/d20/context_helper.hpp>
#include <iso15118/ev/detail/d20/sae_helper.hpp>
#include <iso15118/message/ac_der_sae_charge_parameter_discovery.hpp>
#include <iso15118/sae_modes.hpp>

namespace iso15118::ev::d20::state {

namespace dt = message_20::datatypes;

namespace {

// The request of the round after `rounds_answered`, Finished on the last configured round.
message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest
make_round_request(const Context& ctx, std::uint16_t rounds_answered, std::uint32_t enabled_modes) {
    const auto processing =
        (rounds_answered + 1 < ctx.cpd_rounds()) ? dt::Processing::Ongoing : dt::Processing::Finished;
    return AC_DER_SAE_ChargeParameterDiscovery::make_request(ctx.sae_profile(), ctx.get_ac_params(), ctx.ac_connector(),
                                                             processing, enabled_modes, ctx.secc_clock().now(),
                                                             ctx.secc_clock());
}

} // namespace

message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest AC_DER_SAE_ChargeParameterDiscovery::make_request(
    const der::SaeInverterProfile& profile, const params::AcChargeParams& params, dt::AcConnector connector,
    dt::Processing processing, std::uint32_t enabled_modes, std::uint64_t update_time, const SeccClock& secc_clock) {
    message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest req;
    req.transfer_mode = d20::der::make_sae_cpd_transfer_mode(profile, params, connector, processing, enabled_modes,
                                                             update_time, secc_clock);
    return req;
}

message_20::DER_SAE_AC_ChargeParameterDiscoveryRequest
AC_DER_SAE_ChargeParameterDiscovery::make_first_request(const Context& ctx) {
    return make_round_request(ctx, 0, der::SaeNegotiation{}.enabled_modes);
}

void AC_DER_SAE_ChargeParameterDiscovery::enter() {
    logf_debug("Enter state: AC_DER_SAE_ChargeParameterDiscovery");
}

Result AC_DER_SAE_ChargeParameterDiscovery::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_20::DER_SAE_AC_ChargeParameterDiscoveryResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }
    if (m_rounds_answered < m_ctx.cpd_rounds()) {
        ++m_rounds_answered;
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    const auto& control = res->transfer_mode.der_control_cpd_res;

    const auto problems = d20::der::validate_der_control(control);
    for (const auto& problem : problems) {
        logf_warning("SAE DER control: %s", problem.c_str());
    }
    if (not problems.empty()) {
        if (m_ctx.der_stop_on_invalid_control()) {
            logf_error("Stopping the session on invalid SAE DER control");
            // The owner still sees the block that ends the session.
            m_ctx.feedback.sae_cpd_limits(res->transfer_mode, problems);
            m_ctx.stop_session();
            return Result::stopping();
        }
        logf_warning("Continuing despite %zu SAE DER control problems", problems.size());
    }

    m_negotiation.enabled_modes = supported_enabled_modes(m_ctx, control);
    m_negotiation.permit_service = control.enter_service_cpd_res.permit_service;
    m_negotiation.settings_update_time = m_ctx.secc_clock().now();
    logf_info("SECC enabled SAE DER functions: %s", sae::sae_function_names(m_negotiation.enabled_modes).c_str());

    if (not m_negotiation.permit_service) {
        // [V2G20-3363]: this state only records PermitService; acting on it belongs to the ChargeLoop.
        logf_info("SECC denied PermitService [V2G20-3363]");
    }

    m_ctx.feedback.ac_limits(res->transfer_mode);
    m_ctx.feedback.sae_cpd_limits(res->transfer_mode, problems);
    m_ctx.feedback.der_enabled_modes(m_negotiation.enabled_modes);

    // [V2G20-3356]: the SECC holds the exchange; the EV sends Finished once its rounds are done.
    if (res->transfer_mode.processing != dt::Processing::Finished) {
        m_ctx.send_request(make_round_request(m_ctx, m_rounds_answered, m_negotiation.enabled_modes));
        return Result::awaiting();
    }

    if (m_rounds_answered < m_ctx.cpd_rounds()) {
        // [V2G20-3357]: after Finished the next request is ScheduleExchangeReq, even with rounds left.
        logf_warning("SECC reported Finished to an Ongoing request [V2G20-3352]; skipping %u remaining CPD rounds",
                     static_cast<unsigned>(m_ctx.cpd_rounds() - m_rounds_answered));
    }

    m_ctx.send_request(ScheduleExchange::make_request(m_ctx));
    return m_ctx.create_state<ScheduleExchange>(m_negotiation);
}

} // namespace iso15118::ev::d20::state
