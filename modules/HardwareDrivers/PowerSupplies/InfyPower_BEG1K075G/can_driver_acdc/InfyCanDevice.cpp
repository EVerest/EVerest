// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "InfyCanDevice.hpp"
#include "CanPackets.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

#include <everest/logging.hpp>

namespace {
using namespace std::chrono_literals;
// InfyPower protocol: space controller commands 50-200 ms apart.
constexpr auto TICK_INTERVAL = 50ms;
constexpr auto RX_TIMEOUT = 1000ms;
constexpr unsigned RX_TIMEOUT_TICKS = RX_TIMEOUT / TICK_INTERVAL;
constexpr std::size_t MAX_QUEUED_FRAMES = 256;
constexpr float MIN_SETPOINT_VOLTAGE = 150.;
} // namespace

InfyCanDevice::InfyCanDevice() : can_device::CanDevice(TICK_INTERVAL) {
    using namespace can_packet_acdc;
    // One frame per tick. Requests are answered on the rx handler, setpoints are refreshed cyclically, which also
    // keeps the module from timing out.
    tx_cycle = {
        {CycleStep::Read, [this] { return send(read_frame(ADDR_BROADCAST, SystemDCVoltage())); }},
        {CycleStep::Read, [this] { return send(read_frame(ADDR_BROADCAST, SystemDCCurrent())); }},
        {CycleStep::Read, [this] { return send(read_frame(ADDR_MODULE, PowerModuleStatus())); }},
        {CycleStep::Read, [this] { return send(read_frame(ADDR_MODULE, InverterStatus())); }},
        {CycleStep::Write, [this] { return send(write_frame(ADDR_BROADCAST, OnOff(on))); }},
        {CycleStep::Write,
         [this] { return setpoint_valid() and send(write_frame(ADDR_BROADCAST, SystemDCVoltage(setpoint_voltage))); }},
        {CycleStep::Write,
         [this] { return setpoint_valid() and send(write_frame(ADDR_BROADCAST, SystemDCCurrent(setpoint_current))); }},
        {CycleStep::Write, [this] { return send(write_frame(ADDR_BROADCAST, WalkInEnable(walkin_enable))); }},
        {CycleStep::Write, [this] { return send(write_frame(ADDR_BROADCAST, WorkingMode(inverter_mode))); }},
    };
}

InfyCanDevice::~InfyCanDevice() {
    // Stop the CAN thread before the handlers it calls are destroyed.
    close_device();
}

void InfyCanDevice::rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) {
    EVLOG_debug << "Infy: CAN frame received. ID: 0x" << std::hex << can_id;

    // We only use extended frames here
    if (!(can_id & CAN_EFF_FLAG)) {
        EVLOG_debug << "Infy: Ignoring, not extended protocol.";
        return;
    }

    // is it for us?
    if (can_packet_acdc::destination_address_from_can_id(can_id) != can_packet_acdc::ADDR_CONTROLLER) {
        return;
    }

    // Write acks count as the module answering too.
    ticks_since_rx = 0;

    if (can_packet_acdc::command_number_from_can_id(can_id) == can_packet_acdc::CMD_WRITE) {
        switch (can_packet_acdc::error_code_from_can_id(can_id)) {
        case 0x02:
            EVLOG_error << "Infy: ERROR: Command invalid.";
            break;
        case 0x03:
            EVLOG_error << "Infy: ERROR: Data invalid.";
            break;
        case 0x07:
            EVLOG_error << "Infy: ERROR: In start processing.";
            break;
        }
        return;
    }

    // is it a reply to a read command?
    if (can_packet_acdc::command_number_from_can_id(can_id) != can_packet_acdc::CMD_READ) {
        return;
    }

    if (not reported_connection.value_or(false)) {
        if (settings_restored) {
            settings_restored = false;
            report_connection(true);
        } else if (resync_steps.empty()) {
            start_resync();
        }
    }

    if (payload.size() < 8) {
        EVLOG_warning << "Infy: Ignoring short CAN frame (" << payload.size() << " bytes)";
        return;
    }

    uint16_t packet_type = payload[0] << 8 | payload[1];

    if (can_packet_acdc::error_code_from_can_id(can_id) > 0) {
        EVLOG_debug << "Infy: Parsing CAN packet type: " << std::hex << packet_type << " Error code:" << std::hex
                    << (int)can_packet_acdc::error_code_from_can_id(can_id);
    }

    switch (packet_type) {
    case 0x1001: {
        telemetry.voltage = payload;
    } break;

    case 0x1002: {
        telemetry.current = payload;
        signalVoltageCurrent(telemetry.voltage.volt, telemetry.current.ampere);
    } break;

    case 0x1010: {
        can_packet_acdc::PowerModuleNumber n(payload);
    } break;

    case 0x1110: {
        can_packet_acdc::PowerModuleStatus s(payload);
        telemetry.status = s;
    } break;

    case 0x1111: {
        can_packet_acdc::InverterStatus s(payload);
        signalModuleStatus(telemetry.status, s);
    } break;

    case 0x1103: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_ab_line_voltage = s.value / 1000.;
    } break;

    case 0x1104: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_bc_line_voltage = s.value / 1000.;
    } break;

    case 0x1105: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_ca_line_voltage = s.value / 1000.;
    } break;

    case 0x1106: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ambient_temperature = s.value / 1000.;
    } break;

    case 0x1130: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_max_output_voltage = s.value / 1000.;
    } break;

    case 0x1131: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_min_output_voltage = s.value / 1000.;
    } break;

    case 0x1132: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_max_output_current = s.value / 1000.;
    } break;

    case 0x1133: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_rated_output_power = s.value / 1000.;
    } break;

    case 0x2101: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_voltage = s.value / 1000.;
    } break;

    case 0x2102: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_voltage = s.value / 1000.;
    } break;

    case 0x2103: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_voltage = s.value / 1000.;
    } break;

    case 0x2104: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_current = s.value / 1000.;
    } break;

    case 0x2105: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_current = s.value / 1000.;
    } break;

    case 0x2106: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_current = s.value / 1000.;
    } break;

    case 0x2107: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_frequency = s.value / 1000.;
    } break;

    case 0x2109: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_active_power = s.value / 1000.;
    } break;

    case 0x210A: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_active_power = s.value / 1000.;
    } break;

    case 0x210B: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_active_power = s.value / 1000.;
    } break;

    case 0x2108: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_active_ower = s.value / 1000.;
    } break;

    case 0x210D: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_reactive_power = s.value / 1000.;
    } break;

    case 0x210E: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_reactive_power = s.value / 1000.;
    } break;

    case 0x210F: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_reactive_power = s.value / 1000.;
    } break;

    case 0x210C: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_reactive_ower = s.value / 1000.;
    } break;

    case 0x2110: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_a_apparent_power = s.value / 1000.;
    } break;

    case 0x2111: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_b_apparent_power = s.value / 1000.;
    } break;

    case 0x2112: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_phase_c_apparent_power = s.value / 1000.;
    } break;

    case 0x2113: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.ac_total_apparent_ower = s.value / 1000.;
    } break;

    case 0x4101: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_high_side_voltage = s.value / 1000.;
    } break;

    case 0x4102: {
        can_packet_acdc::GenericSetting s(payload);
        telemetry.dc_high_side_current = s.value / 1000.;
    } break;

    default: {
        can_packet_acdc::GenericSetting s(payload);
    }
    }
}

void InfyCanDevice::tick_handler() {
    if (not link) {
        return;
    }

    // The module is not polled while the resync steps go out, so the timeout runs only once they are through.
    if (not resync_steps.empty()) {
        if (resync_steps.front()()) {
            resync_steps.pop_front();
            if (resync_steps.empty()) {
                settings_restored = true;
                ticks_since_rx = 0;
            }
        }
        last_tick_was_cycle = false;
        return;
    }

    if (ticks_since_rx < RX_TIMEOUT_TICKS) {
        ++ticks_since_rx;
    } else {
        abort_resync();
        report_connection(false);
    }

    if (reported_connection.value_or(false) and last_tick_was_cycle) {
        if (auto frame = pop_queued()) {
            if (not send(*frame)) {
                requeue_front(std::move(*frame));
            }
            last_tick_was_cycle = false;
            return;
        }
    }
    send_cycle_step();
    last_tick_was_cycle = true;
}

// Writes wait until the resync has restored the module, so it is never switched on with stale settings.
void InfyCanDevice::send_cycle_step() {
    bool const writes_allowed = reported_connection.value_or(false);
    for (std::size_t i = 0; i < tx_cycle.size(); ++i) {
        auto const& step = tx_cycle[tx_cycle_step];
        tx_cycle_step = (tx_cycle_step + 1) % tx_cycle.size();
        if (step.kind == CycleStep::Write and not writes_allowed) {
            continue;
        }
        if (step.send()) {
            return;
        }
    }
}

void InfyCanDevice::link_handler(bool up) {
    link = up;
    ticks_since_rx = 0;
    if (not up) {
        abort_resync();
        report_connection(false);
    }
}

// Restores everything the module needs before it is reported as connected: errors/power_supply_DC.yaml requires mode
// and setpoints to be set before CommunicationFault is cleared. Mode, setpoints and output state are read when their
// frame is sent, so a command given while the module is being restored is not overridden. The output is switched on
// last.
void InfyCanDevice::start_resync() {
    using namespace can_packet_acdc;
    EVLOG_info << "Infy: module answering, restoring settings and setpoints";
    std::scoped_lock lock(settings_mutex, queue_mutex);
    // Queued writes are settings the resync sends from their stored values; only the reads are kept.
    queued.erase(
        std::remove_if(queued.begin(), queued.end(),
                       [](Frame const& frame) { return command_number_from_can_id(frame.can_id) == CMD_WRITE; }),
        queued.end());
    resync_steps.clear();
    settings_restored = false;
    if (power_factor.has_value()) {
        resync_steps.push_back(
            [this, pf = power_factor.value()] { return send(write_frame(ADDR_MODULE, PowerFactorAdjust(pf))); });
    }
    for (auto const& setting : generic_settings) {
        auto const address = setting.first;
        auto const value = setting.second;
        resync_steps.push_back([this, address, value] {
            return send(write_frame(ADDR_BROADCAST, GenericSetting(address.first, address.second, value)));
        });
    }
    resync_steps.push_back([this] { return send(write_frame(ADDR_BROADCAST, WorkingMode(inverter_mode))); });
    resync_steps.push_back([this] { return send(write_frame(ADDR_BROADCAST, WalkInEnable(walkin_enable))); });
    resync_steps.push_back([this] {
        return not setpoint_valid() or send(write_frame(ADDR_BROADCAST, SystemDCVoltage(setpoint_voltage)));
    });
    resync_steps.push_back([this] {
        return not setpoint_valid() or send(write_frame(ADDR_BROADCAST, SystemDCCurrent(setpoint_current)));
    });
    resync_steps.push_back([this] { return send(write_frame(ADDR_BROADCAST, OnOff(on))); });
}

void InfyCanDevice::abort_resync() {
    resync_steps.clear();
    settings_restored = false;
}

void InfyCanDevice::report_connection(bool connected) {
    if (reported_connection == connected) {
        return;
    }
    reported_connection = connected;
    if (connected) {
        EVLOG_info << "Infy: module connected";
    } else {
        EVLOG_error << "Infy: module not answering";
    }
    signal_connection(connected);
}

bool InfyCanDevice::setpoint_valid() const {
    return setpoint_voltage > MIN_SETPOINT_VOLTAGE;
}

bool InfyCanDevice::switch_on_off(bool _on) {
    on = _on;
    return true;
}

bool InfyCanDevice::set_walkin_enabled(bool on) {
    walkin_enable = on;
    return true;
}

bool InfyCanDevice::set_inverter_mode(bool i) {
    inverter_mode = i;
    return true;
}

bool InfyCanDevice::adjust_power_factor(float pf) {
    {
        std::scoped_lock lock(settings_mutex);
        power_factor = pf;
    }
    queue(write_frame(can_packet_acdc::ADDR_MODULE, can_packet_acdc::PowerFactorAdjust(pf)));
    return true;
}

bool InfyCanDevice::set_voltage_current(float voltage, float current) {
    setpoint_current = current;
    setpoint_voltage = voltage;
    return true;
}

bool InfyCanDevice::set_generic_setting(uint8_t byte0, uint8_t byte1, uint32_t value) {
    {
        std::scoped_lock lock(settings_mutex);
        generic_settings[{byte0, byte1}] = value;
    }
    queue(write_frame(can_packet_acdc::ADDR_BROADCAST, can_packet_acdc::GenericSetting(byte0, byte1, value)));
    return true;
}

bool InfyCanDevice::set_output_mode(OutputMode mode) {
    return set_generic_setting(0x11, 0x26, static_cast<std::underlying_type<OutputMode>::type>(mode));
}

bool InfyCanDevice::request_rx(const uint8_t destination_address, const std::vector<uint8_t>& payload) {
    queue(read_frame(destination_address, payload));
    return true;
}

InfyCanDevice::Frame InfyCanDevice::write_frame(uint8_t destination_address, const std::vector<uint8_t>& payload) {
    uint32_t can_id = can_packet_acdc::encode_can_id(destination_address, can_packet_acdc::CMD_WRITE);
    can_id |= CAN_EFF_FLAG;
    return {can_id, payload};
}

InfyCanDevice::Frame InfyCanDevice::read_frame(uint8_t destination_address, const std::vector<uint8_t>& payload) {
    uint32_t can_id = can_packet_acdc::encode_can_id(destination_address, can_packet_acdc::CMD_READ);
    can_id |= CAN_EFF_FLAG;
    return {can_id, payload};
}

bool InfyCanDevice::send(const Frame& frame) {
    return tx_frame(frame.can_id, frame.payload);
}

// Settings are restored from their stored values on every resync, so the oldest entries are the ones to lose.
void InfyCanDevice::queue(Frame frame) {
    std::scoped_lock lock(queue_mutex);
    if (queued.size() >= MAX_QUEUED_FRAMES) {
        queued.pop_front();
    }
    queued.push_back(std::move(frame));
}

void InfyCanDevice::requeue_front(Frame frame) {
    std::scoped_lock lock(queue_mutex);
    queued.push_front(std::move(frame));
}

std::optional<InfyCanDevice::Frame> InfyCanDevice::pop_queued() {
    std::scoped_lock lock(queue_mutex);
    if (queued.empty()) {
        return std::nullopt;
    }
    auto frame = std::move(queued.front());
    queued.pop_front();
    return frame;
}

std::ostream& operator<<(std::ostream& out, const InfyCanDevice::Telemetry& self) {
    out << "\n------------------------------------------------\nTelemetry\n---------\n";

    out << "AC line: AB:" << std::to_string(self.ac_ab_line_voltage)
        << " BC: " << std::to_string(self.ac_bc_line_voltage) << " CA: " << std::to_string(self.ac_ca_line_voltage)
        << std::endl;

    out << "AC Phase Voltages: V_A:" << std::to_string(self.ac_phase_a_voltage)
        << " V_B: " << std::to_string(self.ac_phase_b_voltage) << " V_C: " << std::to_string(self.ac_phase_c_voltage)
        << std::endl;

    out << "AC Phase Currents: I_A:" << std::to_string(self.ac_phase_a_current)
        << " I_B: " << std::to_string(self.ac_phase_b_current) << " I_C: " << std::to_string(self.ac_phase_c_current)
        << std::endl;

    out << "AC Active power: Total: " << std::to_string(self.ac_total_active_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_active_power)
        << " P_B: " << std::to_string(self.ac_phase_b_active_power)
        << " P_C: " << std::to_string(self.ac_phase_c_active_power) << std::endl;

    out << "AC Re-Active power: Total: " << std::to_string(self.ac_total_reactive_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_reactive_power)
        << " P_B: " << std::to_string(self.ac_phase_b_reactive_power)
        << " P_C: " << std::to_string(self.ac_phase_c_reactive_power) << std::endl;

    out << "AC Apparent power: Total: " << std::to_string(self.ac_total_apparent_ower)
        << "P_A:" << std::to_string(self.ac_phase_a_apparent_power)
        << " P_B: " << std::to_string(self.ac_phase_b_apparent_power)
        << " P_C: " << std::to_string(self.ac_phase_c_apparent_power) << std::endl;

    out << "AC frequency: " << self.ac_frequency << std::endl;
    out << "Ambient temperature: " << self.ambient_temperature << std::endl;

    out << "DC High Voltage side: Voltage: " << std::to_string(self.dc_high_side_voltage)
        << " Current: " << std::to_string(self.dc_high_side_current) << std::endl;

    out << "Capabilities: dc_min: " << self.dc_min_output_voltage << "V dc_max: " << self.dc_max_output_voltage
        << "V dc_max_current: " << self.dc_max_output_current << "A max_watt: " << self.dc_rated_output_power << "W "
        << std::endl;

    out << self.status << std::endl;
    out << self.voltage << std::endl << self.current << std::endl;

    out << "------------------------------------------------\n";

    return out;
}
