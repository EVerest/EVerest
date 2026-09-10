// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/din/states.hpp>

#include <iso15118/detail/din/state/sequence_error.hpp>

namespace iso15118::din {

Result StateBase::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return on_event(ev);
    }

    // Consumed here, once, for every state: no state hands a message to a successor any more.
    const auto variant = m_ctx.pull_request();

    m_ctx.feedback.v2g_message(variant->get_type());

    // [V2G-DC-391] applies to every request in every state, so it is checked once, here: a request
    // naming another session is not ours to sequence. It also runs ahead of each state's own
    // [V2G-DC-666] check -- both can apply to one message, and the specification orders neither.
    if (state::reject_unknown_session(m_ctx, *variant)) {
        return {};
    }

    return on_request(*variant);
}

} // namespace iso15118::din
