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

    /// CM_VALIDATE BCB-toggle validation, ISO 15118-3 9.4. Two steps, told apart by the REQ's
    /// pilotTimer being zero or not.
    void handle_validate_req(messages::HomeplugMessage const& frame);

    /// Run on every update tick.
    void validate_tick();

    void send_validate_cnf_reply(MacAddress const& mac, std::uint8_t result, std::uint8_t toggle_num);

    bool any_session_matched() const;
    bool all_sessions_failed() const;
    int max_matching_sessions() const;

    /// Sessions are held by pointer: the session states reference the session data, so it must not
    /// move when the list grows.
    std::vector<std::unique_ptr<Session>> m_sessions;
    Timer m_deadline;
    bool m_failed_matching_reset_once{false};

    bool m_validate_armed{false};         // step 1 seen; awaiting step 2 or repeating the step-1 CNF
    bool m_validate_step2_pending{false}; // step 2 seen; waiting out the toggle observation window
    int m_validate_step1_retries{0};      // step-1 repetitions so far, at most C_EV_MATCH_RETRY
    int m_validate_baseline_bc{0};        // bc_transition_count when the observation started
    MacAddress m_validate_owner_mac{};    // the EV that owns the validation in progress
    Timer m_validate_timer;               // step-1 repeat interval, then the step-2 window
};

} // namespace everest::slac::evse::state
