// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include <everest/slac/fsm/ev/states/others.hpp>

#include <algorithm>
#include <cstring>
#include <optional>
#include <random>

#include <endian.h>

#include "../misc.hpp"
#include "timing_helper.hpp"

#include <everest/slac/fsm/ev/states/sounding.hpp>

namespace slac::fsm::ev {

void ResetState::probe_modem_vendor() {
    // Mirrors slac::fsm::evse::InitState: each probe is answered only by the
    // vendor that defines it, so whichever CNF comes back identifies the modem.
    // Link detection needs this because the data-link status is read with a
    // vendor MME -- Qualcomm's LINK_STATUS and Lumissil's NSCM_GET_D_LINK_STATUS
    // are different messages.
    slac::messages::qualcomm::op_attr_req op_attr_req;
    ctx.send_slac_message(ctx.slac_config.plc_peer_mac, op_attr_req);

    slac::messages::lumissil::nscm_get_version_req get_version_req;
    ctx.send_slac_message(ctx.slac_config.plc_peer_mac, get_version_req);
}

void ResetState::handle_vendor_probe_cnf() {
    const auto mmtype = ctx.slac_message.get_mmtype();
    if (mmtype == (slac::defs::qualcomm::MMTYPE_OP_ATTR | slac::defs::MMTYPE_MODE_CNF)) {
        ctx.modem_vendor = ModemVendor::Qualcomm;
        ctx.log_info("Local PLC modem identified as Qualcomm");
    } else if (mmtype == (slac::defs::lumissil::MMTYPE_NSCM_GET_VERSION | slac::defs::MMTYPE_MODE_CNF)) {
        ctx.modem_vendor = ModemVendor::Lumissil;
        ctx.log_info("Local PLC modem identified as Lumissil");
    }
}

void ResetState::enter() {
    leave_logical_network();
    if (ctx.slac_config.link_status.do_detect and ctx.modem_vendor == ModemVendor::Unknown) {
        probe_modem_vendor();
    }
    // [V2G3-M09-18]: leaving the logical network is reported to HLE as
    // D-LINK_READY.indication (no link) -- signal_state("UNMATCHED") is what
    // raises that in the module.
    ctx.signal_state("UNMATCHED");
    ctx.log_info("Entered Reset state");
}

void ResetState::leave_logical_network() {
    if (not ctx.joined_avln) {
        return;
    }

    // ISO 15118-3 requires the EV to leave the logical network and return to
    // "Unmatched" within TP_match_leave when state E is detected on the EV side
    // ([V2G3-M09-19]), on a D-LINK_TERMINATE.request ([V2G3-M09-17]) and on a
    // D-LINK_ERROR.request ([V2G3-M07-13]) -- every one of which reaches us as
    // a reset. [V2G3-A09-121] gives the mechanism: leave the logical network
    // and RESET THE NMK, per the "Leaving an AVLN" clause of [HPGP]. HomePlug
    // has no leave MME for a station, so adopting a fresh random NMK (which
    // moves the local modem to a logical network of its own) is the leave.
    const std::string CHARACTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::random_device random_device;
    std::mt19937 generator(random_device());
    std::uniform_int_distribution<> distribution(0, CHARACTERS.size() - 1);

    uint8_t fresh_nmk[slac::defs::NMK_LEN];
    for (std::size_t i = 0; i < sizeof(fresh_nmk); ++i) {
        fresh_nmk[i] = static_cast<uint8_t>(CHARACTERS[distribution(generator)]);
    }

    slac::messages::cm_set_key_req msg;
    msg.key_type = slac::defs::CM_SET_KEY_REQ_KEY_TYPE_NMK;
    msg.my_nonce = 0xAAAAAAAA;
    msg.your_nonce = 0x00000000;
    msg.pid = slac::defs::CM_SET_KEY_REQ_PID_HLE;
    msg.prn = htole16(slac::defs::CM_SET_KEY_REQ_PRN_UNUSED);
    msg.pmn = slac::defs::CM_SET_KEY_REQ_PMN_UNUSED;
    msg.cco_capability = slac::defs::CM_SET_KEY_REQ_CCO_CAP_NONE;
    slac::utils::generate_nid_from_nmk(msg.nid, fresh_nmk);
    msg.new_eks = slac::defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA;
    memcpy(msg.new_key, fresh_nmk, sizeof(msg.new_key));

    ctx.send_slac_message(ctx.EV_PLC_MAC.data(), msg);
    ctx.joined_avln = false;
    ctx.log_info("Left the logical network (NMK reset to a fresh random key)");
}

FSMSimpleState::HandleEventReturnType ResetState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        handle_vendor_probe_cnf();
        return sa.HANDLED_INTERNALLY;
    }
    if (ev == Event::TRIGGER_MATCHING) {
        // [V2G3-A09-122]: TT_matching_repetition starts with the trigger of the
        // matching process -- here, not on each retry, so the whole sequence of
        // retries is bounded by one window.
        ctx.matching_repetition_deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(slac::defs::TT_MATCHING_REPETITION_MS);
        return sa.create_simple<InitSlacState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

FSMSimpleState::CallbackReturnType ResetState::callback() {
    ctx.log_info("Called callback of ResetState");

    return {};
}

FSMSimpleState::HandleEventReturnType InitSlacState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        if (check_for_valid_parm_conf()) {
            ctx.signal_state("MATCHING");
            return sa.create_simple<SoundingState>(ctx, SessionParamaters{run_id, ctx.slac_message.get_src_mac()});
        }

        return sa.HANDLED_INTERNALLY;
    } else if (ev == Event::FAILED) {
        return sa.create_simple<FailedState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

void InitSlacState::enter() {
    ctx.log_info("Entered init state");

    // generate random run_id
    std::random_device rnd_dev;
    std::mt19937 rng(rnd_dev());
    std::uniform_int_distribution<std::mt19937::result_type> dist256(0, 255);

    for (auto& id : this->run_id) {
        id = dist256(rng);
    }
}

FSMSimpleState::CallbackReturnType InitSlacState::callback() {
    if (num_of_tries == 0) {
        return send_parm_req();
    }

    // did already send a parm req, check for timeout

    const auto now = std::chrono::steady_clock::now();
    const auto time_left = milliseconds_left(now, next_timeout);

    if (time_left > 0) {
        // still have time
        return time_left;
    }

    // Timeout. ISO 15118-3 limits one matching attempt to the initial
    // CM_SLAC_PARM.REQ plus C_EV_match_retry repetitions; the EV must then
    // fall silent instead of hammering the wire. Enforced by ISO 15118-5
    // TC_EVCC_CMN_VTB_CmSlacParm_002/_003.
    if (num_of_tries <= slac::defs::C_EV_MATCH_RETRY) {
        return send_parm_req();
    }

    // no retries left fail
    ctx.log_warn("No CM_SLAC_PARM.CNF after " + std::to_string(num_of_tries) +
                 " CM_SLAC_PARM.REQ attempts (initial + C_EV_match_retry), SLAC matching failed");
    return Event::FAILED;
}

int InitSlacState::send_parm_req() {
    slac::messages::cm_slac_parm_req msg;
    msg.application_type = 0x0;
    msg.security_type = 0x0;
    memcpy(msg.run_id, run_id, sizeof(msg.run_id));

    if (num_of_tries == 0) {
        ctx.log_info("Sending CM_SLAC_PARM.REQ (run id " + format_run_id(run_id) + ")");
    } else {
        ctx.log_warn("CM_SLAC_PARM.CNF timeout, retrying CM_SLAC_PARM.REQ (attempt " +
                     std::to_string(num_of_tries + 1) + ")");
    }

    ctx.send_slac_message(ctx.BROADCAST_MAC.data(), msg);

    num_of_tries++;

    next_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(slac::defs::TT_MATCH_RESPONSE_MS);

    return slac::defs::TT_MATCH_RESPONSE_MS;
}

bool InitSlacState::check_for_valid_parm_conf() {
    const auto mmtype = ctx.slac_message.get_mmtype();
    if (mmtype != (slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_CNF)) {
        ctx.log_warn("Received non-expected SLAC message of type " + format_mmtype(mmtype) +
                     " while waiting for CM_SLAC_PARM.CNF");
        return false;
    }

    // correct message type
    const auto& parm_cnf = ctx.slac_message.get_payload<slac::messages::cm_slac_parm_cnf>();

    // [V2G3-A09-09]: a CM_SLAC_PARM.CNF whose content deviates from the MME
    // definition in ISO 15118-3 Table A.2 is invalid and shall be ignored, which
    // leaves InitSlacState retransmitting the REQ under the [V2G3-A09-10] retry
    // budget. Every field the table pins to a fixed value is checked; RunID is
    // handled below because a mismatch there is a different (expected) case.
    static constexpr uint8_t M_SOUND_TARGET_BROADCAST[slac::messages::M_SOUND_TARGET_LEN] = {0xFF, 0xFF, 0xFF,
                                                                                             0xFF, 0xFF, 0xFF};
    if (parm_cnf.application_type != 0x00 or parm_cnf.security_type != 0x00 or parm_cnf.resp_type != 0x01 or
        parm_cnf.num_sounds != slac::defs::C_EV_MATCH_MNBC or
        parm_cnf.timeout != (slac::defs::TT_EVSE_MATCH_MNBC_MS + 99) / 100 or
        memcmp(parm_cnf.m_sound_target, M_SOUND_TARGET_BROADCAST, sizeof(M_SOUND_TARGET_BROADCAST)) != 0 or
        memcmp(parm_cnf.forwarding_sta, ctx.ev_host_mac.data(), sizeof(parm_cnf.forwarding_sta)) != 0) {
        ctx.log_warn("Received CM_SLAC_PARM.CNF deviating from ISO 15118-3 Table A.2, ignoring");
        return false;
    }

    const auto same_run_id = (memcmp(parm_cnf.run_id, run_id, sizeof(run_id)) == 0);

    if (same_run_id) {
        ctx.log_info("Received CM_SLAC_PARM.CNF from " + format_mac_addr(ctx.slac_message.get_src_mac()) +
                     ", starting sounding");
    } else {
        ctx.log_warn("Received CM_SLAC_PARM.CNF with a foreign run id (" + format_run_id(parm_cnf.run_id) +
                     ", expected " + format_run_id(run_id) + "), ignoring");
    }

    return same_run_id;
}

MatchRequestState::MatchRequestState(Context& ctx, SessionParamaters session_parameters_) :
    FSMSimpleState(ctx), session_parameters(std::move(session_parameters_)) {
}

FSMSimpleState::HandleEventReturnType MatchRequestState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        const auto nmk = check_for_valid_match_req_conf();
        if (nmk) {
            return sa.create_simple<JoinNetworkState>(ctx, nmk);
        }

        return sa.HANDLED_INTERNALLY;
    } else if (ev == Event::FAILED) {
        return sa.create_simple<FailedState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

void MatchRequestState::enter() {
    ctx.log_info("Entered MatchRequestState state");
}

FSMSimpleState::CallbackReturnType MatchRequestState::callback() {
    if (num_of_tries == 0) {
        return send_match_req();
    }

    // did already send a parm req, check for timeout

    const auto now = std::chrono::steady_clock::now();
    const auto time_left = milliseconds_left(now, next_timeout);

    if (time_left > 0) {
        // still have time
        return time_left;
    }

    // Timeout. [V2G3-A09-94] allows C_EV_match_retry RETRANSMISSIONS on top of
    // the initial CM_SLAC_MATCH.REQ, so the budget is num_of_tries <= retry
    // (three frames in total), not < (which would send only one retransmission).
    // Enforced by TC_EVCC_CMN_VTB_CmSlacMatch_003..010, which count them.
    if (num_of_tries <= slac::defs::C_EV_MATCH_RETRY) {
        return send_match_req();
    }

    // no retries left fail
    ctx.log_warn("No CM_SLAC_MATCH.CNF after " + std::to_string(num_of_tries) +
                 " CM_SLAC_MATCH.REQ attempts, SLAC matching failed");
    return Event::FAILED;
}

const uint8_t* MatchRequestState::check_for_valid_match_req_conf() {
    if (num_of_tries == 0) {
        return nullptr;
    }

    const auto mmtype = ctx.slac_message.get_mmtype();
    if (mmtype != (slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_CNF)) {
        return nullptr;
    }

    // correct message type
    const auto& match_cnf = ctx.slac_message.get_payload<slac::messages::cm_slac_match_cnf>();

    // [V2G3-A09-95]: a CM_SLAC_MATCH.CNF whose content deviates from the MME
    // definition in ISO 15118-3 Table A.7 is invalid and shall be ignored --
    // the matching process then keeps retransmitting the REQ until the retry
    // budget in callback() runs out. Accepting one of these would join the EV
    // to a logical network described by a malformed message.
    const auto all_zero = [](const uint8_t* data, size_t len) {
        return std::all_of(data, data + len, [](uint8_t byte) { return byte == 0; });
    };

    if (match_cnf.application_type != 0x00 or match_cnf.security_type != 0x00 or
        match_cnf.mvf_length != slac::defs::CM_SLAC_MATCH_CNF_MVF_LENGTH or
        not all_zero(match_cnf.pev_id, sizeof(match_cnf.pev_id)) or
        not all_zero(match_cnf.evse_id, sizeof(match_cnf.evse_id)) or
        memcmp(match_cnf.pev_mac, ctx.ev_host_mac.data(), sizeof(match_cnf.pev_mac)) != 0 or
        memcmp(match_cnf.evse_mac, session_parameters.evse_mac, sizeof(match_cnf.evse_mac)) != 0) {
        ctx.log_warn("Received CM_SLAC_MATCH.CNF deviating from ISO 15118-3 Table A.7, ignoring");
        return nullptr;
    }

    const auto run_id_match =
        (memcmp(session_parameters.run_id, match_cnf.run_id, sizeof(session_parameters.run_id)) == 0);

    if (run_id_match) {
        ctx.log_info("Received CM_SLAC_MATCH.CNF, NMK: " + format_nmk(match_cnf.nmk));
        return match_cnf.nmk;
    } else {
        ctx.log_warn("Received CM_SLAC_MATCH.CNF with a foreign run id (" + format_run_id(match_cnf.run_id) +
                     ", expected " + format_run_id(session_parameters.run_id) + "), ignoring");
        return nullptr;
    }
}

int MatchRequestState::send_match_req() {
    slac::messages::cm_slac_match_req msg;
    msg.application_type = 0x0;
    msg.security_type = 0x0;
    msg.mvf_length = htole16(0x3e); // FIXME (aw) fixed constant
    memset(msg.pev_id, 0, sizeof(msg.pev_id));
    memcpy(msg.pev_mac, ctx.ev_host_mac.data(), sizeof(msg.pev_mac));
    memset(msg.evse_id, 0, sizeof(msg.evse_id));
    memcpy(msg.evse_mac, session_parameters.evse_mac, sizeof(msg.evse_mac));
    memcpy(msg.run_id, session_parameters.run_id, sizeof(msg.run_id));
    memset(msg._reserved, 0, sizeof(msg._reserved));

    if (num_of_tries == 0) {
        ctx.log_info("Sending CM_SLAC_MATCH.REQ to " + format_mac_addr(session_parameters.evse_mac));
    } else {
        ctx.log_warn("CM_SLAC_MATCH.CNF timeout, retrying CM_SLAC_MATCH.REQ (attempt " +
                     std::to_string(num_of_tries + 1) + "/" + std::to_string(slac::defs::C_EV_MATCH_RETRY + 1) + ")");
    }

    ctx.send_slac_message(session_parameters.evse_mac, msg);

    num_of_tries++;

    next_timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(slac::defs::TT_MATCH_RESPONSE_MS);

    return slac::defs::TT_MATCH_RESPONSE_MS;
}

JoinNetworkState::JoinNetworkState(Context& ctx, const uint8_t* nmk_) : FSMSimpleState(ctx) {
    memcpy(nmk, nmk_, sizeof(nmk));
}

FSMSimpleState::HandleEventReturnType JoinNetworkState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        if (check_for_valid_set_key_conf()) {
            // FIXME (aw): later on, we also need to distinguish between set_key failed
            return sa.create_simple<MatchedState>(ctx);
        }

        return sa.HANDLED_INTERNALLY;
    } else if (ev == Event::FAILED) {
        return sa.create_simple<FailedState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

void JoinNetworkState::enter() {
    ctx.log_info("Entered JoinNetwork state, joining the AVLN with the NMK from CM_SLAC_MATCH.CNF");

    // From here on the local modem carries the EVSE's NMK, so a later reset has
    // to undo that to leave the AVLN again ([V2G3-A09-121]). Set before the
    // request goes out: if the join is interrupted the key may already have
    // been adopted, and leaving a network we are not in is harmless.
    ctx.joined_avln = true;

    slac::messages::cm_set_key_req msg;
    msg.key_type = slac::defs::CM_SET_KEY_REQ_KEY_TYPE_NMK;
    msg.my_nonce = 0xAAAAAAAA;
    msg.your_nonce = 0x00000000;
    msg.pid = slac::defs::CM_SET_KEY_REQ_PID_HLE;
    msg.prn = htole16(slac::defs::CM_SET_KEY_REQ_PRN_UNUSED);
    msg.pmn = slac::defs::CM_SET_KEY_REQ_PMN_UNUSED;
    msg.cco_capability = slac::defs::CM_SET_KEY_REQ_CCO_CAP_NONE;
    slac::utils::generate_nid_from_nmk(msg.nid, nmk);
    msg.new_eks = slac::defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA;
    memcpy(msg.new_key, nmk, sizeof(msg.new_key));

    ctx.send_slac_message(ctx.EV_PLC_MAC.data(), msg);

    timeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(SET_KEY_TIMEOUT_MS);
}

FSMSimpleState::CallbackReturnType JoinNetworkState::callback() {
    const auto now = std::chrono::steady_clock::now();
    const auto time_left = milliseconds_left(now, timeout);

    if (time_left > 0) {
        // still have time
        return time_left;
    }

    // we reached the set key timeout
    ctx.log_warn("CM_SET_KEY.CNF timeout, could not join the AVLN");
    return Event::FAILED;
}

bool JoinNetworkState::check_for_valid_set_key_conf() {
    const auto mmtype = ctx.slac_message.get_mmtype();
    if (mmtype != (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_CNF)) {
        ctx.log_warn("Received non-expected SLAC message of type " + format_mmtype(mmtype) +
                     " while waiting for CM_SET_KEY.CNF");
        return false;
    }

    ctx.log_info("Received CM_SET_KEY.CNF, joined the AVLN");

    // correct message type
    const auto& set_key_cnf = ctx.slac_message.get_payload<slac::messages::cm_set_key_cnf>();

    // FIXME (aw): validation of the message?

    return true;
}

static std::optional<bool> check_link_status_cnf(ModemVendor modem_vendor, slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();
    if (modem_vendor == ModemVendor::Qualcomm and
        mmtype == (slac::defs::qualcomm::MMTYPE_LINK_STATUS | slac::defs::MMTYPE_MODE_CNF)) {
        return {message.get_payload<slac::messages::qualcomm::link_status_cnf>().link_status == 0x01};
    } else if (modem_vendor == ModemVendor::Lumissil and
               mmtype == (slac::defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | slac::defs::MMTYPE_MODE_CNF)) {
        return {message.get_payload<slac::messages::lumissil::nscm_get_d_link_status_cnf>().link_status == 0x01};
    }
    return {};
}

static bool send_link_status_req(Context& ctx) {
    if (ctx.modem_vendor == ModemVendor::Qualcomm) {
        slac::messages::qualcomm::link_status_req link_status_req;
        ctx.send_slac_message(ctx.slac_config.plc_peer_mac, link_status_req);
        return true;
    } else if (ctx.modem_vendor == ModemVendor::Lumissil) {
        slac::messages::lumissil::nscm_get_d_link_status_req link_status_req;
        ctx.send_slac_message(ctx.slac_config.plc_peer_mac, link_status_req);
        return true;
    }
    return false;
}

void MatchedState::enter() {
    ctx.log_info("Entered matched state");
    ctx.signal_state("MATCHED");
    link_up_deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(ctx.slac_config.link_status.timeout_ms);
}

FSMSimpleState::CallbackReturnType MatchedState::callback() {
    const auto& link_status = ctx.slac_config.link_status;

    if (not link_status.do_detect) {
        return {};
    }

    // [V2G3-M07-34]: an unexpected loss of communication is handled per
    // [V2G3-M09-19], i.e. leave the logical network within TP_match_leave and
    // return to "Unmatched". The host only learns the data link died by asking
    // its own modem, so poll it while matched -- the EVSE side does the same.
    if (not link_status_req_sent) {
        if (not send_link_status_req(ctx)) {
            ctx.log_error("Link status detection is enabled, but the modem vendor is unknown");
            return {};
        }
        link_status_req_sent = true;
        return link_status.retry_ms;
    }

    // No answer within retry_ms: ask again on the next poll rather than
    // declaring the link dead on a single lost management frame.
    link_status_req_sent = false;
    return link_status.poll_in_matched_state_ms;
}

FSMSimpleState::HandleEventReturnType MatchedState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else if (ev == Event::SLAC_MESSAGE) {
        const auto link_ok = check_link_status_cnf(ctx.modem_vendor, ctx.slac_message);
        if (link_ok.has_value()) {
            link_status_req_sent = false;
            if (link_ok.value()) {
                link_was_up = true;
            } else if (not link_was_up and std::chrono::steady_clock::now() < link_up_deadline) {
                // The AVLN is not up the instant the match completes -- both
                // modems still have to settle on the new NMK. Only report a
                // LOSS of communication, so wait out link_status.timeout_ms
                // before concluding anything from a down answer.
                ctx.log_debug("Data link not up yet after matching, still waiting");
            } else {
                // ResetState leaves the AVLN (NMK reset) and reports
                // D-LINK_READY (no link) to HLE, which is what
                // [V2G3-M09-19]/[V2G3-M09-18] ask for.
                ctx.log_error("Data link lost while matched, leaving the logical network");
                return sa.create_simple<ResetState>(ctx);
            }
        }
        return sa.HANDLED_INTERNALLY;
    } else {
        return sa.PASS_ON;
    }
}

void FailedState::enter() {
    ctx.log_info("Entered failed state");
    retry_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(slac::defs::TT_MATCHING_RATE_MS);
}

FSMSimpleState::CallbackReturnType FailedState::callback() {
    // [V2G3-A09-123]/[V2G3-A09-125]: a failed matching process is restarted for
    // as long as TT_matching_repetition (started at the trigger) has not
    // expired; once it has, the process stops in "Unmatched" and waits for the
    // next trigger. Without this the very first failure was terminal.
    const auto now = std::chrono::steady_clock::now();

    if (now >= ctx.matching_repetition_deadline) {
        ctx.log_info("TT_matching_repetition expired, staying unmatched until the next trigger");
        return {};
    }

    const auto time_left = milliseconds_left(now, retry_at);
    if (time_left > 0) {
        return time_left;
    }

    ctx.log_info("Restarting the matching process (TT_matching_repetition has not expired yet)");
    return Event::RETRY_MATCHING;
}

FSMSimpleState::HandleEventReturnType FailedState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else if (ev == Event::RETRY_MATCHING) {
        return sa.create_simple<InitSlacState>(ctx);
    } else if (ev == Event::SLAC_MESSAGE) {
        return sa.HANDLED_INTERNALLY;
    } else {
        return sa.PASS_ON;
    }
}

} // namespace slac::fsm::ev
