// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include "power_supply_DCImpl.hpp"

namespace module {
namespace main {

void power_supply_DCImpl::init() {
    caps.bidirectional = true;

    caps.current_regulation_tolerance_A = 1;
    caps.peak_current_ripple_A = 0.2;

    caps.min_export_current_A = 1;
    caps.max_export_current_A = 73.3;
    caps.min_export_voltage_V = 200;
    caps.max_export_voltage_V = 1000;
    caps.max_export_power_W = 22000;
    caps.conversion_efficiency_export = 0.95;

    caps.max_import_current_A = 73.3;
    caps.min_import_current_A = 1;
    caps.max_import_power_W = 22000;
    caps.min_import_voltage_V = 200;
    caps.max_import_voltage_V = 1000;
    caps.conversion_efficiency_import = 0.95;

    mod->acdc.signalVoltageCurrent.connect([this](float voltage, float current) {
        types::power_supply_DC::VoltageCurrent vc;
        vc.current_A = current;
        vc.voltage_V = voltage;
        publish_voltage_current(vc);
    });

    mod->acdc.signalModuleStatus.connect(
        [this](can_packet_acdc::PowerModuleStatus status, can_packet_acdc::InverterStatus inverter_status) {
            static bool firsttime = true;

            // Publish mode changes
            types::power_supply_DC::Mode mode;

            if (status.fault_alarm) {
                mode = types::power_supply_DC::Mode::Fault;
            } else if (status.dc_side_off) {
                mode = types::power_supply_DC::Mode::Off;
            } else if (inverter_status.invert_mode) {
                mode = types::power_supply_DC::Mode::Import;
            } else {
                mode = types::power_supply_DC::Mode::Export;
            }

            if (last_publish_mode != mode || firsttime) {
                publish_mode(mode);
                last_publish_mode = mode;
                firsttime = false;
            }
        });

    mod->acdc.switch_on_off(false);
    mod->acdc.adjust_power_factor(1.0);
    mod->acdc.set_output_mode(InfyCanDevice::OutputMode::Automatic);
}

void power_supply_DCImpl::ready() {
    publish_capabilities(caps);
}

void power_supply_DCImpl::handle_setMode(types::power_supply_DC::Mode& mode,
                                         types::power_supply_DC::ChargingPhase& phase) {
    std::scoped_lock lock(settings_mutex);

    commanded_mode = mode;

    if (mode == types::power_supply_DC::Mode::Off) {
        below_minimum = false;
        mod->acdc.switch_on_off(false);
        mod->acdc.set_inverter_mode(false);
    } else if (mode == types::power_supply_DC::Mode::Export) {
        mod->acdc.set_inverter_mode(false);
        mod->acdc.switch_on_off(output_enabled());
    } else if (mode == types::power_supply_DC::Mode::Import) {
        mod->acdc.set_inverter_mode(true);
        mod->acdc.switch_on_off(output_enabled());
    } else if (mode == types::power_supply_DC::Mode::Fault) {
        below_minimum = false;
        mod->acdc.switch_on_off(false);
        mod->acdc.set_inverter_mode(false);
    }
};

void power_supply_DCImpl::handle_setExportVoltageCurrent(double& voltage, double& current) {

    if (voltage > caps.max_export_voltage_V)
        voltage = caps.max_export_voltage_V;
    else if (voltage < caps.min_export_voltage_V)
        voltage = caps.min_export_voltage_V;

    const bool below_minimum_requested = current < caps.min_export_current_A;
    if (current > caps.max_export_current_A)
        current = caps.max_export_current_A;
    else if (below_minimum_requested)
        current = 0.;

    std::scoped_lock lock(settings_mutex);

    exportVoltage = voltage;
    exportCurrentLimit = current;

    if (below_minimum_requested) {
        set_below_minimum(true);
        return;
    }

    EVLOG_info << "Updating voltage/current via CAN: " << exportVoltage << "V / " << exportCurrentLimit << "A";
    mod->acdc.set_voltage_current(exportVoltage, exportCurrentLimit);
    set_below_minimum(false);
};

void power_supply_DCImpl::handle_setImportVoltageCurrent(double& voltage, double& current) {

    if (caps.min_import_voltage_V.has_value() && caps.max_import_current_A.has_value()) {

        if (voltage > caps.max_import_voltage_V.value())
            voltage = caps.max_import_voltage_V.value();
        else if (voltage < caps.min_import_voltage_V.value())
            voltage = caps.min_import_voltage_V.value();

        const bool below_minimum_requested = current < caps.min_import_current_A.value();
        if (current > caps.max_import_current_A.value())
            current = caps.max_import_current_A.value();
        else if (below_minimum_requested)
            current = 0.;

        std::scoped_lock lock(settings_mutex);
        minImportVoltage = voltage;
        importCurrentLimit = current;

        if (below_minimum_requested) {
            set_below_minimum(true);
            return;
        }

        EVLOG_info << "Updating voltage/current via CAN: " << minImportVoltage << "V / " << importCurrentLimit << "A";
        mod->acdc.set_voltage_current(minImportVoltage, importCurrentLimit);
        set_below_minimum(false);
    }
}

bool power_supply_DCImpl::output_enabled() const {
    return (commanded_mode == types::power_supply_DC::Mode::Export or
            commanded_mode == types::power_supply_DC::Mode::Import) and
           not below_minimum;
}

void power_supply_DCImpl::set_below_minimum(bool below) {
    if (below_minimum == below) {
        return;
    }
    below_minimum = below;
    EVLOG_info << "Current " << (below ? "below" : "at or above") << " minimum, output "
               << (output_enabled() ? "on" : "off");
    mod->acdc.switch_on_off(output_enabled());
}

} // namespace main
} // namespace module
