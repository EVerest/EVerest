// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest

#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_defs.hpp>
#include <everest/slac/slac_messages.hpp>
#include <everest/slac/slac_types.hpp>

namespace everest::lib::slac::fsm::ev {

namespace _context_detail {

template <typename SlacMessageType> struct MMTYPE;
template <> struct MMTYPE<messages::cm_slac_parm_req> {
    static const std::uint16_t value = defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_REQ;
};
template <> struct MMTYPE<messages::cm_start_atten_char_ind> {
    static const std::uint16_t value = defs::MMTYPE_CM_START_ATTEN_CHAR | defs::MMTYPE_MODE_IND;
};
template <> struct MMTYPE<messages::cm_mnbc_sound_ind> {
    static const std::uint16_t value = defs::MMTYPE_CM_MNBC_SOUND | defs::MMTYPE_MODE_IND;
};
template <> struct MMTYPE<messages::cm_atten_char_rsp> {
    static const std::uint16_t value = defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_RSP;
};
template <> struct MMTYPE<messages::cm_slac_match_req> {
    static const std::uint16_t value = defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_REQ;
};
template <> struct MMTYPE<messages::cm_set_key_req> {
    static const std::uint16_t value = defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ;
};

template <typename SlacMessageType> struct MMV {
    // this is the default value for homeplug av 2.0 messages, which are
    // backward compatible with homeplug av 1.1 messages
    // non-backward (to 1.1) compatible message are CM_CHAN_EST,
    // CM_AMP_MAP and CM_NW_STATS, these need to use AV_2_0
    // older av 1.0 message need to use AV_1_0
    static constexpr auto value = defs::MMV::AV_1_1;
};

} // namespace _context_detail

struct ContextCallbacks {
    std::function<bool(messages::HomeplugMessage&)> send_raw_slac{nullptr};
    std::function<void(const std::string&)> signal_state{nullptr};
    std::function<void(bool)> signal_dlink_ready{nullptr};
    std::function<void(const std::string&)> log_debug{nullptr};
    std::function<void(const std::string&)> log_info{nullptr};
    std::function<void(const std::string&)> log_warn{nullptr};
    std::function<void(const std::string&)> log_error{nullptr};
};

struct EvSlacConfig {
    // timeout for CM_SET_KEY.REQ
    int set_key_timeout_ms = 500;
    // maximum amount of attempts to send CM_SLAC_PARM.REQ
    int parm_req_attempts = defs::C_EV_PARM_REQ_ATTEMPTS;
    // timeout for CM_SLAC_PARM.REQ
    int parm_req_timeout_ms = defs::TT_MATCH_RESPONSE_MS;
    // maximum amount of attempts to send CM_SLAC_MATCH.REQ
    int match_req_attempts = defs::C_EV_MATCH_RETRY;
    // timeout for CM_SLAC_MATCH.REQ
    int match_req_timeout_ms = defs::TT_MATCH_RESPONSE_MS;
};

struct SessionParameters {
    SessionParameters() = default;
    SessionParameters(RunId const& run_id_, MacAddress const& evse_mac_) : run_id(run_id_), evse_mac(evse_mac_) {
    }
    SessionParameters(std::uint8_t const* run_id_, std::uint8_t const* evse_mac_) :
        run_id(byte_array_from_wire<RunId>(run_id_)), evse_mac(byte_array_from_wire<MacAddress>(evse_mac_)) {
    }

    RunId run_id{};
    MacAddress evse_mac{};
};

struct Context {
    static constexpr MacAddress BROADCAST_MAC = {defs::BROADCAST_MAC_ADDRESS[0], defs::BROADCAST_MAC_ADDRESS[1],
                                                 defs::BROADCAST_MAC_ADDRESS[2], defs::BROADCAST_MAC_ADDRESS[3],
                                                 defs::BROADCAST_MAC_ADDRESS[4], defs::BROADCAST_MAC_ADDRESS[5]};
    static constexpr MacAddress EV_PLC_MAC = {0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    explicit Context(ContextCallbacks const& callbacks_, MacAddress const& ev_host_mac_) :
        ev_host_mac(ev_host_mac_), callbacks(callbacks_) {
    }
    explicit Context(ContextCallbacks const& callbacks_, std::uint8_t const* ev_host_mac_) :
        ev_host_mac(byte_array_from_wire<MacAddress>(ev_host_mac_)), callbacks(callbacks_) {
    }

    const MacAddress ev_host_mac{};
    EvSlacConfig slac_config{};

    // event specific payloads
    // FIXME (aw): due to the synchronous nature of the fsm, this could be even a ptr/ref
    messages::HomeplugMessage slac_message_payload;

    template <typename SlacMessageType> bool send_slac_message(MacAddress const& mac, SlacMessageType const& message) {
        if (not callbacks.send_raw_slac) {
            return false;
        }

        messages::HomeplugMessage hp_message;
        hp_message.setup_payload(&message, sizeof(message), _context_detail::MMTYPE<SlacMessageType>::value,
                                 _context_detail::MMV<SlacMessageType>::value);
        hp_message.set_destination(mac);

        return callbacks.send_raw_slac(hp_message);
    }

    template <typename SlacMessageType>
    bool send_slac_message(std::uint8_t const* mac, SlacMessageType const& message) {
        return send_slac_message(byte_array_from_wire<MacAddress>(mac), message);
    }

    void signal_state(const std::string& state) {
        if (callbacks.signal_state) {
            callbacks.signal_state(state);
        }
    }
    void signal_dlink_ready(bool value) {
        if (callbacks.signal_dlink_ready) {
            callbacks.signal_dlink_ready(value);
        }
    }

    void log_debug(const std::string& text) {
        if (callbacks.log_debug) {
            callbacks.log_debug(text);
        }
    }
    void log_info(const std::string& text) {
        if (callbacks.log_info) {
            callbacks.log_info(text);
        }
    }
    void log_warn(const std::string& text) {
        if (callbacks.log_warn) {
            callbacks.log_warn(text);
        }
    }
    void log_error(const std::string& text) {
        if (callbacks.log_error) {
            callbacks.log_error(text);
        }
    }

private:
    const ContextCallbacks& callbacks;
};

} // namespace everest::lib::slac::fsm::ev
