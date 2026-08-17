// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "InfyCanDevice.hpp"
#include "CanPackets.hpp"
#include <everest/logging.hpp>

#include <algorithm>
#include <iomanip>
#include <regex>

static std::vector<std::string> split_by_delimiters(const std::string& s, const std::string& delimiters) {
    std::regex re("[" + delimiters + "]");
    std::sregex_token_iterator first{s.begin(), s.end(), re, -1}, last;
    return {first, last};
}

static std::vector<uint8_t> parse_module_addresses(const std::string& a) {
    std::vector<uint8_t> addresses;
    auto adr = split_by_delimiters(a, ",");
    addresses.reserve(adr.size()); // Pre-allocate memory for efficiency

    for (const auto& ad : adr) {
        try {
            addresses.push_back(std::stoi(ad));
        } catch (const std::exception& e) {
            EVLOG_error << "Infy: Invalid module address '" << ad << "': " << e.what();
        }
    }
    return addresses;
}

InfyCanDevice::InfyCanDevice() : CanBus() {
}

InfyCanDevice::~InfyCanDevice() {
}

void InfyCanDevice::initial_ping() {
    if (operating_mode == OperatingMode::GROUP_DISCOVERY) {
        send_command<can_packet_acdc::ReadModuleCount>(group_address, true);
    } else {
        EVLOG_info << "Infy: Operating in FIXED_ADDRESS mode. No need to ping.";
        initialized = true;
        switch_on_off(false);
    }
}

void InfyCanDevice::set_can_device(const std::string& dev) {
    can_device = dev;
    EVLOG_info << "Infy: Setting config values: CAN device: " << dev;
    open_device(can_device.c_str());
}

void InfyCanDevice::set_config_values(const std::string& addrs, int group_addr, int timeout, int controller_address) {
    this->device_connection_timeout_s = timeout;
    this->group_address = group_addr;
    this->controller_address = controller_address;

    EVLOG_info << "Infy: Operating with controller address: 0x" << std::hex << controller_address;
    if (!addrs.empty()) {
        operating_mode = OperatingMode::FIXED_ADDRESS;
        module_addresses = parse_module_addresses(addrs);
        expected_module_count = module_addresses.size();
        active_module_addresses = module_addresses; // Initially assume all configured addresses are active
        EVLOG_info << "Infy: Operating in FIXED_ADDRESS mode with " << expected_module_count << " addresses: " << addrs;
    } else {
        operating_mode = OperatingMode::GROUP_DISCOVERY;
        EVLOG_info << "Infy: Operating in GROUP_DISCOVERY mode for group address: " << group_address;
    }
    EVLOG_info << "Infy: module communication timeout: " << device_connection_timeout_s << "s";
}

void InfyCanDevice::rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) {
    if (!(can_id & CAN_EFF_FLAG)) {
        return;
    }

    // Ignore messages not addressed to us (the controller)
    if (can_packet_acdc::destination_address_from_can_id(can_id) != controller_address) {
        return;
    }

    // Discard malformed CAN frames with insufficient data
    if (payload.size() < 8) {
        EVLOG_error << "Infy: Received malformed CAN frame with size " << payload.size()
                    << " (expected 8 bytes). Discarding frame.";
        return;
    }

    const uint8_t source_address = can_packet_acdc::source_address_from_can_id(can_id);
    const uint8_t command_number = can_packet_acdc::command_number_from_can_id(can_id);

    // Ignore messages from unknown sources in FIXED_ADDRESS mode, only consider those that are explicitly configured.
    // This prevents interference with other Infy devices on the same CAN bus that are not part of this system.
    if (operating_mode == OperatingMode::FIXED_ADDRESS &&
        std::find(module_addresses.begin(), module_addresses.end(), source_address) == module_addresses.end()) {
        EVLOG_debug << "Infy: Received message from unknown module address 0x" << std::hex << std::uppercase
                    << static_cast<int>(source_address) << ". Ignoring.";
        return;
    }

    switch (command_number) {
    case can_packet_acdc::ReadModuleCount::CMD_ID: {
        handle_module_count_packet(payload);
    } break;
    case can_packet_acdc::ReadModuleVI::CMD_ID: {
        handle_simple_telemetry_update(source_address, payload, command_number);
    } break;
    case can_packet_acdc::PowerModuleStatus::CMD_ID: {
        can_packet_acdc::PowerModuleStatus status(payload);
        signalModuleStatus(status);
        // Signal error status changes (excluding fields marked as "Status")
        auto& telemetry = telemetries[source_address];
        check_and_signal_error_status_change(source_address, status, telemetry.status);
        telemetry.status = status;
        // using status message to set the last_update time
        telemetry.last_update = std::chrono::steady_clock::now();
        mark_module_active(source_address); // keep active set in sync with the liveness heartbeat
    } break;
    case can_packet_acdc::ReadModuleVIAfterDiode::CMD_ID: {
        handle_simple_telemetry_update(source_address, payload, command_number);
    } break;
    case can_packet_acdc::ReadModuleCapabilities::CMD_ID: {
        handle_simple_telemetry_update(source_address, payload, command_number);
    } break;
    case can_packet_acdc::ReadModuleBarcode::CMD_ID: {
        can_packet_acdc::ReadModuleBarcode barcode(payload);
        auto& telemetry = telemetries[source_address];
        telemetry.serial_number = barcode.serial_number;
        EVLOG_info << format_module_id(source_address) << ": serial number: " << barcode.serial_number;
    } break;
    default: {
        // Not implemented yet
    }
    }
}

size_t InfyCanDevice::remove_expired_telemetry_entries() {
    auto now = std::chrono::steady_clock::now();
    auto timeout_duration = std::chrono::seconds(device_connection_timeout_s);
    size_t removed_count = 0;

    // Remove expired telemetry entries
    for (auto it = telemetries.begin(); it != telemetries.end();) {
        const auto& [address, telemetry] = *it;
        if (now - telemetry.last_update > timeout_duration) {
            EVLOG_warning << format_module_id(address, telemetry.serial_number)
                          << ": module communication expired (timeout: " << device_connection_timeout_s
                          << "s). Removing from active modules.";
            it = telemetries.erase(it);
            {
                std::lock_guard<std::mutex> lock(active_modules_mutex);
                active_module_addresses.erase(
                    std::remove(active_module_addresses.begin(), active_module_addresses.end(), address),
                    active_module_addresses.end());
            }
            ++removed_count;
        } else {
            ++it;
        }
    }

    // Update active_module_addresses to match current telemetries keys
    {
        // Check CommunicationFault state: trigger if no active modules but we expect some, clear otherwise
        if (removed_count != 0 && telemetries.empty()) {
            // No modules responding - trigger CommunicationFault
            signalError(0xFF, Error::CommunicationFault, true); // Use address 0xFF for system-wide fault
        } else if (!telemetries.empty()) {
            // At least one module responding - clear CommunicationFault
            signalError(0xFF, Error::CommunicationFault, false); // Use address 0xFF for system-wide fault
        }
    }

    return removed_count;
}

void InfyCanDevice::poll_status_handler() {
    // Remove expired telemetry entries
    size_t removed_count = remove_expired_telemetry_entries();

    if (removed_count > 0) {
        EVLOG_info << "Infy: Removed " << removed_count << " expired modules. "
                   << "Active modules remaining: " << active_module_addresses.size();
        // signal the telemetry updates
        signalCapabilitiesUpdate(telemetries);
        signalVoltageCurrent(telemetries);
    }

    // --- Telemetry Polling (paced on CAN event thread) ---
    static const std::vector<uint8_t> empty_read_payload(8, 0);

    clear_paced_tx_queue();

    std::vector<uint8_t> poll_addresses;
    if (operating_mode == OperatingMode::GROUP_DISCOVERY) {
        // Request a list of active modules in the group, this updates `active_module_addresses` based on responses.
        enqueue_poll_command(group_address, can_packet_acdc::ReadModuleCount::CMD_ID, empty_read_payload, true);

        // Only poll addresses that are currently active based on received telemetry.
        std::lock_guard<std::mutex> lock(active_modules_mutex);
        poll_addresses = active_module_addresses;
    } else if (operating_mode == OperatingMode::FIXED_ADDRESS) {
        // Poll all configured module addresses in FIXED_ADDRESS mode, even if they are not currently active.
        // This allows the system to detect modules that have come back online after a temporary communication loss.
        poll_addresses = module_addresses;
    }

    for (const auto& addr : poll_addresses) {
        enqueue_poll_command(addr, can_packet_acdc::ReadModuleVI::CMD_ID, empty_read_payload);
        enqueue_poll_command(addr, can_packet_acdc::PowerModuleStatus::CMD_ID, empty_read_payload);
        enqueue_poll_command(addr, can_packet_acdc::ReadModuleVIAfterDiode::CMD_ID, empty_read_payload);

        auto it = telemetries.find(addr);
        if (it == telemetries.end() || it->second.serial_number.empty()) {
            enqueue_poll_command(addr, can_packet_acdc::ReadModuleCapabilities::CMD_ID, empty_read_payload);
            enqueue_poll_command(addr, can_packet_acdc::ReadModuleBarcode::CMD_ID, empty_read_payload);
        }
    }

    start_paced_tx_cycle();
}

bool InfyCanDevice::switch_on_off(bool on) {
    std::lock_guard<std::mutex> lock(active_modules_mutex);

    EVLOG_info << "Infy: switch_on_off(" << on << ") - active modules: " << active_module_addresses.size();

    bool success = true;
    if (active_module_addresses.empty()) {
        EVLOG_warning << "Infy: No active modules to send switch_on_off command to.";
        return false;
    }
    for (const auto& addr : active_module_addresses) {
        bool tx_result = send_command(addr, can_packet_acdc::SetModuleOnOff(on));
        success &= tx_result;
    }
    return success;
}

bool InfyCanDevice::set_voltage_current(float voltage, float current) {
    std::lock_guard<std::mutex> lock(active_modules_mutex);

    EVLOG_info << "Infy: set_voltage_current(" << voltage << "V, " << current
               << "A) - active modules: " << active_module_addresses.size();

    // Validate that we have active modules before attempting to divide current
    const size_t module_count = active_module_addresses.size();
    if (module_count == 0) {
        EVLOG_warning << "Infy: No active modules to set voltage/current.";
        return false;
    }

    // Current is shared between all modules - safe division guaranteed by check above
    const float current_per_module = current / static_cast<float>(module_count);
    bool success = true;

    for (const auto& addr : active_module_addresses) {
        bool tx_result = send_command(addr, can_packet_acdc::SetModuleVI(voltage, current_per_module));
        success &= tx_result;
    }
    return success;
}

bool InfyCanDevice::send_command_impl(uint8_t destination_address, uint8_t command_number,
                                      const std::vector<uint8_t>& payload, bool group) {
    uint32_t can_id = can_packet_acdc::encode_can_id(
        controller_address, destination_address, command_number,
        group ? InfyProtocol::DEVICE_GROUP_MODULE : InfyProtocol::DEVICE_SINGLE_MODULE, 0x00);
    auto result = _tx(can_id, payload);
    if (not result) {
        EVLOG_warning << "Infy: CAN transmission failed for can_id: 0x" << std::hex << std::uppercase << can_id;
    }
    return result;
}

void InfyCanDevice::enqueue_poll_command(uint8_t destination_address, uint8_t command_number,
                                         const std::vector<uint8_t>& payload, bool group) {
    uint32_t can_id = can_packet_acdc::encode_can_id(
        controller_address, destination_address, command_number,
        group ? InfyProtocol::DEVICE_GROUP_MODULE : InfyProtocol::DEVICE_SINGLE_MODULE, 0x00);
    enqueue_paced_tx(can_id, payload);
}

void InfyCanDevice::handle_module_count_packet(const std::vector<uint8_t>& payload) {
    can_packet_acdc::ReadModuleCount n(payload);
    if (operating_mode != OperatingMode::GROUP_DISCOVERY) {
        return;
    }

    // n count must be at least 1, it is the module that it is answering (the group master)
    expected_module_count = n.count;
    if (expected_module_count != telemetries.size()) {
        EVLOG_info << "Infy: System reports " << expected_module_count
                   << " total modules in group, we might have lost some modules, waiting for timeout or recovery";
    }

    // Initially assume all configured modules are active (will be updated based on responses)
    {
        std::lock_guard<std::mutex> lock(active_modules_mutex);
        active_module_addresses.clear();
        active_module_addresses.reserve(n.count); // Pre-allocate before assignment
        for (uint8_t i = 0; i < n.count; ++i) {
            active_module_addresses.push_back(i);
        }
    }

    if (!initialized) {
        initialized = true;
        EVLOG_info << "Infy: Received first module count packet. Make sure that the modules are off";
        switch_on_off(false);
    }
}

void InfyCanDevice::handle_simple_telemetry_update(uint8_t source_address, const std::vector<uint8_t>& payload,
                                                   uint8_t command_number) {
    auto& telemetry = telemetries[source_address];

    switch (command_number) {
    case can_packet_acdc::ReadModuleVI::CMD_ID: {
        can_packet_acdc::ReadModuleVI vi(payload);
        telemetry.voltage = vi.voltage;
        telemetry.current = vi.current;
        signalVoltageCurrent(telemetries);
    } break;
    case can_packet_acdc::ReadModuleVIAfterDiode::CMD_ID: {
        can_packet_acdc::ReadModuleVIAfterDiode v_after_diode(payload);
        telemetry.v_ext = v_after_diode.v_ext;
        telemetry.i_avail = v_after_diode.i_avail;
        signalVoltageCurrent(telemetries);
    } break;
    case can_packet_acdc::ReadModuleCapabilities::CMD_ID: {
        can_packet_acdc::ReadModuleCapabilities caps(payload);
        telemetry.valid_caps = true;
        telemetry.dc_max_output_voltage = caps.max_voltage;
        telemetry.dc_min_output_voltage = caps.min_voltage;
        telemetry.dc_max_output_current = caps.max_current;
        telemetry.dc_rated_output_power = caps.rated_power;
        EVLOG_info << format_module_id(source_address) << ": capabilities: " << caps.max_voltage << "V / "
                   << caps.min_voltage << "V, " << caps.max_current << "A, power " << caps.rated_power << "W";
        signalCapabilitiesUpdate(telemetries);
        mark_module_active(source_address);
    } break;
    }
}

void InfyCanDevice::mark_module_active(uint8_t source_address) {
    // In FIXED_ADDRESS mode the active set is driven purely by received telemetry: any message from a
    // configured module means it is alive, so (re-)add it if it had been dropped after a comm timeout.
    // GROUP_DISCOVERY derives the active set from ReadModuleCount responses instead.
    if (operating_mode != OperatingMode::FIXED_ADDRESS) {
        return;
    }
    std::lock_guard<std::mutex> lock(active_modules_mutex);
    if (std::find(active_module_addresses.begin(), active_module_addresses.end(), source_address) ==
        active_module_addresses.end()) {
        active_module_addresses.push_back(source_address);
        EVLOG_info << format_module_id(source_address) << ": module re-activated after receiving telemetry.";
    }
}

void InfyCanDevice::check_and_signal_error_status_change(uint8_t source_address,
                                                         const can_packet_acdc::PowerModuleStatus& new_status,
                                                         const can_packet_acdc::PowerModuleStatus& old_status) {
    // Helper lambda to reduce repetition in error status checking
    auto check_status_change = [this, source_address](bool new_val, bool old_val, Error error_type) {
        if (new_val != old_val) {
            signalError(source_address, error_type, new_val);
        }
    };

    // Check all error status changes (excluding fields marked as "Status")
    check_status_change(new_status.fault_alarm, old_status.fault_alarm, Error::InternalFault);
    check_status_change(new_status.over_temperature_alarm, old_status.over_temperature_alarm, Error::OverTemperature);
    check_status_change(new_status.output_over_voltage_alarm, old_status.output_over_voltage_alarm, Error::OverVoltage);
    check_status_change(new_status.fan_fault_alarm, old_status.fan_fault_alarm, Error::FanFault);
    check_status_change(new_status.input_phase_lost_alarm, old_status.input_phase_lost_alarm, Error::InputPhaseLoss);
    check_status_change(new_status.output_short_current, old_status.output_short_current, Error::OverCurrent);
    check_status_change(new_status.communication_interrupt_alarm, old_status.communication_interrupt_alarm,
                        Error::CommunicationFault);
    check_status_change(new_status.input_low_voltage_alarm, old_status.input_low_voltage_alarm, Error::UnderVoltage);
    check_status_change(new_status.input_unbalanced_alarm, old_status.input_unbalanced_alarm, Error::InputVoltage);
    check_status_change(new_status.input_over_voltage_protection, old_status.input_over_voltage_protection,
                        Error::InputVoltage);
    check_status_change(new_status.protection_alarm, old_status.protection_alarm, Error::InternalFault);
    check_status_change(new_status.load_sharing_alarm, old_status.load_sharing_alarm, Error::InternalFault);
    check_status_change(new_status.discharge_abnormal, old_status.discharge_abnormal, Error::InternalFault);
}

std::string InfyCanDevice::format_module_id(uint8_t address, const std::string& serial_number) const {
    std::stringstream ss;
    ss << "Infy[0x" << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << static_cast<int>(address);
    if (!serial_number.empty()) {
        ss << "/" << serial_number;
    }
    ss << "]";
    return ss.str();
}
