// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <memory>
#include <vector>

#include <everest/slac/evse/state/session.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse::state {

// Listening for EVs and running their matching sessions. The order inside feed() is not cosmetic:
// the outcome is decided BEFORE the tick reaches the sessions, so always on the previous tick's
// session states.
struct Matching : public StateBase {
    explicit Matching(Context& ctx) : StateBase(ctx, StateID::Matching) {
    }

    void enter() final;
    void leave() final;
    Result feed(SlacEvent const&) final;

    void signature(std::string& out) const final;
    void describe(StateTree& out) const final;

private:
    using Session = session::Session;
    using SessionData = session::SessionData;

    /// An empty result means stay.
    Result check_outcome();

    void add_session(messages::HomeplugMessage const& frame);

    /// Reject a CM_VALIDATE.REQ, as the legacy implementation does.
    void send_validate_cnf(messages::HomeplugMessage const& frame);

    bool any_session_matched() const;
    bool all_sessions_failed() const;
    int max_matching_sessions() const;

    /// Sessions are held by pointer: the session states reference the session data, so it must not
    /// move when the list grows.
    std::vector<std::unique_ptr<Session>> m_sessions;
    Timer m_deadline;
    bool m_failed_matching_reset_once{false};
};

} // namespace everest::slac::evse::state
