// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#include <everest/slac/fsm/evse/states/others.hpp>

#include <cstring>
#include <optional>
#include <string_view>

#include <everest/slac/fsm/evse/states/matching.hpp>

#include "../misc.hpp"

namespace slac::fsm::evse {

static auto create_cm_set_key_req(uint8_t const* session_nmk) {
    slac::messages::cm_set_key_req set_key_req;

    set_key_req.key_type = slac::defs::CM_SET_KEY_REQ_KEY_TYPE_NMK;
    set_key_req.my_nonce = 0x00000000;
    set_key_req.your_nonce = 0x00000000;
    set_key_req.pid = slac::defs::CM_SET_KEY_REQ_PID_HLE;
    set_key_req.prn = htole16(slac::defs::CM_SET_KEY_REQ_PRN_UNUSED);
    set_key_req.pmn = slac::defs::CM_SET_KEY_REQ_PMN_UNUSED;
    set_key_req.cco_capability = slac::defs::CM_SET_KEY_REQ_CCO_CAP_NONE;
    slac::utils::generate_nid_from_nmk(set_key_req.nid, session_nmk);
    set_key_req.new_eks = slac::defs::CM_SET_KEY_REQ_PEKS_NMK_KNOWN_TO_STA;
    memcpy(set_key_req.new_key, session_nmk, sizeof(set_key_req.new_key));

    return set_key_req;
}

void ResetState::enter() {
    ctx.log_info("Entered Reset state");
    if (ctx.slac_config.regenerate_key_on_reset) {
        generate_nmk(pending_nmk);
    } else {
        // We don't want to regenerate the key, just use the current key as the pending one
        memcpy(pending_nmk, ctx.slac_config.session_nmk, sizeof(pending_nmk));
    }
}

FSMSimpleState::HandleEventReturnType ResetState::handle_event(AllocatorType& sa, Event ev) {
    const auto& cfg = ctx.slac_config;
    if (ev == Event::SLAC_MESSAGE) {
        if (handle_slac_message(ctx.slac_message_payload)) {
            if (cfg.chip_reset.enabled) {
                // If chip reset is enabled in config, go to ResetChipState and from there to IdleState
                return sa.create_simple<ResetChipState>(ctx);
            } else {
                // If chip reset is disabled, go to IdleState directly
                return sa.create_simple<IdleState>(ctx);
            }
        } else {
            return sa.PASS_ON;
        }
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else if (ev == Event::FAILED) {
        if (cfg.chip_reset.enabled) {
            return sa.create_simple<ResetChipState>(ctx);
        } else {
            return sa.create_simple<IdleState>(ctx);
        }
    } else {
        return sa.PASS_ON;
    }
}

FSMSimpleState::CallbackReturnType ResetState::callback() {
    const auto& cfg = ctx.slac_config;
    const auto now = std::chrono::steady_clock::now();

    if (set_key_attempts > 0) {
        // A request is already in flight. We can get here either because the timeout elapsed
        // or because we were woken up early by a rejected/unexpected CNF. In the latter case we
        // wait for the remainder of the interval so that retries happen at a steady pace.
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_attempt_time).count();
        if (elapsed < cfg.set_key_timeout_ms) {
            return static_cast<int>(cfg.set_key_timeout_ms - elapsed);
        }

        if (set_key_attempts >= cfg.set_key_max_attempts) {
            ctx.log_error("CM_SET_KEY.REQ failed after " + std::to_string(set_key_attempts) +
                          " attempts - failed to setup NMK key");
            // Give up retrying, but don't stay stuck in ResetState. Proceed to Idle or ResetChipState with a
            // potentially inconsistent NMK rather than blocking the state machine indefinitely.
            return Event::FAILED;
        }

        ctx.log_warn("CM_SET_KEY.REQ not confirmed, retrying (attempt " + std::to_string(set_key_attempts + 1) + "/" +
                     std::to_string(cfg.set_key_max_attempts) + ")");
    } else {
        ctx.log_info("New NMK key: " + format_nmk(pending_nmk));
    }

    ctx.send_slac_message(cfg.plc_peer_mac, create_cm_set_key_req(pending_nmk));

    set_key_attempts++;
    last_attempt_time = now;

    return cfg.set_key_timeout_ms;
}

bool ResetState::handle_slac_message(slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();
    if (mmtype != (slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_CNF)) {
        // unexpected message
        // FIXME (aw): need to also deal with CM_VALIDATE.REQ. It is optional in the standard.
        ctx.log_warn("Received non-expected SLAC message of type " + format_mmtype(mmtype));
        return false;
    }

    const auto result = message.get_payload<slac::messages::cm_set_key_cnf>().result;
    if (result != slac::defs::CM_SET_KEY_CNF_RESULT_SUCCESS) {
        // Modem rejected the key. Stay in this state so callback() retries the request.
        ctx.log_warn("Received CM_SET_KEY.CNF reported failure (result=" + std::to_string(result) + ")");
        return false;
    }

    ctx.log_info("Received CM_SET_KEY.CNF");

    // CM_SET_KEY.CNF succeeded! Use it for the next session (and CM_SLAC_MATCH.CNF)
    memcpy(ctx.slac_config.session_nmk, pending_nmk, sizeof(ctx.slac_config.session_nmk));

    return true;
}

void ResetChipState::enter() {
    ctx.log_info("Entered HW Chip Reset state");
}

FSMSimpleState::HandleEventReturnType ResetChipState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        if (handle_slac_message(ctx.slac_message_payload)) {
            return sa.create_simple<IdleState>(ctx);
        } else {
            return sa.PASS_ON;
        }
    } else if (ev == Event::SUCCESS) {
        return sa.create_simple<IdleState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

FSMSimpleState::CallbackReturnType ResetChipState::callback() {
    const auto& cfg = ctx.slac_config;
    if (sub_state == SubState::DELAY) {
        sub_state = SubState::SEND_RESET;
        return cfg.chip_reset.delay_ms;

    } else if (sub_state == SubState::SEND_RESET) {

        if (ctx.modem_vendor == ModemVendor::Qualcomm) {
            slac::messages::qualcomm::cm_reset_device_req reset_req;
            ctx.log_info("Resetting HW Chip using RS_DEV.REQ");
            ctx.send_slac_message(cfg.plc_peer_mac, reset_req);
            sub_state = SubState::DONE;
            return cfg.chip_reset.timeout_ms;

        } else if (ctx.modem_vendor == ModemVendor::Lumissil) {
            slac::messages::lumissil::nscm_reset_device_req reset_req;
            ctx.log_info("Resetting HW Chip using NSCM_RESET_DEVICE.REQ");
            sub_state = SubState::DONE;
            ctx.send_slac_message(cfg.plc_peer_mac, reset_req);
            // CG5317 does not reply to the reset packet
            return Event::SUCCESS;

        } else {
            ctx.log_info("Chip reset not supported on this chip");
        }
    } else {
        ctx.log_error("Reset timeout, no response received - failed to reset the chip");
        return {};
    }
    return {};
}

bool ResetChipState::handle_slac_message(slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();
    if (mmtype != (slac::defs::qualcomm::MMTYPE_CM_RESET_DEVICE | slac::defs::MMTYPE_MODE_CNF)) {
        // unexpected message
        ctx.log_warn("Received non-expected SLAC message of type " + format_mmtype(mmtype));
        return false;
    } else {
        ctx.log_info("Received RS_DEV.CNF");
        return true;
    }
}

void IdleState::enter() {
    ctx.signal_state("UNMATCHED");
    ctx.log_info("Entered Idle state");
}

FSMSimpleState::HandleEventReturnType IdleState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::ENTER_BCD) {
        return sa.create_simple<MatchingState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

static std::optional<bool> check_link_status_cnf(const slac::fsm::evse::ModemVendor modem_vendor,
                                                 slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();
    if (modem_vendor == ModemVendor::Qualcomm &&
        mmtype == (slac::defs::qualcomm::MMTYPE_LINK_STATUS | slac::defs::MMTYPE_MODE_CNF)) {
        const auto success = message.get_payload<slac::messages::qualcomm::link_status_cnf>().link_status == 0x01;
        return {success};

    } else if (modem_vendor == ModemVendor::Lumissil &&
               mmtype == (slac::defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | slac::defs::MMTYPE_MODE_CNF)) {
        const auto success =
            message.get_payload<slac::messages::lumissil::nscm_get_d_link_status_cnf>().link_status == 0x01;
        return {success};
    }
    return {};
}

static bool send_link_status_req(slac::fsm::evse::Context& ctx) {
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
    ctx.signal_state("MATCHED");
    ctx.signal_dlink_ready(true);
    ctx.log_info("Entered Matched state");
}

FSMSimpleState::HandleEventReturnType MatchedState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        auto link_ok = check_link_status_cnf(ctx.modem_vendor, ctx.slac_message_payload);
        if (link_ok.has_value()) {
            if (link_ok.value()) {
                return sa.PASS_ON;
            } else {
                ctx.log_error("Connection lost in matched state");
                ctx.signal_error_routine_request();
                return sa.PASS_ON;
            }
        }
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    }
    return sa.PASS_ON;
}

FSMSimpleState::CallbackReturnType MatchedState::callback() {
    const auto& link_status = ctx.slac_config.link_status;

    if (not link_status.do_detect) {
        return {};
    }

    if (not link_status_req_sent) {
        link_status_req_sent = send_link_status_req(ctx);
    } else {
        // Link is confirmed not up yet, query again
        link_status_req_sent = false;
    }

    return link_status.poll_in_matched_state_ms;
}

void MatchedState::leave() {
    ctx.signal_dlink_ready(false);
}

void FailedState::enter() {
    if (ctx.slac_config.ac_mode_five_percent) {
        ctx.signal_error_routine_request();
    }
    ctx.log_info("Entered Failed state");
}

FSMSimpleState::HandleEventReturnType FailedState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

void WaitForLinkState::enter() {
    ctx.log_info("Waiting for Link to be ready...");
    start_time = std::chrono::steady_clock::now();
}

FSMSimpleState::HandleEventReturnType WaitForLinkState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        if (handle_slac_message(ctx.slac_message_payload)) {
            return sa.create_simple<MatchedState>(ctx);
        } else {
            return sa.PASS_ON;
        }
    } else if (ev == Event::RETRY_MATCHING) {
        ctx.log_error("Link could not be established");
        // Notify higher layers to on CP signal
        return sa.create_simple<FailedState>(ctx);
    } else if (ev == Event::RESET) {
        return sa.create_simple<ResetState>(ctx);
    } else {
        return sa.PASS_ON;
    }
}

FSMSimpleState::CallbackReturnType WaitForLinkState::callback() {
    const auto& cfg = ctx.slac_config;
    if (not link_status_req_sent) {
        link_status_req_sent = send_link_status_req(ctx);
        return cfg.link_status.retry_ms;
    } else {
        // Did we timeout?
        if (std::chrono::steady_clock::now() - start_time > std::chrono::milliseconds(cfg.link_status.timeout_ms)) {
            return Event::RETRY_MATCHING;
        }
        // Link is confirmed not up yet, query again
        link_status_req_sent = false;
        return cfg.link_status.retry_ms;
    }
}

WaitForLinkState::WaitForLinkState(Context& ctx,
                                   std::unique_ptr<slac::messages::cm_slac_match_cnf> sent_match_cnf_message) :
    FSMSimpleState(ctx), match_cnf_message(std::move(sent_match_cnf_message)) {
}

bool WaitForLinkState::handle_slac_message(slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();

    auto link_ok = check_link_status_cnf(ctx.modem_vendor, message);

    if (link_ok.has_value() and link_ok.value()) {
        return true;
    }

    if (mmtype == (slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ)) {
        // EV retries MATCH_REQ, so we send the CNF again
        ctx.log_info("Received CM_SLAC_MATCH.REQ retry from EV, sending out CM_SLAC_MATCH.CNF again.");
        ctx.send_slac_message(message.get_src_mac(), *match_cnf_message);
        return false;
    }
    return false;
}

void InitState::enter() {
    // Seed an initial NMK once at startup
    // This is the stable key that is used when regenerate_key_on_reset is disabled
    generate_nmk(ctx.slac_config.session_nmk);
}

FSMSimpleState::HandleEventReturnType InitState::handle_event(AllocatorType& sa, Event ev) {
    if (ev == Event::SLAC_MESSAGE) {
        handle_slac_message(ctx.slac_message_payload);
        return sa.PASS_ON;
    } else if (ev == Event::SUCCESS) {
        return sa.create_simple<ResetState>(ctx);
    }
    return sa.PASS_ON;
}

FSMSimpleState::CallbackReturnType InitState::callback() {
    const auto& cfg = ctx.slac_config;

    if (sub_state == SubState::QUALCOMM_OP_ATTR) {
        sub_state = SubState::LUMISSIL_GET_VERSION;
        slac::messages::qualcomm::op_attr_req op_attr_req;
        ctx.send_slac_message(cfg.plc_peer_mac, op_attr_req);
        return cfg.request_info_delay_ms;

    } else if (sub_state == SubState::LUMISSIL_GET_VERSION) {
        sub_state = SubState::DONE;
        slac::messages::lumissil::nscm_get_version_req version_req;
        ctx.send_slac_message(cfg.plc_peer_mac, version_req);
        return cfg.request_info_delay_ms;

    } else if (sub_state == SubState::DONE) {
        // the requested info may or may not be implemented by the chip,
        // so we ignore timeouts here.
        return Event::SUCCESS;
    }
    return {};
}

static std::string get_qualcomm_device_info(slac::messages::qualcomm::op_attr_cnf const& msg) {
    const auto get_string_view = [](auto const& raw) constexpr {
        static_assert(sizeof(uint8_t) == sizeof(char));
        return std::string_view(reinterpret_cast<char const*>(raw), sizeof(raw));
    };

    std::string result("Qualcomm PLC Device Attributes:");
    result += "\n  HW Platform: ";
    result += get_string_view(msg.hw_platform);
    result += "\n  SW Platform: ";
    result += get_string_view(msg.sw_platform);
    result += ("\n  Firmware: " + std::to_string(msg.version_major) + "." + std::to_string(msg.version_minor) + "." +
               std::to_string(msg.version_pib) + "." + std::to_string(msg.reserved) + "-" +
               std::to_string(msg.version_build));
    result += "\n  Build date: ";
    result += get_string_view(msg.build_date);

    result += "\n  ZC signal: ";

    // FIXME: no magic numbers
    const auto zc_signal = (msg.line_freq_zc >> 2) & 0x03;
    if (zc_signal == 0x01) {
        result += "Detected";
    } else if (zc_signal == 0x02) {
        result += "Missing";
    } else {
        result += ("Unknown (" + std::to_string(zc_signal) + ")");
    }

    result += "\n  Line frequency: ";

    const auto line_freq = (msg.line_freq_zc) & 0x03;
    if (line_freq == 0x01) {
        result += "50Hz";
    } else if (line_freq == 0x02) {
        result += "60Hz";
    } else {
        result += ("Unknown (" + std::to_string(line_freq) + ")");
    }

    return result;
}

static std::string get_lumissil_device_info(slac::messages::lumissil::nscm_get_version_cnf const& msg) {
    return "Lumissil PLC Device Firmware version: " + std::to_string(msg.version_major) + "." +
           std::to_string(msg.version_minor) + "." + std::to_string(msg.version_patch) + "." +
           std::to_string(msg.version_build);
}

void InitState::handle_slac_message(slac::messages::HomeplugMessage& message) {
    const auto mmtype = message.get_mmtype();
    if (mmtype == (slac::defs::qualcomm::MMTYPE_OP_ATTR | slac::defs::MMTYPE_MODE_CNF)) {
        const auto msg = message.get_payload<slac::messages::qualcomm::op_attr_cnf>();
        const auto device_info = get_qualcomm_device_info(msg);
        ctx.log_info(device_info);
        // This message is only supported on Qualcomm, so we can use it to detect the Vendor
        ctx.modem_vendor = ModemVendor::Qualcomm;

    } else if (mmtype == (slac::defs::lumissil::MMTYPE_NSCM_GET_VERSION | slac::defs::MMTYPE_MODE_CNF)) {
        const auto msg = message.get_payload<slac::messages::lumissil::nscm_get_version_cnf>();
        const auto device_info = get_lumissil_device_info(msg);
        ctx.log_info(device_info);
        // This message is only supported on Qualcomm, so we can use it to detect the Vendor
        ctx.modem_vendor = ModemVendor::Lumissil;
    }
}
} // namespace slac::fsm::evse
