// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "protocol/evse_bsp_cb_to_host.h"
#include <charge_bridge/everest_api/evse_bsp_api.hpp>
#include <charge_bridge/utilities/logging.hpp>
#include <charge_bridge/utilities/string.hpp>
#include <chrono>
#include <cstring>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest_api_types/evse_board_support/API.hpp>
#include <everest_api_types/evse_board_support/codec.hpp>
#include <everest_api_types/evse_manager/codec.hpp>
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
// The type 2 PP state machine (see handle_pp_type2) is the single owner of
// MREC23ProximityFault. The safety flag 'pp_invalid' describes the same physical condition but
// is reported as VendorError/PPINVALID instead of a second MREC23ProximityFault instance:
// OCPP 2.0.1 reporting maps the error type to a techCode and drops the sub_type, so clearing
// one instance would tell the CSMS that CX023 is gone while the other source is still faulted.
constexpr auto pp_invalid_subtype = "PPINVALID";
constexpr auto pp_fault_subtype_state = "PPSTATE";

// There is exactly one source for the communication fault, so it does not need a sub_type to
// tell instances apart. Raise and clear must agree on it, otherwise the clear does not match
// the raised instance.
constexpr auto comm_fault_subtype = "";
} // namespace

evse_bsp_api::evse_bsp_api(evse_bsp_config const& config, std::string const& cb_identifier,
                           evse_bsp_host_to_cb& host_status) :
    host_status(host_status), m_capabilities(config.capabilities), m_cb_identifier(cb_identifier) {

    last_everest_heartbeat = std::chrono::steady_clock::time_point();

    m_capabilities_timer.set_timeout(10s);

    std::memset(&cb_status, 0, sizeof(cb_status));

    m_enabled = true;
}

void evse_bsp_api::sync(bool cb_connected) {
    m_cb_connected = cb_connected;
    handle_everest_connection_state();
}

bool evse_bsp_api::register_events(everest::lib::io::event::fd_event_handler& handler) {
    // clang-format off
    return
        handler.register_event_handler(&m_capabilities_timer, [this](auto&) {
            // The capabilities are static configuration and do not depend on a ChargeBridge, so
            // they are re-sent unconditionally: EVerest needs them even before one shows up.
            send_capabilities();
            // The PP state on the other hand comes from 'cb_status', which is zero (or a stale
            // snapshot of a device that is gone) without a live ChargeBridge. Replaying it would
            // publish ampacity 'None' (state NC is 0) and clear a latched proximity fault for a
            // device that is not there, so it is only replayed while one is connected.
            if (m_cb_connected) {
                handle_pp_type2(cb_status.pp_state_type2);
            }
        });
    // clang-format on
}

bool evse_bsp_api::unregister_events(everest::lib::io::event::fd_event_handler& handler) {
    // clang-format off
    return
        handler.unregister_event_handler(&m_capabilities_timer);
    // clang-format on
}

void evse_bsp_api::set_cb_tx(tx_ftor const& handler) {
    m_tx = handler;
}

void evse_bsp_api::tx(evse_bsp_host_to_cb const& msg) {
    if (m_tx) {
        m_tx(msg);
    }
}

void evse_bsp_api::set_mqtt_tx(mqtt_ftor const& handler) {
    m_mqtt_tx = handler;
}

inline static bool operator==(const SafetyErrorFlags& a, const SafetyErrorFlags& b) {
    return a.raw == b.raw;
}
inline static bool operator!=(const SafetyErrorFlags& a, const SafetyErrorFlags& b) {
    return a.raw != b.raw;
}

void evse_bsp_api::set_cb_message(evse_bsp_cb_to_host const& msg) {
    if (cb_status.reset_reason not_eq msg.reset_reason) {
    }
    if (cb_status.cp_state not_eq msg.cp_state) {
        handle_event_cp(msg.cp_state);
    }
    if (cb_status.relay_state != msg.relay_state) {
        handle_event_relay(msg.relay_state);
    }
    if (cb_status.error_flags not_eq msg.error_flags) {
        handle_error(msg.error_flags);
    }
    if (cb_status.pp_state_type1 not_eq msg.pp_state_type1) {
        handle_pp_type1(msg.pp_state_type1);
    }
    if (cb_status.pp_state_type2 not_eq msg.pp_state_type2) {
        handle_pp_type2(msg.pp_state_type2);
    }
    if (cb_status.stop_charging not_eq msg.stop_charging) {
        handle_stop_button(msg.stop_charging);
    }
    // cb_status.lock_state is not checked here as it cannot be reported to EVerest.

    cb_status = msg;
}

void evse_bsp_api::dispatch(std::string const& operation, std::string const& payload) {
    if (operation == "enable") {
        receive_enable(payload);
    } else if (operation == "pwm_on") {
        receive_pwm_on(payload);
    } else if (operation == "cp_state_X1") {
        receive_cp_state_X1(payload);
    } else if (operation == "cp_state_F") {
        receive_cp_state_F(payload);
    } else if (operation == "allow_power_on") {
        receive_allow_power_on(payload);
    } else if (operation == "ac_switch_three_phases_while_charging") {
        receive_ac_switch_three_phases_while_charging(payload);
    } else if (operation == "ac_overcurrent_limit") {
        receive_ac_overcurrent_limit(payload);
    } else if (operation == "lock") {
        receive_lock();
    } else if (operation == "unlock") {
        receive_unlock();
    } else if (operation == "self_test") {
        receive_self_test(payload);
    } else if (operation == "reset") {
        receive_request_reset(payload);
    } else if (operation == "heartbeat") {
        receive_heartbeat(payload);
    } else {
        utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
            << "RECEIVE invalid operation: " << operation << std::endl;
    }
}

void evse_bsp_api::raise_comm_fault() {
    send_raise_error(API_BSP::ErrorEnum::CommunicationFault, comm_fault_subtype, "ChargeBridge not available");
}

void evse_bsp_api::clear_comm_fault() {
    send_clear_error(API_BSP::ErrorEnum::CommunicationFault, comm_fault_subtype, "");
}

void evse_bsp_api::handle_event_cp(std::uint8_t cp) {
    using bc_event = API_BSP::Event;
    bc_event cp_event;
    bool cp_state_valid = true;
    switch (cp) {
    case CpState_A:
        cp_event = bc_event::A;
        send_clear_error(API_BSP::ErrorEnum::MREC14PilotFault, "", "");
        send_clear_error(API_BSP::ErrorEnum::DiodeFault, "", "");
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
        cp_event = bc_event::E;
        break;
    case CpState_F:
        cp_event = bc_event::F;
        break;
    case CpState_DF:
        cp_event = bc_event::E;
        send_raise_error(API_BSP::ErrorEnum::DiodeFault, "", "Diode Fault");
        break;
    case CpState::CpState_INVALID:
        cp_event = bc_event::E;
        send_raise_error(API_BSP::ErrorEnum::MREC14PilotFault, "", "Pilot Fault");
        break;
    default:
        cp_state_valid = false;
    }
    if (cp_state_valid and m_enabled) {
        send_event(cp_event);
    }
}

void evse_bsp_api::handle_event_relay(std::uint8_t relay) {
    using bc_event = API_BSP::Event;
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
        send_event(relaise_event);
    }
}

// 'republish' re-sends an already latched proximity fault (used when EVerest reconnected and
// lost it). The latch itself is never reset from the outside: an out-of-range 'data' hits the
// default case below, which neither raises nor clears, and would leave an active fault with a
// dropped latch unclearable.
void evse_bsp_api::handle_pp_type2(std::uint8_t data, bool republish) {
    API_BSP::Ampacity bc_ampacity;
    bool bc_ampacity_valid = true;
    switch (data) {
    case PpState_Type2_STATE_NC:
        bc_ampacity = API_BSP::Ampacity::None;
        break;
    case PpState_Type2_STATE_13A:
        bc_ampacity = API_BSP::Ampacity::A_13;
        break;
    case PpState_Type2_STATE_20A:
        bc_ampacity = API_BSP::Ampacity::A_20;
        break;
    case PpState_Type2_STATE_32A:
        bc_ampacity = API_BSP::Ampacity::A_32;
        break;
    case PpState_Type2_STATE_70A:
        bc_ampacity = API_BSP::Ampacity::A_63_3ph_70_1ph;
        break;
    case PpState_Type2_STATE_FAULT:
        // Raise error check state
        bc_ampacity_valid = false;
        if (not m_pp_fault_raised or republish) {
            send_raise_error(API_BSP::ErrorEnum::MREC23ProximityFault, pp_fault_subtype_state,
                             "Proximity Pilot Fault State");
            m_pp_fault_raised = true;
        }
        break;
    default:
        bc_ampacity_valid = false;
    }
    if (bc_ampacity_valid) {
        // Firmware reports a non-fault state again: clear a previously raised proximity fault
        if (m_pp_fault_raised) {
            send_clear_error(API_BSP::ErrorEnum::MREC23ProximityFault, pp_fault_subtype_state, "");
            m_pp_fault_raised = false;
        }
        send_ac_pp_amapcity(bc_ampacity);
    }
}

void evse_bsp_api::handle_pp_type1(std::uint8_t data) {
    // EVerest does not really have support for type 1 PP.
    // We just send a stop charging if some one presses the button,
    // for everything else the PP state does not really matter.
    if (data == PpState_Type1_STATE_Connected_Button_Pressed) {
        auto reason = API_EVM::StopTransactionReason::EVDisconnected;
        send_request_stop_transaction(reason);
    }
}

// Error handling
// Define bit masks
enum class SafetyErrorMask : std::uint32_t {
    cp_not_state_c = (1 << 0),
    pwm_not_enabled = (1 << 1),
    pp_invalid = (1 << 2),
    plug_temperature_too_high = (1 << 3),
    internal_temperature_too_high = (1 << 4),
    emergency_input_latched = (1 << 5),
    relay_health_latched = (1 << 6),
    vdd_3v3_out_of_range = (1 << 7),
    vdd_core_out_of_range = (1 << 8),
    vdd_12V_out_of_range = (1 << 9),
    vdd_N12V_out_of_range = (1 << 10),
    vdd_refint_out_of_range = (1 << 11),
    external_allow_power_on = (1 << 12),
    config_mem_error = (1 << 13),
    dc_hv_ov = (1 << 14),
    rcd_error = (1 << 16),
};

// Table that maps a mask to our API error + message
struct FlagSpec {
    SafetyErrorMask mask;
    API_BSP::ErrorEnum error;
    const char* subtype;
    const char* message;
};

static constexpr FlagSpec error_specs[] = {
    {SafetyErrorMask::pp_invalid, API_BSP::ErrorEnum::VendorError, pp_invalid_subtype, "PP invalid"},
    {SafetyErrorMask::plug_temperature_too_high, API_BSP::ErrorEnum::MREC19CableOverTempStop, "",
     "Plug temperature too high"},
    {SafetyErrorMask::internal_temperature_too_high, API_BSP::ErrorEnum::VendorError, "INTTEMP",
     "ChargeBridge internal over temperature"},
    {SafetyErrorMask::emergency_input_latched, API_BSP::ErrorEnum::VendorError, "EMGINPUT", "Emergency input latched"},
    {SafetyErrorMask::relay_health_latched, API_BSP::ErrorEnum::VendorError, "RELAYS", "Relay welded error"},
    {SafetyErrorMask::vdd_3v3_out_of_range, API_BSP::ErrorEnum::VendorError, "3V3", "Supply voltage 3.3V out of range"},
    {SafetyErrorMask::vdd_core_out_of_range, API_BSP::ErrorEnum::VendorError, "VDDCORE",
     "Internal supply core voltage out of range"},
    {SafetyErrorMask::vdd_12V_out_of_range, API_BSP::ErrorEnum::VendorError, "VCC12",
     "Internal supply 12V voltage out of range"},
    {SafetyErrorMask::vdd_N12V_out_of_range, API_BSP::ErrorEnum::VendorError, "VCCN12",
     "Internal supply -12V voltage out of range"},
    {SafetyErrorMask::vdd_refint_out_of_range, API_BSP::ErrorEnum::VendorError, "VCCREF",
     "Internal supply VREF voltage out of range"},
    {SafetyErrorMask::config_mem_error, API_BSP::ErrorEnum::VendorError, "CONFIGMEM", "Internal config memory error"},
    {SafetyErrorMask::dc_hv_ov, API_BSP::ErrorEnum::VendorError, "DV_HV",
     "DC HV OVM. FIXME: This should be on OVM not EVSE interface"},
    {SafetyErrorMask::rcd_error, API_BSP::ErrorEnum::MREC2GroundFailure, "", "RCD error detected"},
};

static constexpr FlagSpec print_warning_specs[] = {
    {SafetyErrorMask::cp_not_state_c, API_BSP::ErrorEnum::VendorWarning, "", "CP is not state C"},
    {SafetyErrorMask::pwm_not_enabled, API_BSP::ErrorEnum::VendorWarning, "", "PWM not enabled"},
    {SafetyErrorMask::external_allow_power_on, API_BSP::ErrorEnum::VendorWarning, "",
     "Allow power on from EVerest missing"},
};

// Raise/clear all errors whose flag changed between 'prev' and 'next'.
// Passing prev = 0 turns every active flag into a rising edge and therefore re-raises all
// currently active errors without clearing anything.
void evse_bsp_api::publish_error_flag_edges(std::uint32_t prev, std::uint32_t next) {
    std::uint32_t became_active = next & ~prev;   // rising edges
    std::uint32_t became_inactive = prev & ~next; // falling edges

    for (const auto& s : error_specs) {
        if (became_active & static_cast<std::uint32_t>(s.mask)) {
            send_raise_error(s.error, s.subtype, s.message);
        }
        if (became_inactive & static_cast<std::uint32_t>(s.mask)) {
            send_clear_error(s.error, s.subtype, "");
        }
    }
}

// 4) Edge-driven handler
void evse_bsp_api::handle_error(const SafetyErrorFlags& data) {
    std::uint32_t next = data.raw; // current raw value

    publish_error_flag_edges(cb_status.error_flags.raw, next);

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

    if (everest_connected && m_cb_connected) {
        if (log.str().empty()) {
            utilities::print_error(m_cb_identifier, "EVSE/EVEREST", 0) << "Relays can be switched on." << std::endl;
        } else {
            utilities::print_error(m_cb_identifier, "EVSE/EVEREST", 0)
                << "Relays off due to:" << log.str() << std::endl;
        }
    }
}

void evse_bsp_api::handle_stop_button(std::uint8_t data) {
    // Only react to the pressed edge, not to the release (or any other toggle)
    // of the stop_charging flag in the status frame.
    if (data == 0) {
        return;
    }
    utilities::print_error(m_cb_identifier, "EVSE/EVEREST", 0)
        << "Stop charging button pressed -> requesting local stop transaction." << std::endl;
    auto reason = API_EVM::StopTransactionReason::Local;
    send_request_stop_transaction(reason);
}

void evse_bsp_api::receive_enable(std::string const& payload) {
    if (everest::lib::API::deserialize(payload, m_enabled)) {
        // Re-derive the reported state from 'cb_status' only while a ChargeBridge is actually
        // connected. Otherwise the snapshot is zero or stale and replaying it would report CP
        // state A and clear MREC14PilotFault/DiodeFault for a device that is not there.
        if (m_cb_connected) {
            handle_event_cp(cb_status.cp_state);
            handle_event_relay(cb_status.relay_state);
        }
    } else {
        utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
            << "receive_enabled: payload invalid -> " << payload << std::endl;
    }
}

void evse_bsp_api::receive_pwm_on(std::string const& payload) {
    double pwm = 0;
    if (everest::lib::API::deserialize(payload, pwm)) {
        host_status.pwm_duty_cycle = pwm * 100;
        tx(host_status);
    } else {
        utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
            << "receive_pwm_on: payload invalid -> " << payload << std::endl;
    }
}

void evse_bsp_api::receive_cp_state_X1([[maybe_unused]] std::string const& payload) {
    host_status.pwm_duty_cycle = 10001;
    tx(host_status);
}

void evse_bsp_api::receive_cp_state_F([[maybe_unused]] std::string const& payload) {
    host_status.pwm_duty_cycle = 0;
    tx(host_status);
}

void evse_bsp_api::receive_allow_power_on(std::string const& payload) {
    API_BSP::PowerOnOff obj;
    if (everest::lib::API::deserialize(payload, obj)) {
        host_status.allow_power_on = obj.allow_power_on;
        tx(host_status);
    } else {
        utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
            << "receive_allow_power_on: payload invalid -> " << payload << std::endl;
    }
}

void evse_bsp_api::receive_ac_switch_three_phases_while_charging(std::string const&) {
}

void evse_bsp_api::receive_ac_overcurrent_limit(std::string const&) {
}

void evse_bsp_api::receive_lock() {
    host_status.connector_lock = 1;
    tx(host_status);
}

void evse_bsp_api::receive_unlock() {
    host_status.connector_lock = 0;
    tx(host_status);
}

void evse_bsp_api::receive_self_test([[maybe_unused]] std::string const& payload) {
}

void evse_bsp_api::receive_request_reset(std::string const&) {
}

void evse_bsp_api::receive_heartbeat(std::string const& pl) {
    last_everest_heartbeat = std::chrono::steady_clock::now();
    std::size_t id = 0;
    if (deserialize(pl, id)) {
        auto delta = id - m_last_hb_id;
        if (delta > 1) {
            utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
                << "EVerest heartbeat missmatch: " << m_last_hb_id << "<->" << id << std::endl;
        }
        m_last_hb_id = id;
    } else {
        utilities::print_error(m_cb_identifier, "EVSE/EVEREST", -1)
            << "EVerest invalid heartbeat message: " << pl << std::endl;
    }
}

void evse_bsp_api::send_event(API_BSP::Event data) {
    API_BSP::BspEvent event{data};
    send_mqtt("event", serialize(event));
}

void evse_bsp_api::send_ac_nr_of_phases(std::uint8_t data) {
    auto phases = static_cast<int>(data);
    if (phases > 0 && phases <= 3) {
        send_mqtt("ac_nr_of_phases", serialize(phases));
    }
}

void evse_bsp_api::send_capabilities() {
    send_mqtt("capabilities", serialize(m_capabilities));
}

void evse_bsp_api::send_ac_pp_amapcity(API_BSP::Ampacity data) {
    API_BSP::ProximityPilot msg{data};
    send_mqtt("ac_pp_ampacity", serialize(msg));
}

void evse_bsp_api::send_request_stop_transaction(API_EVM::StopTransactionReason data) {
    API_EVM::StopTransactionRequest request;
    request.reason = data;
    send_mqtt("request_stop_transaction", serialize(request));
}

void evse_bsp_api::send_rcd_current(std::uint8_t) {
}

void evse_bsp_api::send_raise_error(API_BSP::ErrorEnum error, std::string const& subtype, std::string const& msg) {
    API_BSP::Error error_msg;
    error_msg.type = error;
    error_msg.sub_type = subtype;
    error_msg.message = msg;
    send_mqtt("raise_error", serialize(error_msg));
}

void evse_bsp_api::send_clear_error(API_BSP::ErrorEnum error, std::string const& subtype, std::string const& msg) {
    API_BSP::Error error_msg;
    error_msg.type = error;
    error_msg.sub_type = subtype;
    error_msg.message = msg;
    send_mqtt("clear_error", serialize(error_msg));
}

void evse_bsp_api::send_communication_check() {
    send_mqtt("communication_check", serialize(true));
}

void evse_bsp_api::send_reply_reset([[maybe_unused]] std::string const& replyTo) {
}

void evse_bsp_api::send_mqtt(std::string const& topic, std::string const& message) {
    m_mqtt_tx(topic, message);
}

bool evse_bsp_api::check_everest_heartbeat() {
    return std::chrono::steady_clock::now() - last_everest_heartbeat < 2s;
}

void evse_bsp_api::handle_everest_connection_state() {
    send_communication_check();
    auto current = check_everest_heartbeat();
    auto handle_status = [this](bool status) {
        if (status) {
            utilities::print_error(m_cb_identifier, "EVSE/EVEREST", 0) << "EVerest connected" << std::endl;
            send_capabilities();
            // A freshly (re)started EVerest lost every error raised before it came up, while the
            // MCU keeps its latched errors. Re-publish all currently active errors instead of
            // assuming they are still known. Raising an already active error is ignored by the
            // EVerest error framework, so this is harmless if EVerest did not actually restart
            // (e.g. a short heartbeat gap).
            if (m_cb_connected) {
                // All active safety flags (treating "nothing known before" as the previous
                // state) plus an active proximity fault state.
                publish_error_flag_edges(0, cb_status.error_flags.raw);
                handle_pp_type2(cb_status.pp_state_type2, true);
                // CP and relay state are published on change only, so a restarted EVerest would
                // otherwise see no BSP event (and no MREC14PilotFault/DiodeFault) until the MCU
                // happens to change state or EvseManager re-sends 'enable'. Neither handler
                // latches on a previous state, so both replay the current state unconditionally.
                handle_event_cp(cb_status.cp_state);
                handle_event_relay(cb_status.relay_state);
            } else {
                // Without a live ChargeBridge 'cb_status' is zero or a stale snapshot of a
                // device that is gone (or has been replaced), so it must not be replayed: that
                // would attribute errors to the wrong device and report the connector as
                // available. The communication fault is edge triggered on the ChargeBridge
                // connection and equally unknown to a restarted EVerest, so re-assert it here.
                // The real state is published as soon as the ChargeBridge reports it again.
                raise_comm_fault();
            }
        } else {
            utilities::print_error(m_cb_identifier, "EVSE/EVEREST", 1) << "Waiting for EVerest...." << std::endl;
            host_status.allow_power_on = 0;
            host_status.pwm_duty_cycle = 65535;
            tx(host_status);
        }
    };

    if (m_bc_initial_comm_check) {
        handle_status(current);
        m_bc_initial_comm_check = false;
    } else if (everest_connected != current) {
        handle_status(not everest_connected);
    }
    everest_connected = current;
}

} // namespace charge_bridge::evse_bsp
