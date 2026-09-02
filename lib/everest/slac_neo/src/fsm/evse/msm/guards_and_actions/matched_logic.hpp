// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Guards and actions of the Matched sub-machine (link supervision and CM_AMP_MAP; see matched.hpp).

#pragma once
#include "../common.hpp"

namespace everest::lib::slac::msm::matched_sm {

// Guards
struct detect_link {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->slac_config.link_status.do_detect;
    }
};

// A valid CM_AMP_MAP.REQ (am_len != 0) received while matched. am_len == 0 is
// invalid and must be left unanswered (ISO 15118-5 CmAmpMap_005).
struct is_amp_map_req {
    template <class Fsm, class SrcT, class TarT> bool operator()(message const& e, Fsm&, SrcT&, TarT&) {
        if (e.payload.get_mmtype() != (defs::MMTYPE_CM_AMP_MAP | defs::MMTYPE_MODE_REQ)) {
            return false;
        }
        auto const req = e.payload.payload_as<messages::cm_amp_map_req>();
        return req.has_value() and req->am_len != 0;
    }
};

// Answer a valid CM_AMP_MAP.REQ with CM_AMP_MAP.CNF(result=0x00). On real HW
// the requested transmit-power reduction is applied to the modem; that RF
// effect is out of scope for the SLAC stack.
struct send_amp_map_cnf {
    template <class Fsm, class SrcT, class TarT> void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        messages::cm_amp_map_cnf reply{};
        reply.result = defs::CM_AMP_MAP_CNF_RESULT_SUCCESS;
        if (not fsm.ctx->send_slac_message(e.payload.get_src_mac(), reply)) {
            fsm.ctx->log_warn("Failed to send CM_AMP_MAP.CNF");
        }
    }
};

// SECC-initiated CM_AMP_MAP exchange (ISO 15118-3 A.9.6); the logic lives in AmpMapHandler, owned by
// Matched_def as `amp_map`. These only wire it to the update tick and to the incoming CNF.
struct amp_map_retransmit_due {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.amp_map.retransmit_due();
    }
};
struct retransmit_amp_map {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.amp_map.retransmit(*fsm.ctx);
    }
};
struct is_amp_map_cnf_ok {
    template <class Fsm, class SrcT, class TarT> bool operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
        return fsm.amp_map.is_awaited_cnf(e.payload);
    }
};
struct amp_map_cnf_ack {
    template <class Fsm, class SrcT, class TarT> void operator()(message const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.amp_map.acknowledge_cnf();
    }
};

struct neg_threshold_reached {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.consecutive_neg_link_status + 1 >= fsm.neg_link_status_threshold;
    }
};
struct count_link_status_neg {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        fsm.consecutive_neg_link_status++;
        std::ostringstream ss;
        ss << "Negative LINK_STATUS while matched (" << fsm.consecutive_neg_link_status << "/"
           << fsm.neg_link_status_threshold << "), keeping the link up while debouncing";
        fsm.ctx->log_warn(ss.str());
    }
};
struct clear_link_status_neg {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        if (fsm.consecutive_neg_link_status != 0) {
            fsm.consecutive_neg_link_status = 0;
            fsm.ctx->log_info("Positive LINK_STATUS, link recovered; debounce count cleared");
        }
    }
};

} // namespace everest::lib::slac::msm::matched_sm
