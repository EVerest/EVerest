// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/reset.hpp>

namespace everest::slac::ev::state {

void Failed::enter() {
    m_ctx.d3_state = D3State::Unmatched;
    m_ctx.signal_dlink_ready(false);
    m_ctx.log_warn("EV MSM entered failed");
}

Result Failed::feed(SlacEvent const& ev) {
    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
