// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/evse/states.hpp>

namespace everest::slac::evse {

std::string to_string(StateID id) {
    switch (id) {
    case StateID::Init:
        return "Init";
    case StateID::Reset:
        return "Reset";
    case StateID::ResetChip:
        return "ResetChip";
    case StateID::Idle:
        return "Idle";
    case StateID::Matching:
        return "Matching";
    case StateID::WaitForLink:
        return "WaitForLink";
    case StateID::Matched:
        return "Matched";
    case StateID::Failed:
        return "Failed";
    case StateID::SessionWaitStartAtten:
        return "WaitStartAtten";
    case StateID::SessionSounding:
        return "Sounding";
    case StateID::SessionFinalizeSounding:
        return "FinalizeSounding";
    case StateID::SessionWaitAttenRsp:
        return "WaitAttenRsp";
    case StateID::SessionWaitSlacMatch:
        return "WaitSlacMatch";
    case StateID::SessionMatchComplete:
        return "MatchComplete";
    case StateID::SessionFailed:
        // distinct from StateID::Failed, so a failed session is not mistaken for a failed machine
        return "SessionFailed";
    }
    return "Unknown";
}

} // namespace everest::slac::evse
