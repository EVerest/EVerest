// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/ev/detail/guards.hpp>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::ev {

bool is_slac_parm_cnf(messages::HomeplugMessage const& frame, RunId const& run_id) {
    if (not frame.is_valid()) {
        return false;
    }
    if (frame.get_mmtype() != defs::MMTYPE_CM_SLAC_PARAM_CNF) {
        return false;
    }
    auto const msg = frame.payload_as<messages::cm_slac_parm_cnf>();
    if (not msg.has_value()) {
        return false;
    }
    return wire_pointer_equal(msg->run_id, run_id);
}

bool is_atten_char_ind(messages::HomeplugMessage const& frame) {
    if (not frame.is_valid()) {
        return false;
    }
    if (frame.get_mmtype() != defs::MMTYPE_CM_ATTEN_CHAR_IND) {
        return false;
    }
    return frame.payload_as<messages::cm_atten_char_ind>().has_value();
}

bool is_atten_char_ind_for_run(messages::HomeplugMessage const& frame, RunId const& run_id) {
    if (not is_atten_char_ind(frame)) {
        return false;
    }
    auto const msg = frame.payload_as<messages::cm_atten_char_ind>();
    return msg.has_value() and wire_pointer_equal(msg->run_id, run_id);
}

bool is_slac_match_cnf(messages::HomeplugMessage const& frame, RunId const& run_id, MacAddress const& evse_mac) {
    if (not frame.is_valid()) {
        return false;
    }
    if (frame.get_mmtype() != defs::MMTYPE_CM_SLAC_MATCH_CNF) {
        return false;
    }
    auto const msg = frame.payload_as<messages::cm_slac_match_cnf>();
    if (not msg.has_value()) {
        return false;
    }
    if (not wire_pointer_equal(msg->run_id, run_id)) {
        return false;
    }
    auto const* source_mac = frame.get_src_mac();
    if (source_mac != nullptr and not wire_pointer_equal(source_mac, evse_mac)) {
        return false;
    }
    return true;
}

bool is_set_key_cnf(messages::HomeplugMessage const& frame) {
    if (not frame.is_valid()) {
        return false;
    }
    return frame.get_mmtype() == defs::MMTYPE_CM_SET_KEY_CNF and
           frame.payload_as<messages::cm_set_key_cnf>().has_value();
}

} // namespace everest::slac::ev
