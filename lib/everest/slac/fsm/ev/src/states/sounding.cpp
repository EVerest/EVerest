// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include <everest/slac/fsm/ev/states/sounding.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <random>

#include "timing_helper.hpp"

#include <everest/slac/fsm/ev/states/others.hpp>

namespace slac::fsm::ev {

SoundingState::SoundingState(Context& ctx, SessionParamaters session_parameters_) :
    FSMSimpleState(ctx), session_parameters(std::move(session_parameters_)) {
}

void SoundingState::enter() {
    sounding_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(slac::defs::TT_EV_ATTEN_RESULTS_MS);
}

FSMSimpleState::CallbackReturnType SoundingState::callback() {
    const auto now = std::chrono::steady_clock::now();
    const auto sounding_time_left = milliseconds_left(now, sounding_timeout);

    if (sounding_time_left <= 0) {
        return Event::FAILED;
    }

    if (count_mnbc_sound_sent == 10) {
        // we're already done, return time left until sounding timeout
        return sounding_time_left;
    }

    if (count_start_atten_char_sent == 0) {
        // no sounding messages have been send yet
        next_timeout = now;
    }

    const auto next_step_time_left = milliseconds_left(now, next_timeout);

    if (next_step_time_left > 0) {
        // just idle a bit
        return std::min(next_step_time_left, sounding_time_left);
    }

    // need to issue the next step
    if (do_sounding()) {
        // FIXME (aw): how to setup the next timeout, we could send the everything right away, but waiting at least a
        // bit seems to be a good practice
        const auto next_step_delay_ms = 20; // FIXME (aw): which value to use? TP_EV_BATCH_MSG_INTERVAL_MS
        next_timeout = now + std::chrono::milliseconds(next_step_delay_ms);
        return std::min(next_step_delay_ms, static_cast<int>(sounding_time_left));
    }

    return sounding_time_left;
}

FSMSimpleState::HandleEventReturnType SoundingState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        if (handle_valid_atten_char_ind()) {
            return sa.create_simple<MatchRequestState>(ctx, session_parameters);
        }
        return sa.HANDLED_INTERNALLY;
    } else if (ev == Event::FAILED) {
        // callback() raises FAILED once TT_EV_atten_results expires without a
        // CM_ATTEN_CHAR.IND. Without this branch the event is passed on, ends up
        // unhandled, and the controller re-feeds immediately -- a busy loop that
        // spins at 100% CPU holding feed_mtx, so reset/trigger_matching can never
        // be serviced again and the EV is stuck for good.
        return sa.create_simple<FailedState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    }

    return sa.PASS_ON;
}

bool SoundingState::do_sounding() {
    if (count_start_atten_char_sent < slac::defs::C_EV_START_ATTEN_CHAR_INDS) {

        slac::messages::cm_start_atten_char_ind msg;
        msg.application_type = 0x0;
        msg.security_type = 0x0;
        msg.num_sounds = slac::defs::C_EV_MATCH_MNBC;
        msg.timeout = (slac::defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100; // in multiples of 100ms!
        msg.resp_type = 0x01; // fixed value indicating 'other Green Phy station'
        memcpy(msg.forwarding_sta, ctx.ev_host_mac.data(), sizeof(msg.forwarding_sta));
        memcpy(msg.run_id, session_parameters.run_id, sizeof(msg.run_id));

        ctx.send_slac_message(ctx.BROADCAST_MAC.data(), msg);

        count_start_atten_char_sent++;

    } else if (count_mnbc_sound_sent < slac::defs::C_EV_MATCH_MNBC) {
        count_mnbc_sound_sent++;

        slac::messages::cm_mnbc_sound_ind msg;
        msg.application_type = 0x0;
        msg.security_type = 0x0;
        memset(msg.sender_id, 0, sizeof(msg.sender_id));
        msg.remaining_sound_count = slac::defs::C_EV_MATCH_MNBC - count_mnbc_sound_sent;
        memcpy(msg.run_id, session_parameters.run_id, sizeof(msg.run_id));
        memset(msg._reserved, 0, sizeof(msg._reserved));

        // FIXME (aw): does this have any performance penalties?
        std::random_device rnd_dev;
        std::mt19937 rng(rnd_dev());
        std::uniform_int_distribution<std::mt19937::result_type> dist256(0, 255);

        for (auto& random : msg.random) {
            random = dist256(rng);
        }

        ctx.send_slac_message(ctx.BROADCAST_MAC.data(), msg);
    }

    return (count_mnbc_sound_sent < slac::defs::C_EV_MATCH_MNBC);
}

bool SoundingState::handle_valid_atten_char_ind() {
    if (count_mnbc_sound_sent < slac::defs::C_EV_MATCH_MNBC) {
        ctx.log_info("Received unexpected message while SOUNDING");
        return false;
    }

    const auto mmtype = ctx.slac_message.get_mmtype();

    if (mmtype != (slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_IND)) {
        ctx.log_info("Received unexpected message after SOUNDING");
        return false;
    }

    // correct message type
    const auto& atten_char = ctx.slac_message.get_payload<slac::messages::cm_atten_char_ind>();

    // [V2G3-A09-35]: a CM_ATTEN_CHAR.IND whose content deviates from the MME
    // definition in ISO 15118-3 Table A.4 is invalid and shall be ignored --
    // silently, without a CM_ATTEN_CHAR.RSP. Every field the table pins to a
    // fixed value is checked here; the variable ones (NumSounds, ATTEN_PROFILE)
    // are covered by [V2G3-A09-36] below.
    if (atten_char.application_type != 0x00 or atten_char.security_type != 0x00) {
        ctx.log_info("Ignoring CM_ATTEN_CHAR.IND with an invalid SLAC payload header");
        return false;
    }

    // Table A.4: SOURCE_ADDRESS is the MAC of the EV Host that initiated the
    // matching process, i.e. our own.
    if (memcmp(atten_char.source_address, ctx.ev_host_mac.data(), sizeof(atten_char.source_address)) != 0) {
        ctx.log_info("Ignoring CM_ATTEN_CHAR.IND with a foreign SOURCE_ADDRESS");
        return false;
    }

    const auto run_id_match =
        (memcmp(session_parameters.run_id, atten_char.run_id, sizeof(session_parameters.run_id)) == 0);

    if (run_id_match == false) {
        return false;
    }

    // Table A.4 fixes both station ids to all-zero.
    const auto all_zero = [](const uint8_t* data, size_t len) {
        return std::all_of(data, data + len, [](uint8_t byte) { return byte == 0; });
    };
    if (not all_zero(atten_char.source_id, sizeof(atten_char.source_id)) or
        not all_zero(atten_char.resp_id, sizeof(atten_char.resp_id))) {
        ctx.log_info("Ignoring CM_ATTEN_CHAR.IND with a non-zero SOURCE_ID/RESP_ID");
        return false;
    }

    // [V2G3-A09-36]: with NUM_SOUNDS zero the ATTEN_PROFILE has no significance
    // and the whole message shall be ignored. An empty profile is equally
    // meaningless -- Table A.4 sizes it at C_atten_group_count groups.
    if (atten_char.num_sounds == 0 or atten_char.attenuation_profile.num_groups != slac::defs::AAG_LIST_LEN) {
        ctx.log_info("Ignoring CM_ATTEN_CHAR.IND with an unusable attenuation profile");
        return false;
    }

    // reply
    slac::messages::cm_atten_char_rsp response;
    response.application_type = 0x0;
    response.security_type = 0x0;
    memcpy(response.source_address, ctx.ev_host_mac.data(), sizeof(response.source_address));
    memcpy(response.run_id, atten_char.run_id, sizeof(response.run_id));
    memset(response.source_id, 0, sizeof(response.source_id));
    memset(response.resp_id, 0, sizeof(response.resp_id));
    response.result = 0x0;

    ctx.send_slac_message(session_parameters.evse_mac, response);

    return true;
}

} // namespace slac::fsm::ev
