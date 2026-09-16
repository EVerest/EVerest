// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "protocol/cb_common.h"
#include "protocol/evse_bsp_cb_to_host.h"
#include <charge_bridge/everest_api/ev_bsp_api.hpp>
#include <charge_bridge/mcs_bsp.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <chrono>
#include <cstring>
#include <everest_api_types/ev_board_support/codec.hpp>
#include <everest_api_types/evse_board_support/codec.hpp>
#include <everest_api_types/generic/codec.hpp>
#include <everest_api_types/utilities/codec.hpp>

#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

using namespace std::chrono_literals;
using namespace everest::lib::API::V1_0::types::generic;
using namespace everest::lib::API;

namespace charge_bridge::evse_bsp {

namespace {
// There is exactly one source for the communication fault, so it does not need a sub_type to
// tell instances apart. Raise and clear must agree on it, otherwise the clear does not match
// the raised instance.
constexpr auto comm_fault_subtype = "";
} // namespace

ev_bsp_api::ev_bsp_api([[maybe_unused]] evse_ev_bsp_config const& config, std::string const& cb_identifier,
                       evse_bsp_host_to_cb& host_status) :
    host_status(host_status), m_cb_identifier(cb_identifier) {

    last_everest_heartbeat = std::chrono::steady_clock::time_point();
}

void ev_bsp_api::sync(bool cb_connected) {
    m_cb_connected = cb_connected;
    handle_everest_connection_state();
}

bool ev_bsp_api::register_events([[maybe_unused]] everest::lib::io::event::fd_event_handler& handler) {
    return true;
}

bool ev_bsp_api::unregister_events([[maybe_unused]] everest::lib::io::event::fd_event_handler& handler) {
    return true;
}

void ev_bsp_api::set_cb_tx(tx_ftor const& handler) {
    m_tx = handler;
}

void ev_bsp_api::tx(evse_bsp_host_to_cb const& msg) {
    if (m_tx) {
        m_tx(msg);
    }
}

void ev_bsp_api::set_mqtt_tx(mqtt_ftor const& handler) {
    m_mqtt_tx = handler;
}

void ev_bsp_api::send_bsp_event(API_EVSE_BSP::Event data) {
    API_EVSE_BSP::BspEvent event{data};
    send_mqtt("bsp_event", serialize(event));
}

void ev_bsp_api::send_bsp_measurement(API_EV_BSP::BspMeasurement data) {
    API_EV_BSP::BspMeasurement measurement{data};
    send_mqtt("bsp_measurement", serialize(measurement));
}

void ev_bsp_api::handle_event_relay(std::uint8_t relay) {
    using bc_event = API_EVSE_BSP::Event;
    bc_event relaise_event;
    bool relaise_state_valid = true;
    switch (relay) {
    case RelaiseState::RelayState_Open:
        relaise_event = bc_event::PowerOff;
        break;
    case RelaiseState::RelayState_Closed:
        relaise_event = bc_event::PowerOn;
        break;
    default:
        relaise_state_valid = false;
    }
    if (relaise_state_valid) {
        send_bsp_event(relaise_event);
    }
}

void ev_bsp_api::handle_event_cp(std::uint8_t cp) {
    using bc_event = API_EVSE_BSP::Event;
    bc_event cp_event;
    bool cp_state_valid = true;
    switch (cp) {
    case CpState_A:
        cp_event = bc_event::A;
        break;
    case CpState_B:
        cp_event = bc_event::B;
        break;
    case CpState_C:
        cp_event = bc_event::C;
        break;
    case CpState_D:
        cp_event = bc_event::D;
        break;
    case CpState_E:
        cp_event = bc_event::Disconnected;
        break;
    case CpState_F:
        cp_event = bc_event::F;
        break;
    case CpState_DF:
        cp_event = bc_event::E;
        break;
    case CpState::CpState_INVALID:
        cp_event = bc_event::E;
        break;
    default:
        cp_state_valid = false;
    }
    if (cp_state_valid) {
        last_cp_event = cp_event;
        send_bsp_event(cp_event);
    }
}

void ev_bsp_api::handle_bsp_measurement(uint16_t cp, [[maybe_unused]] uint8_t pp_1, [[maybe_unused]] uint8_t pp2) {
    // FIXME implement PP correctly
    API_EV_BSP::BspMeasurement data;
    // The MCU reports the duty as a 16-bit fraction; EVerest wants percent. In EV role on MCS the
    // MCU synthesizes a fixed 3276 -> 4.9988 %, the 5 % HLC marker of IEC 61851-23-3 Annex CC, rather
    // than an ampacity code.
    //
    // That clears the gate that matters for a DC session: EvManager's iso_wait_pwm_is_running
    // requires > 4.0 % (car_simulation.cpp:187). It does NOT clear the two 7 % gates in the same file
    // (:42 CHARGING_REGULATED, :182 iec_wait_pwr_ready) - and must not be read as a contradiction,
    // because those are IEC AC basic-signalling paths where the duty encodes an available current.
    // An MCS session is DC and HLC-only, so it never runs them.
    data.cp_pwm_duty_cycle = cp / 65536. * 100.;
    API_EVSE_BSP::ProximityPilot pp;
    API_EVSE_BSP::Ampacity amp;
    amp = API_EVSE_BSP::Ampacity::None;
    pp.ampacity = amp;
    data.proximity_pilot = pp;
    send_bsp_measurement(data);
}

inline static bool operator==(const SafetyErrorFlags& a, const SafetyErrorFlags& b) {
    return a.raw == b.raw;
}
inline static bool operator!=(const SafetyErrorFlags& a, const SafetyErrorFlags& b) {
    return a.raw != b.raw;
}

void ev_bsp_api::set_cb_message(evse_bsp_cb_to_host const& msg) {

    if (m_cb_status.cp_state not_eq msg.cp_state) {
        handle_event_cp(msg.cp_state);
    }

    if (m_cb_status.relay_state != msg.relay_state) {
        handle_event_relay(msg.relay_state);
    }

    if (m_cb_status.cp_duty_cycle not_eq msg.cp_duty_cycle or m_cb_status.pp_state_type1 not_eq msg.pp_state_type1 or
        m_cb_status.pp_state_type2 not_eq msg.pp_state_type2) {
        handle_bsp_measurement(msg.cp_duty_cycle, m_cb_status.pp_state_type1, m_cb_status.pp_state_type2);
    }

    if (m_cb_status.error_flags not_eq msg.error_flags) {
        handle_error(msg.error_flags);
    }

    // This is not supported in EVerest yet but should be added at some point
    /*
    if (cb_status.stop_charging not_eq msg.stop_charging) {
        handle_stop_button(msg.stop_charging);
    }*/

    // The ev_board_support interface in EVerest does not yet have proper errors defined, so we do not handle errors
    // here yet
    /*
    if (m_cb_status.error_flags not_eq msg.error_flags) {
        handle_error(msg.error_flags);
    }*/

    m_cb_status = msg;
}

// The bit positions live in charge_bridge/mcs_bsp.hpp. They were duplicated here and in
// evse_bsp_api.cpp, and both copies had fallen behind the wire header - which is how ce_fault and
// id_fault came to be dropped on both interfaces. What this file does with a bit still differs: the
// EV board support interface has no error surface in EVerest yet, so these are rendered into a log
// line only.
using safety_error_mask = charge_bridge::safety_error_mask;

// Table that maps a mask to our API error + message
struct FlagSpec {
    safety_error_mask mask;
    const char* message;
};

static constexpr FlagSpec error_specs[] = {
    {safety_error_mask::pp_invalid, "PP invalid"},
    {safety_error_mask::plug_temperature_too_high, "Plug temperature too high"},
    {safety_error_mask::internal_temperature_too_high, "ChargeBridge internal over temperature"},
    {safety_error_mask::emergency_input_latched, "Emergency input latched"},
    {safety_error_mask::relay_health_latched, "Relay welded error"},
    {safety_error_mask::vdd_3v3_out_of_range, "Supply voltage 3.3V out of range"},
    {safety_error_mask::vdd_core_out_of_range, "Internal supply core voltage out of range"},
    {safety_error_mask::vdd_12V_out_of_range, "Supply 12V (CCS) / 5V front end (MCS) voltage out of range"},
    {safety_error_mask::vdd_N12V_out_of_range, "Internal supply -12V voltage out of range"},
    {safety_error_mask::vdd_refint_out_of_range, "Internal supply VREF voltage out of range"},
    {safety_error_mask::config_mem_error, "Internal config memory error"},
    {safety_error_mask::dc_hv_ov_emergency, "DC HV OVM. FIXME: This should be on OVM not EVSE interface"},
    {safety_error_mask::rcd_error, "RCD error detected"},
    // MCS basic signalling. Without these two the log line for a CE or ID emergency came out empty,
    // and an empty line is read below as "nothing wrong" - so an MCS emergency printed "Relays can be
    // switched on." while the MCU had just opened S V3.
    {safety_error_mask::ce_fault, "MCS Charge Enable signal integrity lost"},
    {safety_error_mask::id_fault, "MCS Insertion Detection lost"},
};

static constexpr FlagSpec print_warning_specs[] = {
    {safety_error_mask::cp_not_state_c, "CP is not state C"},
    {safety_error_mask::pwm_not_enabled, "PWM not enabled"},
    {safety_error_mask::external_allow_power_on, "Allow power on from EVerest missing"},
};

void ev_bsp_api::handle_error(const SafetyErrorFlags& data) {
    std::uint32_t next = data.raw; // current raw value
    std::stringstream log;

    for (const auto& s : print_warning_specs) {
        if (next & static_cast<std::uint32_t>(s.mask)) {
            log << "[" << s.message << "] ";
        }
    }

    for (const auto& s : error_specs) {
        if (next & static_cast<std::uint32_t>(s.mask)) {
            log << "[" << s.message << "] ";
        }
    }

    if (m_everest_connected && m_cb_connected) {
        if (log.str().empty()) {
            utilities::print_error(m_cb_identifier, "EV/EVEREST", 0) << "Relays can be switched on." << std::endl;
        } else {
            utilities::print_error(m_cb_identifier, "EV/EVEREST", 0) << "Relays off due to:" << log.str() << std::endl;
        }
    }
}

void ev_bsp_api::dispatch(std::string const& operation, std::string const& payload) {
    if (operation == "enable") {
        receive_enable(payload);
    } else if (operation == "set_cp_state") {
        receive_set_cp_state(payload);
    } else if (operation == "allow_power_on") {
        receive_allow_power_on(payload);
    } else if (operation == "diode_fail") {
        receive_diode_fail(payload);
    } else if (operation == "set_ac_max_current") {
        receive_set_ac_max_current(payload);
    } else if (operation == "set_three_phases") {
        receive_set_three_phases(payload);
    } else if (operation == "set_rcd_error") {
        receive_set_rcd_error(payload);
    } else if (operation == "heartbeat") {
        receive_heartbeat(payload);
    } else {
        utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
            << "RECEIVE invalid operation: " << operation << std::endl;
    }
}

void ev_bsp_api::raise_comm_fault() {
    send_raise_error(API_GENERIC::ErrorEnum::CommunicationFault, comm_fault_subtype, "ChargeBridge not available");
}

void ev_bsp_api::clear_comm_fault() {
    send_clear_error(API_GENERIC::ErrorEnum::CommunicationFault, comm_fault_subtype);
}

void ev_bsp_api::receive_enable([[maybe_unused]] std::string const& payload) {
    // Not implemented
}

static CpState evcpstate_to_cpstate(API_EV_BSP::EvCpState s) {
    switch (s) {
    case API_EV_BSP::EvCpState::A:
        return CpState::CpState_A;
    case API_EV_BSP::EvCpState::B:
        return CpState::CpState_B;
    case API_EV_BSP::EvCpState::C:
        return CpState::CpState_C;
    case API_EV_BSP::EvCpState::D:
        return CpState::CpState_D;
    case API_EV_BSP::EvCpState::E:
        return CpState::CpState_E;
    default:
        return CpState::CpState_INVALID;
    }
}

void ev_bsp_api::receive_set_cp_state(std::string const& payload) {
    API_EV_BSP::EvCpState cp;

    if (everest::lib::API::deserialize(payload, cp)) {
        host_status.ev_set_cp_state = evcpstate_to_cpstate(cp);
        tx(host_status);
    } else {
        utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
            << "receive_set_cp_state: payload invalid -> " << payload << std::endl;
    }
}

void ev_bsp_api::receive_allow_power_on(std::string const& payload) {
    bool on;

    if (everest::lib::API::deserialize(payload, on)) {
        host_status.allow_power_on = static_cast<std::uint8_t>(on);
        tx(host_status);
    } else {
        utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
            << "receive_allow_power_on: payload invalid -> " << payload << std::endl;
    }
}

void ev_bsp_api::receive_diode_fail(std::string const& payload) {
    bool on;

    if (everest::lib::API::deserialize(payload, on)) {
        host_status.ev_set_diodefault = static_cast<std::uint8_t>(on);
        tx(host_status);
    } else {
        utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
            << "receive_diode_fail: payload invalid -> " << payload << std::endl;
    }
}

void ev_bsp_api::receive_set_ac_max_current([[maybe_unused]] std::string const& payload) {
    // Not implemented
}

void ev_bsp_api::receive_set_three_phases([[maybe_unused]] std::string const& payload) {
    // Not implemented
}

void ev_bsp_api::receive_set_rcd_error([[maybe_unused]] std::string const& payload) {
    // Not implemented
}

void ev_bsp_api::receive_heartbeat(std::string const& pl) {
    last_everest_heartbeat = std::chrono::steady_clock::now();
    std::size_t id = 0;
    if (deserialize(pl, id)) {
        auto delta = id - m_last_hb_id;
        if (delta > 1) {
            utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
                << "EVerest heartbeat missmatch: " << m_last_hb_id << "<->" << id << std::endl;
        }
        m_last_hb_id = id;
    } else {
        utilities::print_error(m_cb_identifier, "EV_BSP/EVEREST", -1)
            << "EVerest invalid heartbeat message: " << pl << std::endl;
    }
}

void ev_bsp_api::send_communication_check() {
    send_mqtt("communication_check", serialize(true));
}

void ev_bsp_api::send_mqtt(std::string const& topic, std::string const& message) {
    if (m_mqtt_tx) {
        m_mqtt_tx(topic, message);
    }
}

bool ev_bsp_api::check_everest_heartbeat() {
    return std::chrono::steady_clock::now() - last_everest_heartbeat < 2s;
}

void ev_bsp_api::handle_everest_connection_state() {
    send_communication_check();
    auto current = check_everest_heartbeat();
    auto handle_status = [this](bool status) {
        if (status) {
            utilities::print_error(m_cb_identifier, "EV/EVEREST", 0) << "EVerest connected" << std::endl;
            if (m_cb_connected) {
                // re-send last CP state event. Only meaningful with a live ChargeBridge:
                // otherwise 'last_cp_event' is the initial 'Disconnected' or a stale event of a
                // device that is gone, and replaying it would describe the wrong device.
                send_bsp_event(last_cp_event);
                // The relay state is a bsp_event too and equally published on change only, so a
                // restarted EVerest would otherwise not learn about closed contactors until the MCU
                // happens to open them. Replayed through the same handler set_cb_message() uses,
                // which does not latch on a previous state (and ignores an invalid relay value).
                handle_event_relay(m_cb_status.relay_state);
            } else {
                // The communication fault is edge triggered on the ChargeBridge connection, so a
                // freshly (re)started EVerest does not know about it. Re-assert it here,
                // otherwise the board support reads as fault free for an unreachable
                // ChargeBridge. Raising an already active error is a no-op in the EVerest error
                // framework.
                raise_comm_fault();
            }
        } else {
            utilities::print_error(m_cb_identifier, "EV/EVEREST", 1) << "Waiting for EVerest..." << std::endl;
            // unplug CP if EVerest disconnects
            host_status.ev_set_cp_state = CpState_A;
            tx(host_status);
        }
    };

    if (m_bc_initial_comm_check) {
        handle_status(current);
        m_bc_initial_comm_check = false;
    } else if (m_everest_connected != current) {
        handle_status(not m_everest_connected);
    }
    m_everest_connected = current;
}

void ev_bsp_api::send_raise_error(API_GENERIC::ErrorEnum error, std::string const& subtype, std::string const& msg) {
    API_GENERIC::Error error_msg;
    error_msg.type = error;
    error_msg.sub_type = subtype;
    error_msg.message = msg;
    send_mqtt("raise_error", serialize(error_msg));
}

void ev_bsp_api::send_clear_error(API_GENERIC::ErrorEnum error, std::string const& subtype) {
    API_GENERIC::Error error_msg;
    error_msg.type = error;
    error_msg.sub_type = subtype;
    send_mqtt("clear_error", serialize(error_msg));
}

} // namespace charge_bridge::evse_bsp
