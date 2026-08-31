// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <everest/slac/protocol/defs.hpp>
#include <everest/slac/protocol/homeplug_message.hpp>
#include <everest/slac/protocol/messages.hpp>
#include <everest/slac/protocol/types.hpp>
#include <everest/slac/status.hpp>
#include <everest/slac/time.hpp>

namespace everest::slac::ev {

namespace detail {

template <typename T> struct MMTYPE;
template <> struct MMTYPE<messages::cm_slac_parm_req> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_SLAC_PARAM_REQ;
};
template <> struct MMTYPE<messages::cm_start_atten_char_ind> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_START_ATTEN_CHAR_IND;
};
template <> struct MMTYPE<messages::cm_mnbc_sound_ind> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_MNBC_SOUND_IND;
};
template <> struct MMTYPE<messages::cm_atten_char_rsp> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_ATTEN_CHAR_RSP;
};
template <> struct MMTYPE<messages::cm_slac_match_req> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_SLAC_MATCH_REQ;
};
template <> struct MMTYPE<messages::cm_set_key_req> {
    static constexpr std::uint16_t value = defs::MMTYPE_CM_SET_KEY_REQ;
};

/// The EV sends only HomePlug AV 1.1 messages; it never talks to the vendor MMEs.
template <typename T> struct MMV {
    static constexpr auto value = defs::MMV::AV_1_1;
};

} // namespace detail

struct Config {
    int set_key_timeout_ms = 500;
    /// CM_SLAC_PARM.REQ transmissions before matching fails. The ISO value (C_EV_match_retry) proved
    /// insufficient with some EVSEs in the field.
    int parm_req_attempts = defs::C_EV_PARM_REQ_ATTEMPTS;
    int parm_req_timeout_ms = defs::TT_MATCH_RESPONSE_MS;
    int match_req_attempts = defs::C_EV_MATCH_RETRY;
    int match_req_timeout_ms = defs::TT_MATCH_RESPONSE_MS;
};

/// Identifies the matching attempt in flight.
struct SessionParameters {
    RunId run_id{};
    MacAddress evse_mac{};
};

struct ContextCallbacks {
    std::function<bool(messages::HomeplugMessage&)> send_raw_slac{nullptr};
    std::function<TimePoint()> now{nullptr};

    std::function<void(D3State)> signal_state{nullptr};
    std::function<void(bool)> signal_dlink_ready{nullptr};

    std::function<void(std::string const&)> log_debug{nullptr};
    std::function<void(std::string const&)> log_info{nullptr};
    std::function<void(std::string const&)> log_warn{nullptr};
    std::function<void(std::string const&)> log_error{nullptr};
};

struct StateBase;
using BasePointerType = std::unique_ptr<StateBase>;

// The EV machine is flat, so its session data has nowhere else to live: the run id, the sounding
// counters and the retry counters all belong here.
class Context {
public:
    static constexpr MacAddress BROADCAST_MAC{defs::BROADCAST_MAC_ADDRESS[0], defs::BROADCAST_MAC_ADDRESS[1],
                                              defs::BROADCAST_MAC_ADDRESS[2], defs::BROADCAST_MAC_ADDRESS[3],
                                              defs::BROADCAST_MAC_ADDRESS[4], defs::BROADCAST_MAC_ADDRESS[5]};
    static constexpr MacAddress EV_PLC_MAC{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};

    Context(ContextCallbacks const& callbacks, MacAddress const& ev_host_mac_) :
        ev_host_mac(ev_host_mac_), m_callbacks(callbacks) {
    }

    template <typename StateType, typename... Args> BasePointerType create_state(Args&&... args) {
        return std::make_unique<StateType>(*this, std::forward<Args>(args)...);
    }

    TimePoint current_time{};
    TimePoint now() const {
        return m_callbacks.now();
    }

    MacAddress const ev_host_mac{};
    Config slac_config{};

    SessionParameters active_session{};
    Nmk pending_nmk{};
    int start_atten_char_count{0};
    int mnbc_sound_count{0};
    int parm_req_attempt_count{0};
    int match_req_attempt_count{0};

    D3State d3_state{D3State::Unmatched};

    /// Used whenever the machine returns to Reset.
    void clear_session() {
        parm_req_attempt_count = 0;
        match_req_attempt_count = 0;
        active_session = {};
        start_atten_char_count = 0;
        mnbc_sound_count = 0;
        pending_nmk.fill(0);
    }

    bool has_parm_req_attempts_left() const {
        return parm_req_attempt_count < slac_config.parm_req_attempts;
    }
    bool has_match_req_attempts_left() const {
        return match_req_attempt_count < slac_config.match_req_attempts;
    }
    bool all_sounding_messages_sent() const {
        return start_atten_char_count >= defs::C_EV_START_ATTEN_CHAR_INDS and mnbc_sound_count >= defs::C_EV_MATCH_MNBC;
    }

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

    void signal_dlink_ready(bool value) const {
        if (m_callbacks.signal_dlink_ready) {
            m_callbacks.signal_dlink_ready(value);
        }
    }

    /// At most once per logical transition.
    void publish_slac_state() {
        if (m_last_published_d3_state.has_value() and *m_last_published_d3_state == d3_state) {
            return;
        }
        m_last_published_d3_state = d3_state;
        if (m_callbacks.signal_state) {
            m_callbacks.signal_state(d3_state);
        }
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

private:
    ContextCallbacks const& m_callbacks;
    std::optional<D3State> m_last_published_d3_state{};
};

} // namespace everest::slac::ev
