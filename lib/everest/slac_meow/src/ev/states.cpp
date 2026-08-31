// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/states.hpp>

namespace everest::slac::ev {

std::string to_string(StateID id) {
    switch (id) {
    case StateID::Reset:
        return "Reset";
    case StateID::Idle:
        return "Idle";
    case StateID::WaitParmCnf:
        return "WaitParmCnf";
    case StateID::Sounding:
        return "Sounding";
    case StateID::WaitAttenCharInd:
        return "WaitAttenCharInd";
    case StateID::WaitMatchCnf:
        return "WaitMatchCnf";
    case StateID::WaitSetKeyCnf:
        return "WaitSetKeyCnf";
    case StateID::Matched:
        return "Matched";
    case StateID::Failed:
        return "Failed";
    }
    return "Unknown";
}

} // namespace everest::slac::ev
