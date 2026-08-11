// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 - 2023 Pionix GmbH and Contributors to EVerest
#ifndef EVSE_SLAC_CONTEXT_HPP
#define EVSE_SLAC_CONTEXT_HPP

#include "everest/slac/slac_messages.hpp"
#include <array>
#include <functional>
#include <optional>
#include <string>

#include <everest/slac/EvseSlacConfig.hpp>
#include <everest/slac/slac.hpp>
#include <everest/slac/slac_types.hpp>
#include <everest/slac/telemetry.hpp>

namespace everest::lib::slac::fsm::evse {

namespace _context_detail {

template <typename SlacMessageType> struct MMTYPE;
template <> struct MMTYPE<messages::cm_slac_parm_cnf> {
    static const uint16_t value = defs::MMTYPE_CM_SLAC_PARAM | defs::MMTYPE_MODE_CNF;
};
template <> struct MMTYPE<messages::cm_atten_char_ind> {
    static const uint16_t value = defs::MMTYPE_CM_ATTEN_CHAR | defs::MMTYPE_MODE_IND;
};
template <> struct MMTYPE<messages::cm_set_key_req> {
    static const uint16_t value = defs::MMTYPE_CM_SET_KEY | defs::MMTYPE_MODE_REQ;
};
template <> struct MMTYPE<messages::cm_validate_cnf> {
    static const uint16_t value = defs::MMTYPE_CM_VALIDATE | defs::MMTYPE_MODE_CNF;
};
template <> struct MMTYPE<messages::cm_slac_match_cnf> {
    static const uint16_t value = defs::MMTYPE_CM_SLAC_MATCH | defs::MMTYPE_MODE_CNF;
};

template <> struct MMTYPE<messages::qualcomm::cm_reset_device_req> {
    static const uint16_t value = defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::qualcomm::cm_reset_device_cnf> {
    static const uint16_t value = defs::qualcomm::MMTYPE_CM_RESET_DEVICE | defs::MMTYPE_MODE_CNF;
};

template <> struct MMTYPE<messages::qualcomm::link_status_req> {
    static const uint16_t value = defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::qualcomm::link_status_cnf> {
    static const uint16_t value = defs::qualcomm::MMTYPE_LINK_STATUS | defs::MMTYPE_MODE_CNF;
};

template <> struct MMTYPE<messages::qualcomm::op_attr_req> {
    static const uint16_t value = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::qualcomm::op_attr_cnf> {
    static const uint16_t value = defs::qualcomm::MMTYPE_OP_ATTR | defs::MMTYPE_MODE_CNF;
};

// This message has no CNF counterpart
template <> struct MMTYPE<messages::lumissil::nscm_reset_device_req> {
    static const uint16_t value = defs::lumissil::MMTYPE_NSCM_RESET_DEVICE | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::lumissil::nscm_get_version_req> {
    static const uint16_t value = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::lumissil::nscm_get_version_cnf> {
    static const uint16_t value = defs::lumissil::MMTYPE_NSCM_GET_VERSION | defs::MMTYPE_MODE_CNF;
};

template <> struct MMTYPE<messages::lumissil::nscm_get_d_link_status_req> {
    static const uint16_t value = defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_REQ;
};

template <> struct MMTYPE<messages::lumissil::nscm_get_d_link_status_cnf> {
    static const uint16_t value = defs::lumissil::MMTYPE_NSCM_GET_D_LINK_STATUS | defs::MMTYPE_MODE_CNF;
};

template <typename SlacMessageType> struct MMV {
    // this is the default value for homeplug av 2.0 messages, which are
    // backward compatible with homeplug av 1.1 messages
    // non-backward (to 1.1) compatible message are CM_CHAN_EST,
    // CM_AMP_MAP and CM_NW_STATS, these need to use AV_2_0
    // older av 1.0 message need to use AV_1_0
    static constexpr auto value = defs::MMV::AV_1_1;
};

template <> struct MMV<messages::qualcomm::cm_reset_device_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::qualcomm::cm_reset_device_cnf> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::qualcomm::link_status_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::qualcomm::link_status_cnf> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::qualcomm::op_attr_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::qualcomm::op_attr_cnf> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

template <> struct MMV<messages::lumissil::nscm_reset_device_req> {
    static constexpr auto value = defs::MMV::AV_1_0; // FIXME this is unclear
};

template <> struct MMV<messages::lumissil::nscm_get_version_req> {
    static constexpr auto value = defs::MMV::AV_1_0; // FIXME this is unclear
};

template <> struct MMV<messages::lumissil::nscm_get_version_cnf> {
    static constexpr auto value = defs::MMV::AV_1_0; // FIXME this is unclear
};

template <> struct MMV<messages::lumissil::nscm_get_d_link_status_req> {
    static constexpr auto value = defs::MMV::AV_1_0; // FIXME this is unclear
};

template <> struct MMV<messages::lumissil::nscm_get_d_link_status_cnf> {
    static constexpr auto value = defs::MMV::AV_1_0; // FIXME this is unclear
};

} // namespace _context_detail

struct ContextCallbacks {
    std::function<bool(messages::HomeplugMessage&)> send_raw_slac{nullptr};
    std::function<void(D3State)> signal_state{nullptr};
    std::function<void(bool)> signal_dlink_ready{nullptr};
    std::function<void()> signal_error_routine_request{nullptr};
    std::function<void(const std::string&)> signal_ev_mac_address_parm_req{nullptr};
    std::function<void(const std::string&)> signal_ev_mac_address_match_cnf{nullptr};
    std::function<void(const std::string&)> log_debug{nullptr};
    std::function<void(const std::string&)> log_info{nullptr};
    std::function<void(const std::string&)> log_warn{nullptr};
    std::function<void(const std::string&)> log_error{nullptr};
    std::function<void(const std::string&, const std::string&, const std::string&)> pub_telemetry{nullptr};
};

struct Context {
    explicit Context(const ContextCallbacks& callbacks_) : callbacks(callbacks_){};

    struct MatchConfirmCache {
        bool valid{false};
        messages::cm_slac_match_cnf message{};
        MacAddress ev_mac{};
        MacAddress evse_mac{};
        RunId run_id{};
    };

    EvseSlacConfig slac_config{};

    // event specific payloads
    // FIXME (aw): due to the synchroneous nature of the fsm, this could be even a ptr/ref
    messages::HomeplugMessage slac_message_payload;

    // FIXME (aw): message should be const, but libslac doesn't allow for const ptr - needs changes in libslac
    template <typename SlacMessageType> bool send_slac_message(MacAddress const& mac, SlacMessageType const& message) {
        messages::HomeplugMessage hp_message;
        hp_message.setup_payload(&message, sizeof(message), _context_detail::MMTYPE<SlacMessageType>::value,
                                 _context_detail::MMV<SlacMessageType>::value);
        hp_message.set_destination(mac);
        if (not callbacks.send_raw_slac) {
            return false;
        }
        return callbacks.send_raw_slac(hp_message);
    }
    template <typename SlacMessageType> bool send_slac_message(uint8_t const* mac, SlacMessageType const& message) {
        return send_slac_message(byte_array_from_wire<MacAddress>(mac), message);
    }

    // signal handlers
    void signal_cm_slac_parm_req(const uint8_t* ev_mac);
    void signal_cm_slac_match_cnf(const uint8_t* ev_mac);
    void signal_dlink_ready(bool value);
    void signal_error_routine_request();
    // Publishes the coarse public SLAC state (D3State) to the consumer, deriving it from status.d3_state.
    // Deduplicates consecutive identical states so a single logical transition emits at most one signal.
    void publish_slac_state();
    void clear_match_confirm_cache();
    void cache_match_confirm_message(messages::cm_slac_match_cnf const& match_confirm_message, uint8_t const* ev_mac,
                                     uint8_t const* evse_mac, uint8_t const* run_id);
    void cache_match_confirm_message(messages::cm_slac_match_cnf const& match_confirm_message, MacAddress const& ev_mac,
                                     MacAddress const& evse_mac, RunId const& run_id);

    // logging util
    void log_debug(const std::string& text);
    void log_info(const std::string& text);
    void log_warn(const std::string& text);
    void log_error(const std::string& text);
    void telemetry(const std::string& block, const std::string& key, const std::string& value);

    defs::ModemVendor modem_vendor{defs::ModemVendor::Unknown};
    MacAddress evse_mac{};
    MatchConfirmCache match_confirm_cache;

    SlacTelemetry status;

private:
    const ContextCallbacks& callbacks;
    // Last D3State handed to signal_state, used to suppress duplicate publications.
    std::optional<D3State> last_published_d3_state{};
};

} // namespace everest::lib::slac::fsm::evse

#endif // EVSE_SLAC_CONTEXT_HPP
