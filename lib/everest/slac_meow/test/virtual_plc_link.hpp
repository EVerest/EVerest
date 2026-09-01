// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <everest/slac/ev/fsm.hpp>
#include <everest/slac/evse/fsm.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/types.hpp>
#include <everest/slac/status.hpp>

namespace everest::slac::test {

extern const MacAddress EVSE_MAC;
extern const MacAddress EV_MAC;
extern const MacAddress MODEM_MAC;

/// Vendor MMEs and the set key exchange go to the station's own modem, never between stations.
bool is_modem_directed(std::uint16_t mmtype);

// The shared PLC medium and the modem attached to each station. The modem half is load bearing:
// CM_SET_KEY.REQ is answered locally and never forwarded, and CM_ATTEN_PROFILE.IND is produced by
// the EVSE's own modem from the M-Sounds it hears (V2G3-A09-43) - without it the EVSE captures no
// sounds and matching cannot complete.
class VirtualPLCLink {
public:
    /// Frames actually exchanged between the two stations, in order.
    std::vector<std::uint16_t> const& over_the_air() const {
        return m_over_the_air;
    }

    std::vector<messages::HomeplugMessage> const& ev_set_key_reqs() const {
        return m_ev_set_key_reqs;
    }

    std::size_t injected_atten_profiles() const {
        return m_injected_atten_profiles;
    }

    /// Wired to the EVSE's send_raw_slac callback.
    bool from_evse(messages::HomeplugMessage& frame);

    /// Wired to the EV's send_raw_slac callback.
    bool from_ev(messages::HomeplugMessage& frame);

    /// Hand every queued frame to its addressee. Queued rather than delivered inline, so a send
    /// during a feed() cannot re-enter the other machine.
    void deliver(evse::FSM& evse, ev::FSM& ev);

private:
    /// The modem confirms the key. Result 0x01 is what QCA parts answer and what
    /// SetKeyCnfSuccessMode::modem_compat_0x01, the default, accepts.
    void answer_set_key_req(std::vector<messages::HomeplugMessage>& inbox);

    /// One attenuation profile per M-Sound, as the EVSE's modem would report it. pev_mac and
    /// num_groups must match what SessionData::validate_message expects, or it is dropped.
    void inject_atten_profile();

    std::vector<messages::HomeplugMessage> m_to_evse, m_to_ev, m_ev_set_key_reqs;
    std::vector<std::uint16_t> m_over_the_air;
    std::size_t m_injected_atten_profiles{0};
};

/// Everything the two machines report back to their modules.
struct Recorder {
    std::vector<std::string> ev_states;
    std::vector<bool> ev_dlink, evse_dlink;
    std::vector<D3State> evse_states;
};

} // namespace everest::slac::test
