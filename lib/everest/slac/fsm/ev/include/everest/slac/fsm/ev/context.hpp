// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EV_SLAC_CONTEXT_HPP
#define EV_SLAC_CONTEXT_HPP

#include <chrono>
#include <functional>
#include <stdexcept>
#include <string>

#include <slac/slac.hpp>

namespace slac::fsm::ev {

namespace _context_detail {
template <typename SlacMessageType> struct MMTYPE;

template <> struct MMTYPE<slac::messages::cm_slac_parm_req> {
    static const uint16_t value = slac::defs::MMTYPE_CM_SLAC_PARAM | slac::defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<slac::messages::cm_start_atten_char_ind> {
    static const uint16_t value = slac::defs::MMTYPE_CM_START_ATTEN_CHAR | slac::defs::MMTYPE_MODE_IND;
};

template <> struct MMTYPE<slac::messages::cm_mnbc_sound_ind> {
    static const uint16_t value = slac::defs::MMTYPE_CM_MNBC_SOUND | slac::defs::MMTYPE_MODE_IND;
};

template <> struct MMTYPE<slac::messages::cm_atten_char_rsp> {
    static const uint16_t value = slac::defs::MMTYPE_CM_ATTEN_CHAR | slac::defs::MMTYPE_MODE_RSP;
};

template <> struct MMTYPE<slac::messages::cm_slac_match_req> {
    static const uint16_t value = slac::defs::MMTYPE_CM_SLAC_MATCH | slac::defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<slac::messages::cm_set_key_req> {
    static const uint16_t value = slac::defs::MMTYPE_CM_SET_KEY | slac::defs::MMTYPE_MODE_REQ;
};

// Vendor MMEs for modem identification and data-link supervision. Same set the
// EVSE side registers -- the host has to speak the modem maker's dialect.
template <> struct MMTYPE<slac::messages::qualcomm::op_attr_req> {
    static const uint16_t value = slac::defs::qualcomm::MMTYPE_OP_ATTR | slac::defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<slac::messages::qualcomm::link_status_req> {
    static const uint16_t value = slac::defs::qualcomm::MMTYPE_LINK_STATUS | slac::defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<slac::messages::lumissil::nscm_get_version_req> {
    static const uint16_t value = slac::defs::lumissil::MMTYPE_NSCM_GET_VERSION | slac::defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<slac::messages::lumissil::nscm_get_d_link_status_req> {
    static const uint16_t value = slac::defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | slac::defs::MMTYPE_MODE_REQ;
};

template <typename SlacMessageType> struct MMV {
    // this is the default value for homeplug av 2.0 messages, which are
    // backward compatible with homeplug av 1.1 messages
    // non-backward (to 1.1) compatible message are CM_CHAN_EST,
    // CM_AMP_MAP and CM_NW_STATS, these need to use AV_2_0
    // older av 1.0 message need to use AV_1_0
    static constexpr auto value = slac::defs::MMV::AV_1_1;
};

// Vendor MMEs are HomePlug AV 1.0 framed.
template <> struct MMV<slac::messages::qualcomm::op_attr_req> {
    static constexpr auto value = slac::defs::MMV::AV_1_0;
};

template <> struct MMV<slac::messages::qualcomm::link_status_req> {
    static constexpr auto value = slac::defs::MMV::AV_1_0;
};

template <> struct MMV<slac::messages::lumissil::nscm_get_version_req> {
    static constexpr auto value = slac::defs::MMV::AV_1_0;
};

template <> struct MMV<slac::messages::lumissil::nscm_get_d_link_status_req> {
    static constexpr auto value = slac::defs::MMV::AV_1_0;
};

} // namespace _context_detail

/// PLC modem makes differ in how the host asks for the data link state, so the
/// vendor has to be known before link detection can be used. Mirrors
/// slac::fsm::evse::ModemVendor.
enum class ModemVendor {
    Unknown,
    Qualcomm,
    Lumissil,
};

struct EvSlacConfig {
    /// MAC of the local PLC modem the host talks to.
    uint8_t plc_peer_mac[ETH_ALEN] = {0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    // Settings for LINK_STATUS detection, mirroring the EVSE side.
    struct link_status_struct {
        bool do_detect = false;
        int retry_ms = 100;
        int poll_in_matched_state_ms = 1000;
        int timeout_ms = 5000;
    } link_status;
};

struct ContextCallbacks {
    std::function<void(slac::messages::HomeplugMessage&)> send_raw_slac{nullptr};
    std::function<void(const std::string&)> signal_state{nullptr};
    std::function<void(const std::string&)> log_debug{nullptr};
    std::function<void(const std::string&)> log_info{nullptr};
    std::function<void(const std::string&)> log_warn{nullptr};
    std::function<void(const std::string&)> log_error{nullptr};
};

struct Context {
    static constexpr std::array<uint8_t, ETH_ALEN> BROADCAST_MAC = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static constexpr std::array<uint8_t, ETH_ALEN> EV_PLC_MAC = {0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    Context(const ContextCallbacks& callbacks_, const std::array<uint8_t, ETH_ALEN>& mac) :
        callbacks(callbacks_), ev_host_mac(mac) {
    }

    const std::array<uint8_t, ETH_ALEN> ev_host_mac{};

    EvSlacConfig slac_config;

    /// Detected from whichever vendor probe the local modem answers.
    ModemVendor modem_vendor{ModemVendor::Unknown};

    // Whether we told the local modem to adopt the EVSE's NMK, i.e. whether we
    // are in (or joining) its AVLN. Leaving one means resetting the NMK, so
    // there is nothing to do unless we joined ([V2G3-A09-121]).
    bool joined_avln{false};

    // [V2G3-A09-122]: TT_matching_repetition starts with the trigger of the
    // matching process and bounds how long a failed match keeps being retried.
    // Lives in the context because it spans states (trigger -> ... -> failed
    // -> retry -> ...), unlike the per-state timeouts.
    std::chrono::steady_clock::time_point matching_repetition_deadline{};

    // event specific payloads
    // FIXME (aw): due to the synchroneous nature of the fsm, this could be even a ptr/ref
    slac::messages::HomeplugMessage slac_message;

    // FIXME (aw): message should be const, but libslac doesn't allow for const ptr - needs changes in libslac
    template <typename SlacMessageType>
    void send_slac_message(const uint8_t* dest_mac, SlacMessageType const& message) {
        slac::messages::HomeplugMessage hp_message;
        hp_message.setup_ethernet_header(dest_mac);
        try {
            hp_message.setup_payload(&message, sizeof(message), _context_detail::MMTYPE<SlacMessageType>::value,
                                     _context_detail::MMV<SlacMessageType>::value);
        } catch (const std::runtime_error& e) {
            const auto error_message = std::string("Could not setup SLAC payload: ") + std::string(e.what());
            log_error(error_message);
        }
        callbacks.send_raw_slac(hp_message);
    }

    // signal handlers
    void signal_state(const std::string& state);

    // logging util
    void log_debug(const std::string& text);
    void log_info(const std::string& text);
    void log_warn(const std::string& text);
    void log_error(const std::string& text);

private:
    const ContextCallbacks& callbacks;
};

struct SessionParamaters {
    SessionParamaters(const uint8_t* run_id, const uint8_t* evse_mac);
    uint8_t run_id[slac::defs::RUN_ID_LEN];
    uint8_t evse_mac[ETH_ALEN];
};

} // namespace slac::fsm::ev

#endif // EV_SLAC_CONTEXT_HPP
