// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

// EV SLAC state machine on fsm::v2 (see fsm_helpers.hpp for the execution model).
//
//   Reset --update--> Idle
//   Reset|Idle --trigger_matching / CM_SLAC_PARM.REQ--> WaitParmCnf
//   WaitParmCnf --update/timeout / resend while attempts are left--> WaitParmCnf, then Failed
//   WaitParmCnf --message(CM_SLAC_PARM.CNF)--> Sounding
//   Sounding --update / next CM_START_ATTEN_CHAR.IND or CM_MNBC_SOUND.IND when due--> (internal)
//   Sounding --update / all sounding frames sent--> WaitAttenCharInd;  --update/timeout--> Failed
//   Sounding|WaitAttenCharInd --message(CM_ATTEN_CHAR.IND) / CM_ATTEN_CHAR.RSP + CM_SLAC_MATCH.REQ--> WaitMatchCnf
//   WaitAttenCharInd --update/timeout--> Failed
//   WaitMatchCnf --update/timeout / resend while attempts are left--> WaitMatchCnf, then Failed
//   WaitMatchCnf --message(CM_SLAC_MATCH.CNF) / CM_SET_KEY.REQ--> WaitSetKeyCnf
//   WaitSetKeyCnf --message(CM_SET_KEY.CNF)--> Matched;  --update/timeout--> Failed
//   any --reset--> Reset

#pragma once

#include "../fsm_helpers.hpp"

#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_types.hpp>

namespace everest::lib::slac::sm::ev {

enum class StateId : int {
    Reset,
    Idle,
    WaitParmCnf,
    Sounding,
    WaitAttenCharInd,
    WaitMatchCnf,
    WaitSetKeyCnf,
    Matched,
    Failed,
};

struct Tag {};
using EvState = State<Tag>;

class SlacEvMachine {
public:
    explicit SlacEvMachine(fsm::ev::Context& ctx);

    void start();
    void process(Event const& e);
    void traverse(Visitor& visitor) const;

    fsm::ev::Context& ctx;
    int start_atten_char_count{0};
    int mnbc_sound_count{0};
    int parm_req_attempt_count{0};
    int match_req_attempt_count{0};
    Nmk pending_nmk{};
    fsm::ev::SessionParameters active_session{};

private:
    Region<EvState> top;
};

} // namespace everest::lib::slac::sm::ev
