// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "virtual_plc_link.hpp"

#include <algorithm>

#include <everest/slac/protocol/defs.hpp>

namespace everest::slac::test {

const MacAddress EVSE_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
const MacAddress EV_MAC{0x02, 0x00, 0x00, 0x00, 0x00, 0x42};
const MacAddress MODEM_MAC{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

bool is_modem_directed(std::uint16_t mmtype) {
    return mmtype == defs::mmtype::SET_KEY_REQ or
           (mmtype & defs::mmtype::CATEGORY_MASK) == defs::mmtype::CATEGORY_VENDOR_SPECIFIC;
}

bool VirtualPLCLink::from_evse(messages::HomeplugMessage& frame) {
    auto stamped = frame;
    stamped.set_source(EVSE_MAC);

    if (is_modem_directed(stamped.get_mmtype())) {
        // the EVSE's vendor probes go unanswered here, so its modem vendor stays Unknown
        if (stamped.get_mmtype() == defs::mmtype::SET_KEY_REQ) {
            answer_set_key_req(m_to_evse);
        }
        return true;
    }
    m_over_the_air.push_back(stamped.get_mmtype());
    m_to_ev.push_back(stamped);
    return true;
}

bool VirtualPLCLink::from_ev(messages::HomeplugMessage& frame) {
    auto stamped = frame;
    stamped.set_source(EV_MAC);

    if (is_modem_directed(stamped.get_mmtype())) {
        if (stamped.get_mmtype() == defs::mmtype::SET_KEY_REQ) {
            m_ev_set_key_reqs.push_back(stamped);
            answer_set_key_req(m_to_ev);
        }
        return true;
    }

    m_over_the_air.push_back(stamped.get_mmtype());
    m_to_evse.push_back(stamped);

    // the EVSE's modem reports what it heard of each M-Sound
    if (stamped.get_mmtype() == defs::mmtype::MNBC_SOUND_IND) {
        inject_atten_profile();
    }
    return true;
}

void VirtualPLCLink::deliver(evse::FSM& evse, ev::FSM& ev) {
    auto to_evse = std::move(m_to_evse);
    auto to_ev = std::move(m_to_ev);
    m_to_evse.clear();
    m_to_ev.clear();

    for (auto& frame : to_evse) {
        evse.message(frame);
    }
    for (auto& frame : to_ev) {
        ev.message(frame);
    }
}

void VirtualPLCLink::answer_set_key_req(std::vector<messages::HomeplugMessage>& inbox) {
    messages::cm_set_key_cnf cnf{};
    cnf.result = defs::mme::set_key_cnf::RESULT_MODEM_COMPAT_SUCCESS;

    messages::HomeplugMessage frame;
    frame.setup_payload(&cnf, sizeof(cnf), defs::mmtype::SET_KEY_CNF, defs::MMV::AV_1_1);
    frame.set_source(MODEM_MAC);
    inbox.push_back(frame);
}

void VirtualPLCLink::inject_atten_profile() {
    messages::cm_atten_profile_ind ind{};
    std::copy(EV_MAC.begin(), EV_MAC.end(), std::begin(ind.pev_mac));
    ind.num_groups = defs::AAG_LIST_LEN;
    for (auto& group : ind.aag) {
        group = 20; // a plausible attenuation in dB; the value itself is not under test
    }

    messages::HomeplugMessage frame;
    frame.setup_payload(&ind, sizeof(ind), defs::mmtype::ATTEN_PROFILE_IND, defs::MMV::AV_1_1);
    frame.set_source(MODEM_MAC);
    m_to_evse.push_back(frame);
    ++m_injected_atten_profiles;
}

} // namespace everest::slac::test
