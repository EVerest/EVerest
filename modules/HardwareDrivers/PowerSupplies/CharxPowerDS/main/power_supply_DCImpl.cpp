// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "power_supply_DCImpl.hpp"

#include <thread>

namespace module {
namespace main {

namespace {
// Transfers in a row that failed to reach the bus before the socket is opened again (the interface may have
// been removed and created again, e.g. a USB adapter that was replugged).
constexpr int REOPEN_AFTER_IO_ERRORS = 20;

std::chrono::milliseconds ms(double seconds) {
    return std::chrono::milliseconds(static_cast<int64_t>(seconds * 1000.0));
}

const char* error_type(charx::ErrorType type) {
    switch (type) {
    case charx::ErrorType::CommunicationFault:
        return "power_supply_DC/CommunicationFault";
    case charx::ErrorType::HardwareFault:
        return "power_supply_DC/HardwareFault";
    case charx::ErrorType::OverTemperature:
        return "power_supply_DC/OverTemperature";
    case charx::ErrorType::UnderTemperature:
        return "power_supply_DC/UnderTemperature";
    case charx::ErrorType::UnderVoltageAC:
        return "power_supply_DC/UnderVoltageAC";
    case charx::ErrorType::OverVoltageAC:
        return "power_supply_DC/OverVoltageAC";
    case charx::ErrorType::OverVoltageDC:
        return "power_supply_DC/OverVoltageDC";
    case charx::ErrorType::OverCurrentDC:
        return "power_supply_DC/OverCurrentDC";
    case charx::ErrorType::OverCurrentAC:
        return "power_supply_DC/OverCurrentAC";
    case charx::ErrorType::VendorError:
        return "power_supply_DC/VendorError";
    }
    return "power_supply_DC/VendorError";
}
} // namespace

void power_supply_DCImpl::init() {
    charx::ControllerConfig c;
    c.contactor = (mod->config.contactor == "B") ? 2 : 1;
    c.min_export_voltage_V = mod->config.min_export_voltage_V;
    c.max_export_voltage_V = mod->config.max_export_voltage_V;
    c.max_export_current_A = mod->config.max_export_current_A;
    c.max_export_power_W = mod->config.max_export_power_W;
    c.power_on_timeout = ms(mod->config.power_on_timeout_s);
    c.power_off_timeout = ms(mod->config.power_off_timeout_s);
    c.off_current_threshold_A = mod->config.off_current_threshold_A;
    charx::ControllerOutputs& outputs = *this; // private base, accessible here only
    controller = std::make_unique<charx::Controller>(c, outputs);
}

void power_supply_DCImpl::ready() {
    const auto poll_interval = std::chrono::milliseconds(mod->config.poll_interval_ms);
    while (true) {
        if (!can) {
            try {
                can = std::make_unique<charx::Canopen>(mod->config.device, mod->config.node_id,
                                                       mod->config.master_node_id,
                                                       std::chrono::milliseconds(mod->config.sdo_timeout_ms));
                can->start_remote_node();
                EVLOG_info << "CAN " << mod->config.device << " open, node " << mod->config.node_id;
            } catch (const std::exception& e) {
                controller->report_unreachable(e.what());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        controller->step(*can, std::chrono::steady_clock::now());

        if (can->io_errors_in_row() >= REOPEN_AFTER_IO_ERRORS) {
            EVLOG_warning << "CAN " << mod->config.device << ": frames cannot be sent, opening the interface again";
            can.reset();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        // sleep until the next poll, or less when EvseManager changes a target
        controller->wait_for_target_change(poll_interval);
    }
}

void power_supply_DCImpl::handle_setMode(types::power_supply_DC::Mode& mode,
                                         types::power_supply_DC::ChargingPhase& phase) {
    const bool exporting = (mode == types::power_supply_DC::Mode::Export);
    controller->set_mode(exporting);
    if (!exporting) {
        // Interface rule: return only when it is safe to open the charger relays (power stage off or current
        // below the threshold), but never block for long.
        controller->wait_until_off_safe(ms(mod->config.power_off_timeout_s));
    }
}

void power_supply_DCImpl::handle_setExportVoltageCurrent(double& voltage, double& current) {
    controller->set_export_setpoint(voltage, current);
}

void power_supply_DCImpl::handle_setImportVoltageCurrent(double& voltage, double& current) {
    // the module only exports
}

void power_supply_DCImpl::on_capabilities(const charx::Capabilities& c) {
    types::power_supply_DC::Capabilities caps;
    caps.bidirectional = false;
    caps.min_export_voltage_V = c.min_export_voltage_V;
    caps.max_export_voltage_V = c.max_export_voltage_V;
    caps.min_export_current_A = 0;
    caps.max_export_current_A = c.max_export_current_A;
    caps.max_export_power_W = c.max_export_power_W;
    caps.current_regulation_tolerance_A = c.current_regulation_tolerance_A;
    caps.peak_current_ripple_A = c.peak_current_ripple_A;
    publish_capabilities(caps);
}

void power_supply_DCImpl::on_mode(bool exporting) {
    publish_mode(exporting ? types::power_supply_DC::Mode::Export : types::power_supply_DC::Mode::Off);
}

void power_supply_DCImpl::on_measurement(double voltage_V, double current_A) {
    types::power_supply_DC::VoltageCurrent vc;
    vc.voltage_V = voltage_V;
    vc.current_A = current_A;
    publish_voltage_current(vc);
}

void power_supply_DCImpl::on_error(charx::ErrorType type, bool active, const std::string& message) {
    const char* t = error_type(type);
    if (active) {
        raise_error(error_factory->create_error(t, "", message, Everest::error::Severity::High));
    } else {
        clear_error(t);
    }
}

} // namespace main
} // namespace module
