// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <everest/slac/evse/states.hpp>
#include <everest/slac/protocol/types.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// Set the network membership key on the modem. Under retry_confirmed the new NMK is staged in
// m_pending_nmk and only promoted once the modem confirms; see SetKeyHandlingMode for the rest.
struct Reset : public StateBase {
    explicit Reset(Context& ctx) : StateBase(ctx, StateID::Reset) {
    }

    void enter() final;
    Result feed(SlacEvent const&) final;
    void describe(StateTree& out) const final;
    void signature(std::string& out) const final;

private:
    void send_set_key_req();
    bool is_retry_confirmed() const;
    Result handle_set_key_cnf(messages::HomeplugMessage const& frame);
    Result leave_reset();

    Nmk m_pending_nmk{};
    int m_set_key_attempts{0};
    Timer m_set_key_timer;
    /// The modem has answered, or we have given up waiting for it.
    bool m_settled{false};
};

} // namespace everest::slac::evse::state
