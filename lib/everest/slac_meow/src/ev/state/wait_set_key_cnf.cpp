// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>

#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/matched.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/wait_set_key_cnf.hpp>
#include <everest/slac/protocol/defs.hpp>

namespace everest::slac::ev::state {

void WaitSetKeyCnf::enter() {
    auto const configured = m_ctx.slac_config.set_key_timeout_ms;
    auto const timeout_ms =
        (configured > 0) ? static_cast<std::uint32_t>(configured) : static_cast<std::uint32_t>(defs::TT_MATCH_JOIN_MS);
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(timeout_ms));
}

Result WaitSetKeyCnf::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>();
        }
        return {};
    }

    if (auto const* message = get_if_message(ev)) {
        if (is_set_key_cnf(*message)) {
            return m_ctx.create_state<Matched>();
        }
        return {};
    }

    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
