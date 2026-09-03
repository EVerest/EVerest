// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

// Shared building blocks of the EVSE SLAC state machine: includes, namespace setup, the session
// flags and the vendor-specific link-status check states used by Matched and WaitForLink.
//
// Where behaviour lives:
// - on_entry / on_exit of a state hold what holds for that state regardless of how it was entered
//   or left (status bookkeeping, arming the state's timers, resetting per-state counters, and side
//   effects that are a property of being in the state, such as raising dlink_ready in Matched).
// - Row actions and guards in the transition tables hold what depends on the edge, i.e. on the
//   event and the source state (answering a message, counting a retry).
// The functors and entry helpers of each sub-machine live in guards_and_actions/<machine>_logic.hpp,
// so a _def file shows only states, aliases, the table, the hooks and its data.

#pragma once
#include <everest/slac/fsm/slac_msm_helpers.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/internal_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_utils.hpp>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/fsm/evse/context.hpp>
#include <everest/slac/timer.hpp>

#include "../../misc.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace everest::lib::slac::msm {
using namespace everest::lib::slac;
using namespace std::chrono_literals;
using namespace boost::msm::front;
using namespace boost::msm::back;

// States
// A timeout state whose duration is read from one EvseSlacConfig field on entry.
template <std::chrono::milliseconds fsm::evse::EvseSlacConfig::*Field> struct config_timeout_state : timeout_state {
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        timeout_state::duration = fsm.ctx->slac_config.*Field;
        timeout_state::on_entry(e, fsm);
    }
};

// Guards
struct is_lumissil {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Lumissil;
    }
};
struct is_qualcomm {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Qualcomm;
    }
};
struct link_status_cnf {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT&) {
        return src.link_status_cnf(e, fsm);
    }
};

struct link_status_neg {
    template <class Fsm, class Evt, class SrcT, class TarT> bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT&) {
        return src.is_link_status_neg(e, fsm);
    }
};

// Actions
struct link_status_req {
    template <class Fsm, class Evt, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& tar) {
        // Re-arm the poll timer only when we actually poll, so the link-status
        // poll cadence is not disturbed by unrelated self-transitions (see
        // CheckLink::on_entry).
        tar.to.reset(fsm.ctx->current_time);
        tar.link_status_req(fsm);
    }
};

// Flags
struct SessionFailed {};
struct SessionMatched {};

// States
struct CheckLink : public state<> {
    template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
        // Only (re)arm the poll duration here -- do NOT reset the timer's
        // reference. The poll timer is reset when a LINK_STATUS.REQ is actually
        // sent (see the link_status_req action). Otherwise every self-transition
        // that re-enters this substate for another reason (notably the Neo-only
        // CM_AMP_MAP retransmit/responder in the matched state) would restart the
        // countdown and starve the link-status poll, so a connection loss would
        // not be detected within TP_match_leave (ISO 15118-5 PLCLinkStatus_005).
        to.set_duration(fsm.link_check_to);
    }

    timer to;
    bool state_timeout(timer::tp now) const {
        return to.expired(now);
    }
};
struct Lumissil : public CheckLink {
    template <class Fsm, class Evt> bool is_link_status_message(Evt const& e, Fsm&) {
        const auto mmtype = e.payload.get_mmtype();
        return mmtype == (defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }

    template <class Fsm, class Evt> bool link_status_cnf(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status == defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm, class Evt> bool is_link_status_neg(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::lumissil::nscm_get_d_link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status != defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm> void link_status_req(Fsm& fsm) {
        messages::lumissil::nscm_get_d_link_status_req link_status_req{};
        if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req)) {
            fsm.ctx->log_warn("Failed to send CM_GET_D_LINK_STATUS.REQ to SLAC peer");
        }
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }
};
struct Qualcomm : public CheckLink {
    template <class Fsm, class Evt> bool is_link_status_message(Evt const& e, Fsm&) {
        const auto mmtype = e.payload.get_mmtype();
        return mmtype == (defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF);
    }

    template <class Fsm, class Evt> bool link_status_cnf(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::qualcomm::link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status == defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm, class Evt> bool is_link_status_neg(Evt const& e, Fsm& fsm) {
        if (not is_link_status_message(e, fsm)) {
            return false;
        }
        auto const link_status_msg = e.payload.template payload_as<messages::qualcomm::link_status_cnf>();
        return link_status_msg.has_value() && (link_status_msg->link_status != defs::D_LINK_STATUS_LINKED);
    }

    template <class Fsm> void link_status_req(Fsm& fsm) {
        messages::qualcomm::link_status_req link_status_req{};
        if (not fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req)) {
            fsm.ctx->log_warn("Failed to send LINK_STATUS.REQ to SLAC peer");
        }
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }
};

} // namespace everest::lib::slac::msm
