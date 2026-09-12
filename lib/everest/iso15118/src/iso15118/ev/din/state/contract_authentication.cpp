// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/din/state/contract_authentication.hpp>

#include <iso15118/detail/helper.hpp>
#include <iso15118/ev/detail/din/context_helper.hpp>
#include <iso15118/ev/detail/din/state/contract_authentication.hpp>
#include <iso15118/ev/din/state/charge_parameter_discovery.hpp>

namespace iso15118::ev::din::state {

namespace contract_authentication {

namespace dt = message_din::datatypes;

message_din::ContractAuthenticationRequest create_request() {
    // EIM: id and gen_challenge omitted.
    return {};
}

bool authorization_finished(const message_din::ContractAuthenticationResponse& res) {
    return res.evse_processing == dt::EvseProcessing::Finished;
}

} // namespace contract_authentication

void ContractAuthentication::enter() {
    logf_debug("Enter state: ContractAuthentication (DIN 70121)");
    m_ctx.send_request(contract_authentication::create_request());
}

Result ContractAuthentication::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return Result::ignored();
    }

    const auto variant = m_ctx.pull_response();

    const auto* res = expect_response<message_din::ContractAuthenticationResponse>(m_ctx, *variant);
    if (res == nullptr) {
        return Result::stopping();
    }

    if (auto stop = stop_before_start(m_ctx)) {
        return std::move(*stop);
    }

    if (contract_authentication::authorization_finished(*res)) {
        return m_ctx.create_state<ChargeParameterDiscovery>();
    }

    // EVSEProcessing::Ongoing: re-poll. The session owns the ongoing guard.
    m_ctx.send_request(contract_authentication::create_request());
    return Result::awaiting();
}

} // namespace iso15118::ev::din::state
