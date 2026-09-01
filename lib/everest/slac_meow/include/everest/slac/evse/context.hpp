// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <everest/slac/evse/config.hpp>
#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/format.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/types.hpp>
#include <everest/slac/status.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::evse {

namespace detail {

/// The MMTYPE and version each outgoing message is sent with.
template <typename T> struct MMTYPE;
template <> struct MMTYPE<messages::cm_slac_parm_cnf> {
    static constexpr std::uint16_t value = defs::mmtype::SLAC_PARAM_CNF;
};
template <> struct MMTYPE<messages::cm_atten_char_ind> {
    static constexpr std::uint16_t value = defs::mmtype::ATTEN_CHAR_IND;
};
template <> struct MMTYPE<messages::cm_set_key_req> {
    static constexpr std::uint16_t value = defs::mmtype::SET_KEY_REQ;
};
template <> struct MMTYPE<messages::cm_amp_map_cnf> {
    static constexpr std::uint16_t value = defs::mmtype::AMP_MAP_CNF;
};
template <> struct MMTYPE<messages::cm_validate_cnf> {
    static constexpr std::uint16_t value = defs::mmtype::VALIDATE_CNF;
};
template <> struct MMTYPE<messages::cm_slac_match_cnf> {
    static constexpr std::uint16_t value = defs::mmtype::SLAC_MATCH_CNF;
};
template <> struct MMTYPE<messages::qualcomm::cm_reset_device_req> {
    static constexpr std::uint16_t value = defs::mmtype::qualcomm::RESET_DEVICE_REQ;
};
template <> struct MMTYPE<messages::qualcomm::link_status_req> {
    static constexpr std::uint16_t value = defs::mmtype::qualcomm::LINK_STATUS_REQ;
};
template <> struct MMTYPE<messages::qualcomm::op_attr_req> {
    static constexpr std::uint16_t value = defs::mmtype::qualcomm::OP_ATTR_REQ;
};
template <> struct MMTYPE<messages::lumissil::nscm_reset_device_req> {
    static constexpr std::uint16_t value = defs::mmtype::lumissil::RESET_DEVICE_REQ;
};
template <> struct MMTYPE<messages::lumissil::nscm_get_version_req> {
    static constexpr std::uint16_t value = defs::mmtype::lumissil::GET_VERSION_REQ;
};
template <> struct MMTYPE<messages::lumissil::nscm_get_d_link_status_req> {
    static constexpr std::uint16_t value = defs::mmtype::lumissil::GET_D_LINK_STATUS_REQ;
};

/// HomePlug AV 1.1 by default; the vendor MMEs predate it and use 1.0.
template <typename T> struct MMV {
    static constexpr auto value = defs::MMV::AV_1_1;
};
/// CM_AMP_MAP is not backward compatible with AV 1.1 and has to be framed AV 2.0.
template <> struct MMV<messages::cm_amp_map_cnf> {
    static constexpr auto value = defs::MMV::AV_2_0;
};
template <> struct MMV<messages::qualcomm::cm_reset_device_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};
template <> struct MMV<messages::qualcomm::link_status_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};
template <> struct MMV<messages::qualcomm::op_attr_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};
template <> struct MMV<messages::lumissil::nscm_reset_device_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};
template <> struct MMV<messages::lumissil::nscm_get_version_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};
template <> struct MMV<messages::lumissil::nscm_get_d_link_status_req> {
    static constexpr auto value = defs::MMV::AV_1_0;
};

} // namespace detail

// The machine reads no clock, opens no socket and logs nowhere by itself; it calls these.
struct ContextCallbacks {
    std::function<bool(messages::HomeplugMessage&)> send_raw_slac{nullptr};
    std::function<TimePoint()> now{nullptr};

    /// Sampled when a CM_VALIDATE exchange needs it, not reacted to. Optional: without it,
    /// validation reports no toggles rather than failing.
    std::function<int()> bc_transition_count{nullptr};

    std::function<void(D3State)> signal_state{nullptr};
    std::function<void(bool)> signal_dlink_ready{nullptr};
    std::function<void()> signal_error_routine_request{nullptr};
    std::function<void(std::string const&)> signal_ev_mac_address_parm_req{nullptr};
    std::function<void(std::string const&)> signal_ev_mac_address_match_cnf{nullptr};

    std::function<void(std::string const&)> log_debug{nullptr};
    std::function<void(std::string const&)> log_info{nullptr};
    std::function<void(std::string const&)> log_warn{nullptr};
    std::function<void(std::string const&)> log_error{nullptr};

    std::function<void(std::string const& block, std::string const& key, std::string const& value)> pub_telemetry{
        nullptr};
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// fsm::v2 destroys a state on transition, so whatever outlives a single state lives here. Per-phase
// data stays on the composite states themselves - Reset owns its pending NMK, Matching its sessions.
class Context {
public:
    Context(ContextCallbacks const& callbacks) : m_callbacks(callbacks) {
    }

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    /// Stamped by the facade once per event, so everything handling it agrees on when "now" is.
    TimePoint current_time{};
    TimePoint now() const {
        return m_callbacks.now();
    }

    /// 0 if the charge controller supplies no count.
    int bc_transition_count() const {
        return m_callbacks.bc_transition_count ? m_callbacks.bc_transition_count() : 0;
    }

    Config slac_config{};
    /// After a CM_VALIDATE the match request must arrive within TT_match_sequence rather than the
    /// whole match session: CmSlacMatch_003/004, cmValidate variant.
    bool validation_done{false};
    Timer validation_match_window{};

    Status status{};

    defs::ModemVendor modem_vendor{defs::ModemVendor::Unknown};
    MacAddress evse_mac{};

    /// The last CM_SLAC_MATCH.CNF sent, kept so a retransmitted request can be answered from it.
    struct MatchConfirmCache {
        bool valid{false};
        messages::cm_slac_match_cnf message{};
        MacAddress ev_mac{};
        MacAddress evse_mac{};
        RunId run_id{};
    } match_confirm_cache;

    template <typename SlacMessageType> bool send_slac_message(MacAddress const& mac, SlacMessageType const& message) {
        if (not m_callbacks.send_raw_slac) {
            return false;
        }
        messages::HomeplugMessage hp_message;
        hp_message.setup_payload(&message, sizeof(message), detail::MMTYPE<SlacMessageType>::value,
                                 detail::MMV<SlacMessageType>::value);
        hp_message.set_destination(mac);
        return m_callbacks.send_raw_slac(hp_message);
    }
    template <typename SlacMessageType>
    bool send_slac_message(std::uint8_t const* mac, SlacMessageType const& message) {
        return send_slac_message(byte_array_from_wire<MacAddress>(mac), message);
    }

    /// The variable-length amplitude map does not fit the fixed-struct path above, so it is framed
    /// here. am_data holds ceil(am_len / 2) pre-packed bytes.
    bool send_amp_map_req(MacAddress const& mac, std::uint16_t am_len, std::vector<std::uint8_t> const& am_data) {
        if (not m_callbacks.send_raw_slac) {
            return false;
        }
        std::vector<std::uint8_t> payload;
        payload.reserve(sizeof(std::uint16_t) + am_data.size());
        payload.push_back(static_cast<std::uint8_t>(am_len & 0xFF));
        payload.push_back(static_cast<std::uint8_t>((am_len >> 8) & 0xFF));
        payload.insert(payload.end(), am_data.begin(), am_data.end());

        messages::HomeplugMessage hp_message;
        hp_message.setup_payload(payload.data(), payload.size(), defs::mmtype::AMP_MAP_REQ, defs::MMV::AV_2_0);
        hp_message.set_destination(mac);
        return m_callbacks.send_raw_slac(hp_message);
    }

    void signal_cm_slac_parm_req(std::uint8_t const* ev_mac) {
        if (m_callbacks.signal_ev_mac_address_parm_req) {
            m_callbacks.signal_ev_mac_address_parm_req(format_mac_addr(ev_mac));
        }
    }
    void signal_cm_slac_match_cnf(std::uint8_t const* ev_mac) {
        if (m_callbacks.signal_ev_mac_address_match_cnf) {
            m_callbacks.signal_ev_mac_address_match_cnf(format_mac_addr(ev_mac));
        }
    }
    void signal_dlink_ready(bool value) {
        if (m_callbacks.signal_dlink_ready) {
            m_callbacks.signal_dlink_ready(value);
        }
    }
    void signal_error_routine_request() {
        if (m_callbacks.signal_error_routine_request) {
            m_callbacks.signal_error_routine_request();
        }
    }

    /// Publish status.d3_state, at most once per logical transition.
    void publish_slac_state() {
        if (m_last_published_d3_state.has_value() and *m_last_published_d3_state == status.d3_state) {
            return;
        }
        m_last_published_d3_state = status.d3_state;
        if (m_callbacks.signal_state) {
            m_callbacks.signal_state(status.d3_state);
        }
    }

    void clear_match_confirm_cache() {
        match_confirm_cache = MatchConfirmCache{};
    }
    void cache_match_confirm_message(messages::cm_slac_match_cnf const& message, MacAddress const& ev_mac,
                                     MacAddress const& evse_mac, RunId const& run_id) {
        match_confirm_cache.valid = true;
        match_confirm_cache.message = message;
        match_confirm_cache.ev_mac = ev_mac;
        match_confirm_cache.evse_mac = evse_mac;
        match_confirm_cache.run_id = run_id;
    }

    void log_debug(std::string const& text) const {
        if (m_callbacks.log_debug) {
            m_callbacks.log_debug(text);
        }
    }
    void log_info(std::string const& text) const {
        if (m_callbacks.log_info) {
            m_callbacks.log_info(text);
        }
    }
    void log_warn(std::string const& text) const {
        if (m_callbacks.log_warn) {
            m_callbacks.log_warn(text);
        }
    }
    void log_error(std::string const& text) const {
        if (m_callbacks.log_error) {
            m_callbacks.log_error(text);
        }
    }
    void telemetry(std::string const& block, std::string const& key, std::string const& value) const {
        if (m_callbacks.pub_telemetry) {
            m_callbacks.pub_telemetry(block, key, value);
        }
    }

private:
    ContextCallbacks const& m_callbacks;
    std::optional<D3State> m_last_published_d3_state{};
};

} // namespace everest::slac::evse
