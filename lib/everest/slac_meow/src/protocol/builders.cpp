// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <everest/slac/protocol/builders.hpp>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/utils.hpp>

namespace everest::slac::protocol {

messages::cm_set_key_req make_set_key_req(Nmk const& session_nmk) {
    messages::cm_set_key_req set_key_req{};

    set_key_req.key_type = defs::mme::set_key_req::KEY_TYPE_NMK;
    set_key_req.my_nonce = 0x00000000;
    set_key_req.your_nonce = 0x00000000;
    set_key_req.pid = defs::mme::set_key_req::PID_HLE;
    set_key_req.prn = htole16(defs::mme::set_key_req::PRN_UNUSED);
    set_key_req.pmn = defs::mme::set_key_req::PMN_UNUSED;
    set_key_req.cco_capability = defs::mme::set_key_req::CCO_CAP_NONE;
    utils::generate_nid_from_nmk(set_key_req.nid, session_nmk.data());
    set_key_req.new_eks = defs::mme::set_key_req::PEKS_NMK_KNOWN_TO_STA;
    copy_to_wire(set_key_req.new_key, session_nmk);

    return set_key_req;
}

// Note (aw): this function doesn't return by value in order to optimize for fewer copies
void make_slac_match_cnf(messages::cm_slac_match_cnf& match_cnf, messages::cm_slac_match_req const& match_req,
                         Nmk const& session_nmk) {
    match_cnf.application_type = defs::COMMON_APPLICATION_TYPE;
    match_cnf.security_type = defs::COMMON_SECURITY_TYPE;
    match_cnf.mvf_length = htole16(defs::mme::slac_match_cnf::MVF_LENGTH);
    copy_wire(match_cnf.pev_id, match_req.pev_id);
    copy_wire(match_cnf.pev_mac, match_req.pev_mac);
    copy_wire(match_cnf.evse_id, match_req.evse_id);
    copy_wire(match_cnf.evse_mac, match_req.evse_mac);
    copy_wire(match_cnf.run_id, match_req.run_id);
    zero_wire(match_cnf._rerserved);
    match_cnf._reserved2 = 0;
    utils::generate_nid_from_nmk(match_cnf.nid, session_nmk.data());
    copy_to_wire(match_cnf.nmk, session_nmk);
}

} // namespace everest::slac::protocol
