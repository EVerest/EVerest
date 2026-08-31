// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/state/failed.hpp>
#include <everest/slac/evse/state/reset.hpp>

namespace everest::slac::evse::state {

void Failed::enter() {
    if (m_ctx.slac_config.ac_mode_five_percent) {
        m_ctx.signal_error_routine_request();
    }
    m_ctx.clear_match_confirm_cache();
    m_ctx.status.match_state = SlacState::Failed;
    m_ctx.status.d3_state = D3State::Unmatched;
}

Result Failed::feed(SlacEvent const& ev) {
    if (std::get_if<event::Reset>(&ev)) {
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::evse::state
