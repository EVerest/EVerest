// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#include <iso15118/d2/states.hpp>

#include <iso15118/detail/d2/state/sequence_error.hpp>

namespace iso15118::d2 {

Result StateBase::feed(Event ev) {
    if (ev != Event::V2GTP_MESSAGE) {
        return on_event(ev);
    }

    // Consumed here, once, for every state: no state hands a message to a successor any more.
    const auto variant = m_ctx.pull_request();

    // Reported here because the variant is in hand and feed() runs exactly once per received frame.
    m_ctx.feedback.v2g_message(variant->get_type());

    // [V2G2-460] applies to every request in every state, so it is checked once, here: a request naming
    // another session is not ours to sequence, and the header settles that before the body is
    // considered. That is also why it runs ahead of each state's own [V2G2-459] check -- both can apply
    // to one message, and the spec orders neither.
    //
    // SessionStopReq is deliberately not special-cased: Figures 103/104 place it in two states only, and
    // [V2G2-538] makes it a sequence error everywhere else. The spec's abort path is a connection
    // teardown ([V2G2-737] -> [V2G2-728] -> [V2G2-025]), not a SessionStopReq.
    if (state::reject_unknown_session(m_ctx, variant->get_type(), variant->get_session_id())) {
        return {};
    }

    return on_request(*variant);
}

} // namespace iso15118::d2
