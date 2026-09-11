// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>

#include <everest/util/fsm/fsm.hpp>

#include <everest/slac/evse/session_data.hpp>
#include <everest/slac/evse/states.hpp>
#include <everest/slac/time.hpp>

// One matching session with a single EV. The EVSE runs several at once: PLC is a shared medium, so
// neighbouring charge points cross-talk and more than one EV can be sounding at the same time.
namespace everest::slac::evse::state::session {

using SessionData = evse::SessionData;

/// Everything these states need beyond the context is the session data.
struct SessionStateBase : public StateBase {
    SessionStateBase(Context& ctx, SessionData& data, StateID id) : StateBase(ctx, id), m_data(data) {
    }

protected:
    SessionData& m_data;
};

struct WaitStartAtten : public SessionStateBase {
    WaitStartAtten(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionWaitStartAtten) {
    }
    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

struct Sounding : public SessionStateBase {
    Sounding(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionSounding) {
    }
    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

/// A short settling delay before reporting the averaged attenuation.
struct FinalizeSounding : public SessionStateBase {
    FinalizeSounding(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionFinalizeSounding) {
    }
    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

/// CM_ATTEN_CHAR.IND is out; waiting for the EV to acknowledge it, re-sending on timeout.
struct WaitAttenRsp : public SessionStateBase {
    WaitAttenRsp(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionWaitAttenRsp) {
    }
    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

struct WaitSlacMatch : public SessionStateBase {
    WaitSlacMatch(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionWaitSlacMatch) {
    }
    void enter() final;
    Result feed(SlacEvent const&) final;

private:
    Timer m_deadline{};
};

/// This session won: CM_SLAC_MATCH.CNF has been sent and cached.
struct MatchComplete : public SessionStateBase {
    MatchComplete(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionMatchComplete) {
    }
    Result feed(SlacEvent const&) final;
};

struct Failed : public SessionStateBase {
    Failed(Context& ctx, SessionData& data) : SessionStateBase(ctx, data, StateID::SessionFailed) {
    }
    Result feed(SlacEvent const&) final;
};

// The states hold a reference to m_data, so a session must not move; nor is fsm::v2::FSM movable.
class Session {
public:
    Session(Context& ctx, SessionData data);

    /// Restart this session in place, as a repeated CM_SLAC_PARM.REQ for the same identity does.
    void restart(SessionData data);

    void feed(SlacEvent const& ev);

    bool matched() const;
    bool failed() const;

    SessionData const& data() const {
        return m_data;
    }

    void signature(std::string& out) const;
    void describe(StateTree& out) const;

private:
    Context& m_ctx;
    SessionData m_data;
    std::optional<fsm::v2::FSM<StateBase>> m_fsm;
};

} // namespace everest::slac::evse::state::session
