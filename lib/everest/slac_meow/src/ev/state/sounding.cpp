// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include <chrono>

#include <everest/slac/ev/detail/actions.hpp>
#include <everest/slac/ev/detail/guards.hpp>
#include <everest/slac/ev/state/failed.hpp>
#include <everest/slac/ev/state/reset.hpp>
#include <everest/slac/ev/state/sounding.hpp>
#include <everest/slac/ev/state/wait_atten_char_ind.hpp>
#include <everest/slac/ev/state/wait_match_cnf.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/messages.hpp>

namespace everest::slac::ev::state {

namespace {

/// Send the next of the 3 CM_START_ATTEN_CHAR.IND then 10 CM_MNBC_SOUND.IND sounding messages.
void send_next_sounding(Context& ctx) {

    if (ctx.start_atten_char_count < defs::C_EV_START_ATTEN_CHAR_INDS) {
        messages::cm_start_atten_char_ind msg{};
        msg.application_type = defs::COMMON_APPLICATION_TYPE;
        msg.security_type = defs::COMMON_SECURITY_TYPE;
        msg.num_sounds = defs::C_EV_MATCH_MNBC;
        msg.timeout = static_cast<std::uint8_t>((defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100);
        msg.resp_type = defs::CM_SLAC_PARM_CNF_RESP_TYPE;
        copy_to_wire(msg.forwarding_sta, ctx.ev_host_mac);
        copy_to_wire(msg.run_id, ctx.active_session.run_id);

        ctx.start_atten_char_count++;
        if (not ctx.send_slac_message(Context::BROADCAST_MAC, msg)) {
            ctx.log_warn("Failed to send CM_START_ATTEN_CHAR.IND");
        }
        return;
    }

    if (ctx.mnbc_sound_count < defs::C_EV_MATCH_MNBC) {
        messages::cm_mnbc_sound_ind msg{};
        msg.application_type = defs::COMMON_APPLICATION_TYPE;
        msg.security_type = defs::COMMON_SECURITY_TYPE;
        zero_wire(msg.sender_id);
        ++ctx.mnbc_sound_count;
        msg.remaining_sound_count = defs::C_EV_MATCH_MNBC - ctx.mnbc_sound_count;
        copy_to_wire(msg.run_id, ctx.active_session.run_id);
        zero_wire(msg._reserved);
        randomize(msg.random);

        if (not ctx.send_slac_message(Context::BROADCAST_MAC, msg)) {
            ctx.log_warn("Failed to send CM_MNBC_SOUND.IND");
        }
    }
}

} // namespace

void Sounding::enter() {
    m_deadline.arm(m_ctx.current_time, std::chrono::milliseconds(defs::TT_EV_ATTEN_RESULTS_MS));
}

Result Sounding::feed(SlacEvent const& ev) {
    if (std::get_if<event::Update>(&ev)) {
        if (m_deadline.expired(m_ctx.current_time)) {
            return m_ctx.create_state<Failed>();
        }
        if (m_ctx.all_sounding_messages_sent()) {
            return m_ctx.create_state<WaitAttenCharInd>();
        }
        // one sounding message per tick; deliberately does not re-arm the deadline, which covers
        // the whole burst
        send_next_sounding(m_ctx);
        return handled();
    }

    if (auto const* frame = as_frame(ev)) {
        // the EVSE can answer before we have left Sounding, but only once the burst is complete
        if (m_ctx.all_sounding_messages_sent() and is_atten_char_ind_for_run(*frame, m_ctx.active_session.run_id)) {
            send_atten_char_rsp_and_match_req(m_ctx, *frame);
            return m_ctx.create_state<WaitMatchCnf>();
        }
        return {};
    }

    if (std::get_if<event::Reset>(&ev)) {
        m_ctx.clear_session();
        return m_ctx.create_state<Reset>();
    }

    return {};
}

} // namespace everest::slac::ev::state
