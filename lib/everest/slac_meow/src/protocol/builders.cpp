// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/protocol/builders.hpp>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/utils.hpp>

namespace everest::slac::protocol {

messages::cm_set_key_req make_set_key_req(Nmk const& session_nmk) {
    messages::cm_set_key_req set_key_req{};

    set_key_req.key_type = defs::CM_SET_KEY_REQ_KEY_TYPE_NMK;
    set_key_req.my_nonce = 0x00000000;
    set_key_req.your_nonce = 0x00000000;
    set_key_req.pid = defs::CM_SET_KEY_REQ_PID_HLE;
    set_key_req.prn = htole16(defs::CM_SET_KEY_REQ_PRN_UNUSED);
    set_key_req.pmn = defs::CM_SET_KEY_REQ_PMN_UNUSED;
    set_key_req.cco_capability = defs::CM_SET_KEY_REQ_CCO_CAP_NONE;
    utils::generate_nid_from_nmk(set_key_req.nid, session_nmk.data());
    set_key_req.new_eks = defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA;
    copy_to_wire(set_key_req.new_key, session_nmk);

    return set_key_req;
}

} // namespace everest::slac::protocol
