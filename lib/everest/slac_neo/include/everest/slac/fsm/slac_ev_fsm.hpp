// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <everest/slac/fsm/slac_msm_helpers.hpp>

#include <algorithm>
#include <endian.h>
#include <random>

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/completion_event.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/internal_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>
#include <boost/msm/front/states.hpp>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/MatchingSessionData.hpp>
#include <everest/slac/fsm/ev/context.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>

namespace everest::lib::slac::msm::ev {

using namespace everest::lib::slac;
using namespace boost::msm::front;
using namespace boost::msm::back;
// The helper header already provides reset/message/update events in the enclosing
// msm namespace that are shared with EVSE.

struct trigger_matching {};
struct failed {};

// EV MSM
struct SlacEVFSM_def : state_machine_def<SlacEVFSM_def> {
    // States
    struct Reset : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            if (fsm.ctx == nullptr) {
                return;
            }
            fsm.ctx->signal_state("UNMATCHED");
            fsm.ctx->signal_dlink_ready(false);
            fsm.ctx->log_debug("EV MSM entered reset");
        }
    };

    struct Idle : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            if (fsm.ctx) {
                fsm.ctx->signal_state("UNMATCHED");
                fsm.ctx->signal_dlink_ready(false);
            }
        }
    };

    struct WaitParmCnf : public timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& event, Fsm& fsm) {
            auto timeout_ms = defs::TT_MATCH_RESPONSE_MS;
            if (fsm.ctx && fsm.ctx->slac_config.parm_req_timeout_ms > 0) {
                timeout_ms = fsm.ctx->slac_config.parm_req_timeout_ms;
            }
            timeout_state::state_timeout_ms = timeout_ms;
            timeout_state::on_entry(event, fsm);
        }
    };
    struct Sounding : public timeout_ms_state<defs::TT_EV_ATTEN_RESULTS_MS> {};
    struct WaitAttenCharInd : public timeout_ms_state<defs::TT_EV_ATTEN_RESULTS_MS> {};
    struct WaitMatchCnf : public timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& event, Fsm& fsm) {
            auto timeout_ms = defs::TT_MATCH_RESPONSE_MS;
            if (fsm.ctx && fsm.ctx->slac_config.match_req_timeout_ms > 0) {
                timeout_ms = fsm.ctx->slac_config.match_req_timeout_ms;
            }
            timeout_state::state_timeout_ms = timeout_ms;
            timeout_state::on_entry(event, fsm);
        }
    };
    struct WaitSetKeyCnf : public timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& event, Fsm& fsm) {
            auto timeout_ms = defs::TT_MATCH_JOIN_MS;
            if (fsm.ctx && fsm.ctx->slac_config.set_key_timeout_ms > 0) {
                timeout_ms = fsm.ctx->slac_config.set_key_timeout_ms;
            }
            timeout_state::state_timeout_ms = timeout_ms;
            timeout_state::on_entry(event, fsm);
        }
    };

    struct Matched : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            if (fsm.ctx) {
                fsm.ctx->signal_state("MATCHED");
                fsm.ctx->signal_dlink_ready(true);
                fsm.ctx->log_info("EV MSM entered matched");
            }
        }
    };
    struct Failed : public state<> {
        template <class Event, class Fsm> void on_entry(Event const&, Fsm& fsm) {
            if (fsm.ctx) {
                fsm.ctx->signal_state("UNMATCHED");
                fsm.ctx->signal_dlink_ready(false);
                fsm.ctx->log_warn("EV MSM entered failed");
            }
        }
    };

    struct reset_matching {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.ctx) {
                fsm.parm_req_attempt_count = 0;
                fsm.match_req_attempt_count = 0;
                fsm.active_session = {};
                fsm.start_atten_char_count = 0;
                fsm.mnbc_sound_count = 0;
                fsm.pending_nmk.fill(0);
            }
        }
    };

    struct send_slac_parm_req {
        template <class Fsm> static void send(Fsm& fsm) {
            messages::cm_slac_parm_req req{};
            req.application_type = defs::COMMON_APPLICATION_TYPE;
            req.security_type = defs::COMMON_SECURITY_TYPE;
            copy_to_wire(req.run_id, fsm.active_session.run_id);

            if (not fsm.ctx->send_slac_message(fsm::ev::Context::BROADCAST_MAC, req)) {
                fsm.ctx->log_warn("Failed to send CM_SLAC_PARM.REQ");
            }
            fsm.parm_req_attempt_count++;
        }

        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.ctx) {
                send(fsm);
            }
        }
    };
    struct send_slac_match_req {
        template <class Fsm> static void send(Fsm& fsm) {
            messages::cm_slac_match_req match_req{};
            match_req.application_type = defs::COMMON_APPLICATION_TYPE;
            match_req.security_type = defs::COMMON_SECURITY_TYPE;
            match_req.mvf_length = htole16(defs::CM_SLAC_MATCH_REQ_MVF_LENGTH);
            std::fill(std::begin(match_req.pev_id), std::end(match_req.pev_id), std::uint8_t{0});
            copy_to_wire(match_req.pev_mac, fsm.ctx->ev_host_mac);
            std::fill(std::begin(match_req.evse_id), std::end(match_req.evse_id), std::uint8_t{0});
            copy_to_wire(match_req.evse_mac, fsm.active_session.evse_mac);
            copy_to_wire(match_req.run_id, fsm.active_session.run_id);
            std::fill(std::begin(match_req._reserved), std::end(match_req._reserved), std::uint8_t{0});

            if (not fsm.ctx->send_slac_message(fsm.active_session.evse_mac, match_req)) {
                fsm.ctx->log_warn("Failed to send CM_SLAC_MATCH.REQ");
            }
            fsm.match_req_attempt_count++;
        }

        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.ctx) {
                send(fsm);
            }
        }
    };

    struct start_matching {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (fsm.ctx) {
                fsm.parm_req_attempt_count = 0;
                fsm.match_req_attempt_count = 0;
                fsm.start_atten_char_count = 0;
                fsm.mnbc_sound_count = 0;
                fsm.pending_nmk.fill(0);

                std::random_device rnd_dev;
                std::mt19937 rng(rnd_dev());
                std::uniform_int_distribution<int> byte_distribution(0, 0xFF);
                fsm.active_session = fsm::ev::SessionParameters{};
                for (auto& octet : fsm.active_session.run_id) {
                    octet = static_cast<std::uint8_t>(byte_distribution(rng));
                }
                send_slac_parm_req::send(fsm);
                fsm.ctx->log_info("EV MSM start matching");
            }
        }
    };
    struct capture_slac_parm_cnf {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return;
            }
            const auto msg = e.payload.template payload_as<messages::cm_slac_parm_cnf>();
            if (not msg.has_value()) {
                fsm.ctx->log_warn("Received CM_SLAC_PARM.CNF with invalid payload");
                return;
            }
            auto const src_mac = e.payload.get_src_mac();
            if (src_mac == nullptr) {
                fsm.ctx->log_warn("Received CM_SLAC_PARM.CNF without source MAC");
                return;
            }
            std::copy_n(src_mac, fsm.active_session.evse_mac.size(), fsm.active_session.evse_mac.begin());
            fsm.ctx->signal_state("MATCHING");
        }
    };

    struct sounding_messages {
        template <class Fsm> static void send_next_sounding(Fsm& fsm) {
            if (not fsm.ctx) {
                return;
            }
            if (fsm.start_atten_char_count < defs::C_EV_START_ATTEN_CHAR_INDS) {
                messages::cm_start_atten_char_ind msg{};
                msg.application_type = defs::COMMON_APPLICATION_TYPE;
                msg.security_type = defs::COMMON_SECURITY_TYPE;
                msg.num_sounds = defs::C_EV_MATCH_MNBC;
                msg.timeout = static_cast<std::uint8_t>((defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100);
                msg.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
                copy_to_wire(msg.forwarding_sta, fsm.ctx->ev_host_mac);
                copy_to_wire(msg.run_id, fsm.active_session.run_id);
                fsm.start_atten_char_count++;
                if (not fsm.ctx->send_slac_message(fsm::ev::Context::BROADCAST_MAC, msg)) {
                    fsm.ctx->log_warn("Failed to send CM_START_ATTEN_CHAR.IND");
                }
                return;
            }

            if (fsm.mnbc_sound_count < defs::C_EV_MATCH_MNBC) {
                messages::cm_mnbc_sound_ind msg{};
                msg.application_type = defs::COMMON_APPLICATION_TYPE;
                msg.security_type = defs::COMMON_SECURITY_TYPE;
                std::fill(std::begin(msg.sender_id), std::end(msg.sender_id), std::uint8_t{0});
                ++fsm.mnbc_sound_count;
                msg.remaining_sound_count = defs::C_EV_MATCH_MNBC - fsm.mnbc_sound_count;
                copy_to_wire(msg.run_id, fsm.active_session.run_id);
                std::fill(std::begin(msg._reserved), std::end(msg._reserved), std::uint8_t{0});

                std::random_device rnd_dev;
                std::mt19937 rng(rnd_dev());
                std::uniform_int_distribution<int> byte_distribution(0, 0xFF);
                for (auto& octet : msg.random) {
                    octet = static_cast<std::uint8_t>(byte_distribution(rng));
                }

                if (not fsm.ctx->send_slac_message(fsm::ev::Context::BROADCAST_MAC, msg)) {
                    fsm.ctx->log_warn("Failed to send CM_MNBC_SOUND.IND");
                }
            }
        }
    };

    struct should_continue_sounding {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT&) {
            return not src.state_timeout() && (fsm.start_atten_char_count < defs::C_EV_START_ATTEN_CHAR_INDS ||
                                               fsm.mnbc_sound_count < defs::C_EV_MATCH_MNBC);
        }
    };

    struct are_all_sounding_messages_sent {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            return fsm.start_atten_char_count >= defs::C_EV_START_ATTEN_CHAR_INDS &&
                   fsm.mnbc_sound_count >= defs::C_EV_MATCH_MNBC;
        }
    };

    struct has_parm_req_attempts_left {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return false;
            }
            return fsm.parm_req_attempt_count < fsm.ctx->slac_config.parm_req_attempts;
        }
    };
    struct no_parm_req_attempts_left {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return true;
            }
            return fsm.parm_req_attempt_count >= fsm.ctx->slac_config.parm_req_attempts;
        }
    };
    struct has_match_req_attempts_left {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return false;
            }
            return fsm.match_req_attempt_count < fsm.ctx->slac_config.match_req_attempts;
        }
    };
    struct no_match_req_attempts_left {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return true;
            }
            return fsm.match_req_attempt_count >= fsm.ctx->slac_config.match_req_attempts;
        }
    };

    struct send_next_sounding {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
            sounding_messages::send_next_sounding(fsm);
        }
    };

    struct send_atten_char_rsp_and_match_req {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            if (not fsm.ctx) {
                return;
            }
            const auto atten_char_ind = e.payload.template payload_as<messages::cm_atten_char_ind>();
            if (not atten_char_ind.has_value()) {
                fsm.ctx->log_warn("Received CM_ATTEN_CHAR.IND with invalid payload");
                return;
            }

            messages::cm_atten_char_rsp rsp{};
            rsp.application_type = defs::COMMON_APPLICATION_TYPE;
            rsp.security_type = defs::COMMON_SECURITY_TYPE;
            copy_to_wire(rsp.source_address, fsm.ctx->ev_host_mac);
            copy_to_wire(rsp.run_id, fsm.active_session.run_id);
            std::fill(std::begin(rsp.source_id), std::end(rsp.source_id), std::uint8_t{0});
            std::fill(std::begin(rsp.resp_id), std::end(rsp.resp_id), std::uint8_t{0});
            rsp.result = defs::CM_ATTEN_CHAR_RSP_RESULT;
            if (not fsm.ctx->send_slac_message(fsm.active_session.evse_mac, rsp)) {
                fsm.ctx->log_warn("Failed to send CM_ATTEN_CHAR.RSP");
            }

            send_slac_match_req::send(fsm);
        }
    };

    struct store_match_cnf_and_send_set_key {
        template <class Evt, class Fsm, class SrcT, class TarT> void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            const auto match_cnf = e.payload.template payload_as<messages::cm_slac_match_cnf>();
            if (not match_cnf.has_value()) {
                if (fsm.ctx) {
                    fsm.ctx->log_warn("Received CM_SLAC_MATCH.CNF with invalid payload");
                }
                return;
            }
            if (fsm.ctx) {
                std::copy_n(std::begin(match_cnf->nmk), defs::NMK_LEN, fsm.pending_nmk.begin());
                if (not fsm.ctx->send_slac_message(
                        fsm::ev::Context::EV_PLC_MAC,
                        fsm::evse::MatchingSessionData::create_cm_set_key_req(fsm.pending_nmk))) {
                    fsm.ctx->log_warn("Failed to send CM_SET_KEY.REQ");
                }
            }
        }
    };

    bool is_set_key_cnf(message const& e) {
        if (not e.payload.is_valid()) {
            return false;
        }
        return e.payload.get_mmtype() == (defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF) and
               e.payload.template payload_as<messages::cm_set_key_cnf>().has_value();
    }
    struct guard_is_slac_parm_cnf {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            if (not e.payload.is_valid()) {
                return false;
            }
            const auto msg = e.payload.template payload_as<messages::cm_slac_parm_cnf>();
            if (not msg.has_value()) {
                return false;
            }
            return e.payload.get_mmtype() == (defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF) and
                   wire_pointer_equal(msg->run_id, fsm.active_session.run_id);
        }
    };
    struct guard_is_atten_char_ind {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const& e, Fsm&, SrcT&, TarT&) {
            if (not e.payload.is_valid()) {
                return false;
            }
            const auto msg = e.payload.template payload_as<messages::cm_atten_char_ind>();
            return e.payload.get_mmtype() == (defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND) and msg.has_value();
        }
    };
    struct guard_is_atten_char_ind_after_sounding {
        template <class Evt, class Fsm, class SrcT, class TarT>
        bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT& tar) {
            if (not guard_is_atten_char_ind{}(e, fsm, src, tar)) {
                return false;
            }
            const auto msg = e.payload.template payload_as<messages::cm_atten_char_ind>();
            if (not msg.has_value()) {
                return false;
            }
            return fsm.start_atten_char_count >= defs::C_EV_START_ATTEN_CHAR_INDS &&
                   fsm.mnbc_sound_count >= defs::C_EV_MATCH_MNBC &&
                   wire_pointer_equal(msg->run_id, fsm.active_session.run_id);
        }
    };
    struct guard_is_slac_match_cnf {
        template <class Evt, class Fsm, class SrcT, class TarT> bool operator()(Evt const& e, Fsm& fsm, SrcT&, TarT&) {
            if (not e.payload.is_valid()) {
                return false;
            }
            auto const msg = e.payload.template payload_as<messages::cm_slac_match_cnf>();
            if (not msg.has_value()) {
                return false;
            }
            if (e.payload.get_mmtype() != (defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_CNF)) {
                return false;
            }
            if (not wire_pointer_equal(msg->run_id, fsm.active_session.run_id)) {
                return false;
            }
            auto const source_mac = e.payload.get_src_mac();
            if (source_mac != nullptr && not wire_pointer_equal(source_mac, fsm.active_session.evse_mac)) {
                return false;
            }
            return true;
        }
    };

    // Transitions
    using initial_state = Reset;
    using p = SlacEVFSM_def;
    using parm_retry = And_<timeout, has_parm_req_attempts_left>;
    using parm_give_up = And_<timeout, no_parm_req_attempts_left>;
    using match_retry = And_<timeout, has_match_req_attempts_left>;
    using match_give_up = And_<timeout, no_match_req_attempts_left>;
    using sounding_done = are_all_sounding_messages_sent;
    using attenuation_after_sounding = guard_is_atten_char_ind_after_sounding;
    using atten_rsp_match_req = send_atten_char_rsp_and_match_req;
    using match_cnf_set_key = store_match_cnf_and_send_set_key;

    // clang-format off
    struct transition_table : boost::mpl::vector<
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        //  | Source           | Event            | Target           | Action                | Guard                    |
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < Reset            , update           , Idle             , none                  , none                     >,
        Row < Reset            , trigger_matching , WaitParmCnf      , start_matching        , none                     >,
        Row < Reset            , reset            , Reset            , none                  , none                     >,
        Row < Reset            , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < Idle             , trigger_matching , WaitParmCnf      , start_matching        , none                     >,
        Row < Idle             , reset            , Reset            , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < WaitParmCnf      , update           , WaitParmCnf      , send_slac_parm_req    , parm_retry               >,
        Row < WaitParmCnf      , update           , Failed           , none                  , parm_give_up             >,
        Row < WaitParmCnf      , message          , Sounding         , capture_slac_parm_cnf , guard_is_slac_parm_cnf   >,
        Row < WaitParmCnf      , reset            , Reset            , reset_matching        , none                     >,
        Row < WaitParmCnf      , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < Sounding         , update           , none             , send_next_sounding    , should_continue_sounding >,
        Row < Sounding         , update           , WaitAttenCharInd , none                  , sounding_done            >,
        Row < Sounding         , update           , Failed           , none                  , timeout                  >,
        Row < Sounding         , message          , WaitMatchCnf     , atten_rsp_match_req   , attenuation_after_sounding>,
        Row < Sounding         , reset            , Reset            , reset_matching        , none                     >,
        Row < Sounding         , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < WaitAttenCharInd , update           , Failed           , none                  , timeout                  >,
        Row < WaitAttenCharInd , message          , WaitMatchCnf     , atten_rsp_match_req   , attenuation_after_sounding>,
        Row < WaitAttenCharInd , reset            , Reset            , reset_matching        , none                     >,
        Row < WaitAttenCharInd , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < WaitMatchCnf     , update           , WaitMatchCnf     , send_slac_match_req   , match_retry              >,
        Row < WaitMatchCnf     , update           , Failed           , none                  , match_give_up            >,
        Row < WaitMatchCnf     , message          , WaitSetKeyCnf    , match_cnf_set_key     , guard_is_slac_match_cnf  >,
        Row < WaitMatchCnf     , reset            , Reset            , reset_matching        , none                     >,
        Row < WaitMatchCnf     , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < WaitSetKeyCnf    , update           , Failed           , none                  , timeout                  >,
        g_row< WaitSetKeyCnf    , message          , Matched          /* none */             , &p::is_set_key_cnf       >,
        Row < WaitSetKeyCnf    , reset            , Reset            , reset_matching        , none                     >,
        Row < WaitSetKeyCnf    , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < Matched          , reset            , Reset            , reset_matching        , none                     >,
        Row < Matched          , failed           , Failed           , none                  , none                     >,
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        Row < Failed           , reset            , Reset            , reset_matching        , none                     >
        //  +------------------+------------------+------------------+-----------------------+--------------------------+
        > {};
    // clang-format on

    template <class FSM, class Event> void no_transition(Event const&, FSM&, int) {
    }

    fsm::ev::Context* ctx;
    int start_atten_char_count{0};
    int mnbc_sound_count{0};
    int parm_req_attempt_count{0};
    int match_req_attempt_count{0};
    Nmk pending_nmk{};
    fsm::ev::SessionParameters active_session{};

    SlacEVFSM_def(fsm::ev::Context& ctx_) : ctx(&ctx_) {
    }
};

using SlacEVFSM = state_machine<SlacEVFSM_def>;

} // namespace everest::lib::slac::msm::ev
