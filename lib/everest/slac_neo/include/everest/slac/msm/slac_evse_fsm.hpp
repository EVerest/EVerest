// SPDX-License-Identifier: Apache-2.0
// Copyright 2022 - 2026 Pionix GmbH and Contributors to EVerest

#pragma once
#define BOOST_MSM_DEBUG_SIGMASK
#define BOOST_MPL_CFG_NO_PREPROCESSED_HEADERS
#define BOOST_MPL_LIMIT_VECTOR_SIZE 30
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

#include <chrono>
#include <iostream>
#include <sstream>

namespace everest::lib::slac::msm {
using namespace everest::lib::slac;
using namespace std::chrono_literals;
using namespace boost::msm::front;
using namespace boost::msm::back;

template <class Guard> struct Not_ {
    template <class Evt, class Fsm, class SourceState, class TargetState>
    bool operator()(Evt const& evt, Fsm& fsm, SourceState& src, TargetState& tgt) {
        return !Guard()(evt, fsm, src, tgt);
    }
};

template <class G1, class G2> struct And_ {
    template <class Evt, class Fsm, class SourceState, class TargetState>
    bool operator()(Evt const& evt, Fsm& fsm, SourceState& src, TargetState& tgt) {
        return G1()(evt, fsm, src, tgt) && G2()(evt, fsm, src, tgt);
    }
};

// clang-format off

// Events
struct message{
    messages::HomeplugMessage payload; //TODO: proper type
};
struct reset{};
struct enter_bcd{};
struct leave_bcd{};
struct update{};

// Guards
struct is_link_detection_on
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& )
    {
        return true;
    }
};
struct is_lumissil
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Lumissil;
    }
};
struct is_qualcomm
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
        return fsm.ctx->modem_vendor == defs::ModemVendor::Qualcomm;
    }
};
struct link_status_cnf
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT&) {
        return src.link_status_cnf(e, fsm);
    }
};

struct timeout
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm&, SrcT& src, TarT& )
    {
        return src.state_timeout();
    }
};

struct failed
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& )
    {
        return src.state_failed();
    }
};

template<std::uint16_t MessageType>
struct is_message_of_type
{
    template <class Fsm, class SrcT, class TarT>
    bool operator()(message const& e, Fsm& fsm, SrcT&, TarT& ) {
        const auto mmtype = e.payload.get_mmtype();
        return mmtype == MessageType;
    }
};


// Actions
struct trigger_update
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
        fsm.process_event(update{});
    };
};
struct link_status_req
{
    template <class Fsm, class Evt, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& tar) {
        tar.link_status_req(fsm);
    }
};
template <class MsgT>
struct send_default_msg {
    template <class Evt, class Fsm, class SrcT, class TarT>
    void operator()(Evt const&, Fsm& fsm, SrcT&, TarT&) {
        MsgT msg;
        fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, msg);
    }
};

// Flags
struct SessionFailed{};
struct SessionMatched{};

// States
template <std::uint32_t TimeoutMS> struct timeout_ms_state : public state<> {
    template <class Event, class Fsm> void on_entry(Event const&, Fsm&) {
        to.setDurationMilliSeconds(TimeoutMS);
        to.reset();
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};
struct timeout_state : public state<> {
    template <class Event, class Fsm> void on_entry(Event const&, Fsm&) {
        to.setDurationMilliSeconds(state_timeout_ms);
        to.reset();
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }

    std::uint32_t state_timeout_ms{0};
};
struct CheckLink     : public state<> {
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        to.setDurationMilliSeconds(fsm.link_check_to_ms);
        to.reset();
    }

    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};
struct Lumissil      : public CheckLink {
    template <class Fsm, class Evt>
    bool link_status_cnf(Evt const& e, Fsm& fsm) {
        const auto mmtype = e.payload.get_mmtype();
        auto expected = defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF;
        auto link_status = e.payload.template get_payload<messages::lumissil::nscm_get_d_link_status_cnf>().link_status;
        return (expected == mmtype) and (link_status == 0x01);
    }

    template <class Fsm>
    void link_status_req(Fsm& fsm) {
        messages::lumissil::nscm_get_d_link_status_req link_status_req;
        fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req);
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }

};
struct Qualcomm      : public CheckLink {
    template <class Fsm, class Evt>
    bool link_status_cnf(Evt const& e, Fsm& fsm) {
        const auto mmtype = e.payload.get_mmtype();
        auto expected = defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF;
        auto link_status = e.payload.template get_payload<messages::qualcomm::link_status_cnf>().link_status;
        return (expected == mmtype) and (link_status == 0x01);
    }

    template <class Fsm>
    void link_status_req(Fsm& fsm) {
        messages::qualcomm::link_status_req link_status_req;
        fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, link_status_req);
    }
    template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
        CheckLink::on_entry(e, fsm);
    }
};


// SubState StateMachines
struct Session_def     : public state_machine_def<Session_def> {
    // States
    static constexpr auto FINALIZE_SOUNDING_DELAY_MS = 45;
    struct WaitStartAtten   : public timeout_ms_state<defs::TT_MATCH_SEQUENCE_MS> { };
    struct Sounding         : public timeout_ms_state<defs::TT_EVSE_MATCH_MNBC_MS> {
        struct update_session {
            template <class Fsm, class SrcT, class TarT>
            void operator()(message const& e, Fsm& fsm, SrcT& src, TarT& ) {
                for (int i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
                    auto msg = e.payload.get_payload<slac::messages::cm_atten_profile_ind>();
                    fsm.session_data.captured_aags[i] += msg.aag[i];
                }
                fsm.session_data.captured_sounds++;
            }
        };
        struct is_atten_profile_ind {
            template <class Fsm, class SrcT, class TarT>
            bool operator()(message const& e, Fsm& fsm, SrcT& src, TarT& ) {
                auto mmtype = slac::defs::MMTYPE_CM_ATTEN_PROFILE | slac::defs::MMTYPE_MODE_IND;
                return check_message<slac::messages::cm_atten_profile_ind>(e, mmtype, fsm.session_data);
            }
        };

        struct internal_transition_table : boost::mpl::vector<
            //         Event              / Action             [Guard]
            //        +------------------+--------------------+-----------------------+-
            Internal  < message          , update_session     , is_atten_profile_ind >
            //        +------------------+--------------------+-----------------------+-
            > {};
    };
    struct FinalizeSounding : public timeout_ms_state<FINALIZE_SOUNDING_DELAY_MS> { };
    struct WaitAttenRsp     : public timeout_ms_state<defs::TT_MATCH_RESPONSE_MS> { };
    struct WaitSlacMatch    : public timeout_ms_state<defs::TT_EVSE_MATCH_SESSION_MS> {  };
    struct MatchComplete    : public state<> {
        typedef boost::mpl::vector<SessionMatched> flag_list;
    };
    struct Failed           : public state<> {
        typedef boost::mpl::vector<SessionFailed> flag_list;
    };

    // Guards
    template<class MsgT>
    static bool check_message(message const& e, std::uint16_t expected, fsm::evse::MatchingSessionData const& session_data) {
        const auto mmtype = e.payload.get_mmtype();
        if(mmtype not_eq expected){
            return false;
        }
        return session_data.validate_message(e.payload.get_payload<MsgT>());
    }
    bool is_start_atten_char(message const& e) {
        auto mmtype = defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND;
        return check_message<slac::messages::cm_start_atten_char_ind>(e, mmtype, session_data);
    }
    bool is_atten_char_rsp(message const& e) {
        auto mmtype = slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP;
        return check_message<slac::messages::cm_atten_char_rsp>(e, mmtype, session_data);
    }
    bool is_slac_match_req(message const& e) {
        auto mmtype = slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ;
        return check_message<slac::messages::cm_slac_match_req>(e, mmtype, session_data);
    }
    bool enough_sounds(message const& e) {
        return session_data.captured_sounds >= slac::defs::CM_SLAC_PARM_CNF_NUM_SOUNDS;
    }
    struct retry_limit {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& ) {
            return fsm.session_data.num_retries > slac::defs::C_EV_MATCH_RETRY;
        }
    };

    // Actions
    struct finalize_snd {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& ) {
            // action
            auto atten_char = fsm.session_data.create_cm_atten_char_ind(fsm.ctx->slac_config.sounding_atten_adjustment);
            fsm.ctx->send_slac_message(fsm.session_data.ev_mac, atten_char);
            // logging
            // FIXME (jh) Still need to add all logging
            int aag_overall_sum = 0;
            for (size_t i = 0; i < slac::defs::AAG_LIST_LEN; ++i) {
                aag_overall_sum += atten_char.attenuation_profile.aag[i];
            }
            fsm.ctx->status.average_attenuation = aag_overall_sum / slac::defs::AAG_LIST_LEN;
        }
    };
    struct retry_snd {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT& src, TarT& tar) {
            finalize_snd obj;
            obj(e, fsm, src, tar);
            fsm.session_data.num_retries++;
        }
    };
    void match_cnf(message const& e){
        messages::cm_slac_match_cnf& reply = ctx->match_confirm_message;
        auto msg = e.payload.get_payload<slac::messages::cm_slac_match_req>();
        session_data.create_cm_slac_match_cnf(reply, msg, ctx->slac_config.session_nmk);
        ctx->send_slac_message(session_data.ev_mac, reply);
        ctx->signal_cm_slac_match_cnf(session_data.ev_mac);
        std::copy(std::begin(session_data.ev_mac), std::end(session_data.ev_mac), std::begin(ctx->status.ev_mac));
    }

    // Transitions
    using initial_state = WaitStartAtten;
    using p = Session_def;
    struct transition_table : boost::mpl::vector<
        //     Source            + Event             -> Target           / Action            [Guard] last transition wins
        //    +------------------+--------------------+------------------+------------------+---------------------------+-
        Row   < WaitStartAtten   , update             , Failed           , none             , timeout                   >,
        g_row < WaitStartAtten   , message            , Sounding         /* none */         , &p::is_start_atten_char   >,
        Row   < Sounding         , update             , FinalizeSounding , none             , timeout                   >,
        g_row < Sounding         , message            , FinalizeSounding /* none */         , &p::enough_sounds         >,
        Row   < FinalizeSounding , update             , WaitAttenRsp     , finalize_snd     , timeout                   >,
        Row   < WaitAttenRsp     , update             , WaitAttenRsp     , retry_snd        , timeout                   >,
        Row   < WaitAttenRsp     , update             , Failed           , none             , And_<timeout, retry_limit>>,
        g_row < WaitAttenRsp     , message            , WaitSlacMatch    /* none */         , &p::is_atten_char_rsp     >,
        Row   < WaitSlacMatch    , update             , Failed           , none             , timeout                   >,
        row   < WaitSlacMatch    , message            , MatchComplete    , &p::match_cnf    , &p::is_slac_match_req     >
        //    +------------------+--------------------+------------------+------------------+---------------------------+-
        >{};
    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        //ctx = fsm.ctx; <- does not work here, since there is no parent FSM
        session_data.num_retries = 0;
    }

    fsm::evse::MatchingSessionData session_data;
    fsm::evse::Context* ctx;
};
struct Matching_def    : public state_machine_def<Matching_def> {
    // States
    struct Init    : public state<> { };
    struct Listen  : public state<> { };
    struct Pipe    : public state<> { };
    struct Matched : public exit_pseudo_state<update> { };
    struct Failed  : public exit_pseudo_state<update> { };

    // Guards
    bool is_matched(update const&) {
        for(auto& elem : sessions){
            if(elem.is_flag_active<SessionMatched>()){
                return true;
            }
        }
        return false;
    }
    bool is_failed(update const&) {
        if(sessions.empty()){
            return false;
        }
        for(auto& elem : sessions){
            if(not elem.is_flag_active<SessionFailed>()){
                return false;
            }
        }
        return true;
    }
    struct is_slac_param_req : public is_message_of_type<slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ> { };

    //Actions
    struct pipe_event {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT& ) {
            for(auto& elem : fsm.sessions){
                elem.process_event(e);
            }
        }
    };

    struct add_session {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT& src, TarT& ) {
            // Add session
            auto& ctx = *fsm.ctx;
            auto& msg = e.payload.get_payload<slac::messages::cm_slac_parm_req>();
            fsm::evse::MatchingSessionData data(e.payload.get_src_mac(), msg.run_id, ctx.evse_mac);
            auto& session = fsm.sessions.emplace_back();
            session.session_data = data;
            session.ctx = fsm.ctx;
            session.start();
            // send reply
            auto param_confirm = data.create_cm_slac_parm_cnf();
            ctx.send_slac_message(param_confirm.forwarding_sta, param_confirm);
            ctx.signal_cm_slac_parm_req(data.ev_mac);
            ctx.status.session_count = fsm.sessions.size();
        }
    };

    // Transitions
    using Session = state_machine<Session_def>;
    using initial_state = boost::mpl::vector<Init, Listen, Pipe>;
    using p = Matching_def;
    struct transition_table : boost::mpl::vector<
        //     Source         + Event             -> Target           / Action            [Guard]
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        g_row < Init          , update             , Matched          /* none */         , &p::is_matched           >,
        g_row < Init          , update             , Failed           /* none */         , &p::is_failed            >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Listen        , message            , Listen           , add_session      , is_slac_param_req        >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Pipe          , message            , none             , pipe_event       , none                     >,
        Row   < Pipe          , update             , none             , pipe_event       , none                     >
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        >{};

    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) {
    }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        to.setDurationMilliSeconds(ctx->slac_config.slac_init_timeout_ms);
        to.reset();
        ctx->status.match_state = SlacState::Matching;
        ctx->status.d3_state = D3State::Matching;
    }

    template <class Event, class Fsm>
    void on_exit(Event const&, Fsm& fsm) {
        sessions.clear();
        ctx->status.session_count = 0;
    }

    std::vector<Session> sessions;
    fsm::evse::Context* ctx;
    timer to;

    bool state_timeout() {
        return to.timeout();
    }
};
struct Reset_def       : public state_machine_def<Reset_def> {
    // States
    struct Init      : public state<>{ };
    struct MsgSent   : public state<>{ };
    struct MsgValid  : public state<>{ };
    struct ResetChip : public exit_pseudo_state<update>{ };
    struct Idle      : public exit_pseudo_state<update>{ };

    // Guards
    struct msg_expected : public is_message_of_type<defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_CNF>{ };
    struct is_reset_chip_on {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.chip_reset.enabled;
        };
    };
    //Actions
    struct send_set_key_req {
        template <class Fsm, class SrcT, class TarT>
        void operator()(none const& e, Fsm& fsm, SrcT&, TarT&) {
            auto msg = everest::lib::slac::fsm::evse::MatchingSessionData::create_cm_set_key_req(fsm.ctx->slac_config.session_nmk);
            fsm.ctx->send_slac_message(fsm.ctx->slac_config.plc_peer_mac, msg);
        }
    };


    // Transitions
    using initial_state = Init;
    struct transition_table : boost::mpl::vector<
        //   Source         + Event             -> Target           / Action            [Guard]
        //  +---------------+--------------------+------------------+------------------+--------------------------+
        Row < Init          , none               , MsgSent          , send_set_key_req , none                    >,
        Row < MsgSent       , message            , MsgValid         , trigger_update   , msg_expected            >,
        Row < MsgValid      , update             , ResetChip        , none             , is_reset_chip_on        >,
        Row < MsgValid      , update             , Idle             , none             , Not_<is_reset_chip_on>  >
        //  +---------------+--------------------+------------------+------------------+--------------------------+
        > {};
    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        fsm.ctx->slac_config.generate_nmk();
        to.setDurationMilliSeconds(fsm.ctx->slac_config.set_key_timeout_ms);
        to.reset();
        ctx->status.match_state = SlacState::Reset;
        ctx->status.d3_state = D3State::Unmatched;
        ctx->status.modem_NMK = false;
    }

    fsm::evse::Context* ctx;
    timer to;
    bool state_timeout() {
        return to.timeout();
    }
};
struct ResetChip_def   : public state_machine_def<ResetChip_def> {
    // States
    struct Delay     : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            to.setDurationMilliSeconds(fsm.ctx->slac_config.chip_reset.delay_ms);
            to.reset();
        }

        timer to;
        bool state_timeout() {
            return to.timeout();
        }
    };
    struct Sent      : public state<> { };
    struct Received  : public state<> { };
    struct Done      : public exit_pseudo_state<update> { };

    // Guards
    bool is_done(update const&){
        // Qualcomm sends a reply
        // CG5317 does not reply to the reset packet
        // Chip reset not supported for other chips
        return ctx->modem_vendor == defs::ModemVendor::Lumissil;
    }
    struct is_reset_message {
        template <class Fsm, class SrcT, class TarT>
        bool operator()(message const& e, Fsm& fsm, SrcT&, TarT& ) {
            const auto mmtype = e.payload.get_mmtype();
            auto expected = defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF;
            return mmtype == expected;
        }
    };

    // Actions
    struct send_message {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            auto& ctx = *fsm.ctx;
            if (ctx.modem_vendor == defs::ModemVendor::Qualcomm) {
                messages::qualcomm::cm_reset_device_req reset_req;
                ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req);
            } else if (ctx.modem_vendor == defs::ModemVendor::Lumissil) {
                messages::lumissil::nscm_reset_device_req reset_req;
                ctx.send_slac_message(ctx.slac_config.plc_peer_mac, reset_req);
            }
        };
    };

    // Transistions
    using initial_state = Delay;
    using p = ResetChip_def;
    struct transition_table : boost::mpl::vector<
        //     Source         + Event             -> Target           / Action            [Guard]
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Delay         , update             , Sent             , send_message     , timeout                  >,
        Row   < Sent          , message            , Received         , trigger_update   , is_reset_message         >,
        g_row < Sent          , update             , Done             /* none */         , &p::is_done              >,
        _row  < Received      , update             , Done             /* none */           /*none*/                 >
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        >{};
    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    //Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->status.match_state = SlacState::ResetChip;
        ctx->status.d3_state = D3State::Unmatched;
    }

    fsm::evse::Context* ctx;
};
struct Matched_def     : public state_machine_def<Matched_def> {
    // States
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Other         : public state<> { };
    struct Failed        : public exit_pseudo_state<message> { };

    // Guards
    struct detect_link {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.link_status.do_detect;
        }
    };

    // Transitions
    using initial_state = Init;
    using p = ResetChip_def;
    struct transition_table : boost::mpl::vector<
        //     Source         + Event             -> Target           / Action            [Guard] // last transition fires first
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Init          , none               , Other            , none             , none                     >,
        Row   < Init          , none               , Lumissil         , link_status_req  , is_lumissil              >,
        Row   < Init          , none               , Qualcomm         , link_status_req  , is_qualcomm              >,
        Row   < Init          , none               , NoDetect         , none             , Not_<detect_link>        >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Lumissil      , update             , Lumissil         , link_status_req  , timeout                  >,
        Row   < Lumissil      , message            , Failed           , none             , Not_<link_status_cnf>    >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Qualcomm      , update             , Qualcomm         , link_status_req  , timeout                  >,
        Row   < Qualcomm      , message            , Failed           , none             , Not_<link_status_cnf>    >
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        >{};

    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    // members;
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->signal_dlink_ready(true);
        link_check_to_ms = ctx->slac_config.link_status.poll_in_matched_state_ms;
        ctx->status.match_state = SlacState::Matched;
        ctx->status.d3_state = D3State::Matched;
        ctx->status.modem_link_ready = true;
    }

    template <class Event, class Fsm>
    void on_exit(Event const&, Fsm&) {
        ctx->signal_dlink_ready(false);
        ctx->status.ev_mac.fill(0);
        ctx->status.average_attenuation = 0.f;
        ctx->status.modem_link_ready = false;
    }

    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
};
struct WaitForLink_def : public state_machine_def<WaitForLink_def> {
    // States
    struct Init          : public state<> { };
    struct NoDetect      : public state<> { };
    struct Failed        : public exit_pseudo_state<none> { };
    struct Matched       : public exit_pseudo_state<message> { };

    // Guards
    struct is_match_req : public is_message_of_type<defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ> { };

    //Actions
    struct send_match_cnf {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const& e, Fsm& fsm, SrcT&, TarT& ) {
            auto& ctx = *fsm.ctx;
            auto ev_mac = e.payload.get_src_mac();
            ctx.send_slac_message(ev_mac, ctx.match_confirm_message);
        }
    };

    // Transitions
    using initial_state = Init;
    using p = ResetChip_def;
    struct transition_table : boost::mpl::vector<
        //     Source         + Event             -> Target           / Action            [Guard] // last transition fires first
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Init          , none               , Failed           , none             , none                     >,
        Row   < Init          , none               , Lumissil         , link_status_req  , is_lumissil              >,
        Row   < Init          , none               , Qualcomm         , link_status_req  , is_qualcomm              >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Lumissil      , update             , Lumissil         , link_status_req  , timeout                  >,
        Row   < Lumissil      , message            , Lumissil         , send_match_cnf   , is_match_req             >,
        Row   < Lumissil      , message            , Matched          , none             , link_status_cnf          >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Qualcomm      , update             , Qualcomm         , link_status_req  , timeout                  >,
        Row   < Qualcomm      , message            , Qualcomm         , send_match_cnf   , is_match_req             >,
        Row   < Qualcomm      , message            , Matched          , none             , link_status_cnf          >
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        >{};

    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        link_check_to_ms = ctx->slac_config.link_status.retry_ms;
        to.setDurationMilliSeconds(ctx->slac_config.link_status.timeout_ms);
        to.reset();
        ctx->status.match_state = SlacState::WairForLink;
        ctx->status.d3_state = D3State::Unmatched;
    }


    fsm::evse::Context* ctx;
    int link_check_to_ms{0};
    timer to;
    bool state_timeout(){
        return to.timeout();
    }
};
struct Init_def        : public state_machine_def<Init_def> {
    // States
    struct Init       : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct OpAttr     : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct GetVersion : timeout_state {
        template <class Event, class Fsm> void on_entry(Event const& e, Fsm& fsm) {
            timeout_state::state_timeout_ms = fsm.ctx->slac_config.request_info_delay_ms;
            timeout_state::on_entry(e, fsm);
        }
    };
    struct Done       : exit_pseudo_state<update> { };
    struct Other      : state<> { };
    struct Lumissil   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.get_payload<messages::lumissil::nscm_get_version_cnf>();
            return utils::device_info(msg);
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Lumissil;
        static auto constexpr msg_type = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF;
    };
    struct Qualcomm   : public state<> {
        static std::string device_info(message const& e) {
            auto msg = e.payload.get_payload<messages::qualcomm::op_attr_cnf>();
            return utils::device_info(msg);
        }
        static auto constexpr modem_vendor = defs::ModemVendor::Qualcomm;
        static auto constexpr msg_type = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF;
    };

    // Guards
    struct is_qualcomm_msg : public is_message_of_type<Qualcomm::msg_type>{ };
    struct is_lumissil_msg : public is_message_of_type<Lumissil::msg_type>{ };

    // Actions
    struct op_attr_req : public send_default_msg<messages::qualcomm::op_attr_req> { };
    struct get_version_req : public send_default_msg<messages::lumissil::nscm_get_version_req> { };
    struct set_modem_vendor {
        template <class Fsm, class SrcT, class TarT>
        void operator()(message const& e, Fsm& fsm, SrcT&, TarT&) {
            fsm.ctx->modem_vendor = TarT::modem_vendor;
            fsm.ctx->log_info(TarT::device_info(e));
        }

    };

    // Transitions
    using initial_state = boost::mpl::vector<Init, Other>;
    struct transition_table : boost::mpl::vector<
        //     Source         + Event             -> Target           / Action            [Guard]
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Init          , update             , OpAttr           , op_attr_req      , timeout                  >,
        Row   < OpAttr        , update             , GetVersion       , get_version_req  , timeout                  >,
        Row   < GetVersion    , update             , Done             , none             , timeout                  >,
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        Row   < Other         , message            , Lumissil         , set_modem_vendor , is_lumissil_msg          >,
        Row   < Other         , message            , Qualcomm         , set_modem_vendor , is_qualcomm_msg          >
        //    +---------------+--------------------+------------------+------------------+--------------------------+-
        >{};
    template <class FSM,class Event>
    void no_transition(Event const& e, FSM&,int state) { }

    // Members
    template <class Event, class Fsm>
    void on_entry(Event const&, Fsm& fsm) {
        ctx = fsm.ctx;
        ctx->status.match_state = SlacState::Init;
        ctx->status.d3_state = D3State::Unmatched;
    }

    fsm::evse::Context* ctx;
};



// Main StateMachine
struct SlacFSM_def : state_machine_def<SlacFSM_def> {
    // States
    using Init = state_machine<Init_def>;
    using Init_Done = Init::exit_pt<Init_def::Done>;

    using Reset = state_machine<Reset_def>;
    using Reset_ResetChip = Reset::exit_pt<Reset_def::ResetChip>;
    using Reset_Idle = Reset::exit_pt<Reset_def::Idle>;

    using ResetChip = state_machine<ResetChip_def>;
    using ResetChip_Done = ResetChip::exit_pt<ResetChip_def::Done>;

    using Matching = state_machine<Matching_def>;
    using Matching_Fail = Matching::exit_pt<Matching_def::Failed>;
    using Matching_Match = Matching::exit_pt<Matching_def::Matched>;

    using Matched = state_machine<Matched_def>;
    using Matched_Fail = Matched::exit_pt<Matched_def::Failed>;

    using WaitForLink = state_machine<WaitForLink_def>;
    using WaitForLink_Fail = WaitForLink::exit_pt<WaitForLink_def::Failed>;
    using WaitForLink_Match = WaitForLink::exit_pt<WaitForLink_def::Matched>;

    struct Idle   : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            fsm.ctx->status.match_state = SlacState::Idle;
            fsm.ctx->status.d3_state = D3State::Unmatched;
            fsm.ctx->status.modem_PIB = true;
            fsm.ctx->status.modem_NMK = true;
        }
    };
    struct Failed : public state<> {
        template <class Event, class Fsm>
        void on_entry(Event const&, Fsm& fsm) {
            auto& ctx = *fsm.ctx;
            if (ctx.slac_config.ac_mode_five_percent) {
                ctx.signal_error_routine_request();
            }
            ctx.status.match_state = SlacState::Failed;
            ctx.status.d3_state = D3State::Unmatched;
        }
    };

    // Guards
    struct cfg_wait_for_link {
        template <class Fsm, class Evt, class SrcT, class TarT>
        bool operator()(Evt const&, Fsm& fsm, SrcT&, TarT& ) {
            return fsm.ctx->slac_config.link_status.do_detect;
        }
    };

    // Actions
    struct on_matched_fail {
        template <class Fsm, class Evt, class SrcT, class TarT>
        void operator()(Evt const&, Fsm& fsm, SrcT& src, TarT& ) {
            auto& ctx = *fsm.ctx;
            ctx.log_error("Connection lost in matched state");
            ctx.signal_error_routine_request();
        };
    };

    // Transitions
    using initial_state = Init;
    struct transition_table : boost::mpl::vector<
        //  +Source            + Event              + -> Target        + / Action         + [Guard]                  +
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Init_Done        , update             , Reset            , none             , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Reset            , reset              , Reset            , none             , none                    >,
        Row < Reset            , update             , Failed           , none             , timeout                 >,
        Row < Reset_ResetChip  , update             , ResetChip        , none             , none                    >,
        Row < Reset_Idle       , update             , Idle             , none             , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < ResetChip_Done   , update             , Idle             , none             , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Idle             , enter_bcd          , Matching         , none             , none                    >,
        Row < Idle             , reset              , Reset            , none             , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Matching         , reset              , Reset            , none             , none                    >,
        Row < Matching         , leave_bcd          , Idle             , none             , none                    >,
        Row < Matching         , update             , Failed           , none             , timeout                 >,
        Row < Matching_Fail    , none               , Failed           , none             , none                    >,
        Row < Matching_Match   , none               , WaitForLink      , none             , cfg_wait_for_link       >,
        Row < Matching_Match   , none               , Matched          , none             , Not_<cfg_wait_for_link> >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < WaitForLink      , update             , Failed           , none             , timeout                 >,
        Row < WaitForLink      , reset              , Reset            , none             , none                    >,
        Row < WaitForLink_Fail , none               , Failed           , none             , none                    >,
        Row < WaitForLink_Match, message            , Matched          , none             , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Matched          , reset              , Reset            , none             , none                    >,
        Row < Matched_Fail     , message            , Failed           , on_matched_fail  , none                    >,
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        Row < Failed           , reset              , Reset            , none             , none                    >
        //  +------------------+--------------------+------------------+------------------+--------------------------+
        > {};
    template <class FSM,class Event>
    void no_transition(Event const& e, FSM& fsm,int state) {
    }

    // Members
    fsm::evse::Context* ctx;
    SlacFSM_def(fsm::evse::Context& ctx_) : ctx(&ctx_) {
    }
};

using SlacFSM = state_machine<SlacFSM_def>;

// clang-format on

} // namespace everest::lib::slac::msm
