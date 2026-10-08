// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "power_supply_DCImpl.hpp"

#include <algorithm>
#include <cmath>
#include <thread>

#include <fmt/core.h>

namespace module {
namespace main {

namespace {
constexpr const char* ERR_COMM = "power_supply_DC/CommunicationFault";
constexpr const char* ERR_HW = "power_supply_DC/HardwareFault";
constexpr const char* ERR_OT = "power_supply_DC/OverTemperature";
constexpr const char* ERR_UT = "power_supply_DC/UnderTemperature";
constexpr const char* ERR_UVAC = "power_supply_DC/UnderVoltageAC";
constexpr const char* ERR_OVAC = "power_supply_DC/OverVoltageAC";
constexpr const char* ERR_OVDC = "power_supply_DC/OverVoltageDC";
constexpr const char* ERR_OCDC = "power_supply_DC/OverCurrentDC";
constexpr const char* ERR_OCAC = "power_supply_DC/OverCurrentAC";
constexpr const char* ERR_VENDOR = "power_supply_DC/VendorError";

// Commands that the module did not follow are re-sent after this time.
constexpr auto RESEND_AFTER = std::chrono::seconds(3);

bool bit(uint32_t v, int b) {
    return (v >> b) & 1u;
}

int32_t milli(double v) {
    return static_cast<int32_t>(std::lround(v * 1000.0));
}
} // namespace

bool power_supply_DCImpl::power_on() const {
    // Flags1: PFC off and DC output side off both clear = power stage running
    return status_valid && !bit(flags1, charx::flag::PfcOff) && !bit(flags1, charx::flag::DcOutputSideOff);
}

bool power_supply_DCImpl::contactor_closed() const {
    return status_valid && ds_status == contactor_value;
}

void power_supply_DCImpl::set_error(const std::string& type, bool active, const std::string& message) {
    const bool is_active = error_state_monitor->is_error_active(type, "");
    if (active && !is_active) {
        EVLOG_warning << "raising " << type << ": " << message;
        raise_error(error_factory->create_error(type, "", message, Everest::error::Severity::High));
    } else if (!active && is_active) {
        EVLOG_info << "clearing " << type;
        clear_error(type);
    }
}

charx::SdoResult power_supply_DCImpl::write(charx::Obj o, int32_t value, const char* what) {
    auto r = can->write(o, value);
    if (!r.ok() && !(r.status == charx::SdoResult::Status::Abort && r.abort_code == 0x08000022)) {
        EVLOG_warning << fmt::format("write {} ({:04X}:{:02X} = {}) failed: {}", what, o.index, o.sub, value,
                                     r.describe());
    }
    return r;
}

void power_supply_DCImpl::init() {
    contactor_value = (mod->config.contactor == "B") ? 2 : 1;
}

bool power_supply_DCImpl::read_capabilities() {
    auto vmin = can->read(charx::obj::v_min_avl);
    auto vmax = can->read(charx::obj::v_max_avl);
    auto imax = can->read(charx::obj::i_max_avl);
    if (!vmin.ok() || !vmax.ok() || !imax.ok()) {
        return false;
    }
    types::power_supply_DC::Capabilities caps;
    {
        std::lock_guard<std::mutex> lock(mtx);
        cap_v_min = std::max(mod->config.min_export_voltage_V, vmin.value / 1000.0);
        cap_v_max = std::min(mod->config.max_export_voltage_V, vmax.value / 1000.0);
        cap_i_max = std::min(mod->config.max_export_current_A, imax.value / 1000.0);
        caps.bidirectional = false;
        caps.min_export_voltage_V = cap_v_min;
        caps.max_export_voltage_V = cap_v_max;
        caps.min_export_current_A = 0;
        caps.max_export_current_A = cap_i_max;
        caps.max_export_power_W = mod->config.max_export_power_W;
        // per Phoenix Contact: +-1 % of max current, at least 0.3 A; ripple 1.3 %
        // of max current
        caps.current_regulation_tolerance_A = std::max(0.3, 0.01 * cap_i_max);
        caps.peak_current_ripple_A = 0.013 * cap_i_max;
    }
    EVLOG_info << fmt::format("capabilities: {:.0f}-{:.0f} V, 0-{:.0f} A, {:.0f} W", caps.min_export_voltage_V,
                              caps.max_export_voltage_V, caps.max_export_current_A, caps.max_export_power_W);
    publish_capabilities(caps);
    return true;
}

bool power_supply_DCImpl::poll_status() {
    static unsigned cycle = 0;
    auto f1 = can->read(charx::obj::flags1);
    auto ds = can->read(charx::obj::ds_output_status);
    auto v = can->read(charx::obj::v_meas);
    auto i = can->read(charx::obj::i_meas);
    if (!f1.ok() || !ds.ok() || !v.ok() || !i.ok()) {
        return false;
    }
    std::optional<int32_t> ce;
    if (cycle++ % 8 == 0) {
        auto r = can->read(charx::obj::contactor_error);
        if (r.ok()) {
            ce = r.value;
        }
    }
    std::lock_guard<std::mutex> lock(mtx);
    flags1 = static_cast<uint32_t>(f1.value);
    ds_status = ds.value;
    v_meas = v.value / 1000.0;
    i_meas = i.value / 1000.0;
    if (ce) {
        contactor_error = *ce;
    }
    status_valid = true;
    return true;
}

void power_supply_DCImpl::actuate() {
    bool want_export;
    double v, i;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lock(mtx);
        want_export = target_export;
        v = std::clamp(target_voltage, cap_v_min, cap_v_max);
        i = std::clamp(target_current, 0.0, cap_i_max);
        gen = target_generation;
    }
    const auto now = Clock::now();
    static bool applied_export = false;

    if (gen != applied_generation) {
        if (want_export && !applied_export) {
            EVLOG_info << fmt::format("switch on requested: {:.1f} V / {:.1f} A", v, i);
            sent_contactor = power_on_accepted = sent_current = false;
            on_requested_at = now;
            off_requested_at.reset();
        } else if (!want_export && applied_export) {
            EVLOG_info << "switch off requested";
            sent_off = false;
            off_requested_at = now;
            on_requested_at.reset();
        }
        sent_voltage = false; // setpoints may have changed
        sent_current = false;
        applied_export = want_export;
        applied_generation = gen;
    }

    if (want_export) {
        if (!sent_voltage && write(charx::obj::v_set, milli(v), "voltage setpoint").ok()) {
            sent_voltage = true;
        }
        if (!sent_contactor) {
            // current stays 0 until PowerOn is accepted (as the PS_CHARX_POWER
            // library does it)
            if (write(charx::obj::i_set, 0, "current 0").ok() &&
                write(charx::obj::ds_output, contactor_value, "close contactor").ok()) {
                sent_contactor = true;
                last_resend = now;
            }
        }
        if (sent_contactor && !power_on_accepted) {
            // refused with abort 0x08000022 until the contactor reports closed (~0.8
            // s) - retry every cycle
            if (write(charx::obj::readiness, 1, "PowerOn").ok()) {
                power_on_accepted = true;
                EVLOG_info << "PowerOn accepted";
            }
        }
        if (power_on_accepted && !sent_current && write(charx::obj::i_set, milli(i), "current setpoint").ok()) {
            sent_current = true;
        }
        // The module did not follow (contactor open or PFC off long after
        // acceptance): send everything again. DcOutputSideOff alone is not a
        // reason: the module drops it ~1.3 s while switching its DC stages.
        bool not_following;
        {
            std::lock_guard<std::mutex> lock(mtx);
            not_following = status_valid && (!contactor_closed() || bit(flags1, charx::flag::PfcOff));
        }
        if (power_on_accepted && not_following && now - last_resend > RESEND_AFTER) {
            EVLOG_warning << "module does not follow the switch-on, sending the commands again";
            sent_voltage = sent_contactor = power_on_accepted = sent_current = false;
            last_resend = now;
        }
    } else {
        if (!sent_off) {
            const bool a = write(charx::obj::readiness, 0, "PowerOff").ok();
            const bool b = write(charx::obj::ds_output, 0, "open contactor").ok();
            const bool c = write(charx::obj::i_set, 0, "current 0").ok();
            sent_off = a && b && c;
            last_resend = now;
        }
        bool not_off;
        {
            std::lock_guard<std::mutex> lock(mtx);
            not_off = status_valid && (power_on() || ds_status != 0);
        }
        if (sent_off && not_off && now - last_resend > RESEND_AFTER) {
            EVLOG_warning << "module is not off, sending PowerOff and contactor open again";
            sent_off = false;
        }
    }
}

void power_supply_DCImpl::handle_errors() {
    uint32_t f;
    int32_t ce;
    bool on, closed, off_confirmed, want_export;
    {
        std::lock_guard<std::mutex> lock(mtx);
        f = flags1;
        ce = contactor_error;
        on = power_on();
        closed = contactor_closed();
        off_confirmed = !on && ds_status == 0;
        want_export = target_export;
    }
    using namespace charx::flag;
    if (f != last_logged_flags) {
        EVLOG_info << fmt::format("module flags1 0x{:08X} [{}]", f, charx::flags1_to_string(f));
        last_logged_flags = f;
    }
    const std::string flags = charx::flags1_to_string(f);
    set_error(ERR_HW,
              bit(f, InternalFailure) || bit(f, ConverterError) || bit(f, ShortCircuit) || bit(f, EmergencyStop) ||
                  bit(f, DischargeProblem) || bit(f, FanFault) || bit(f, ModuleIdRepetition) || ce != 0,
              fmt::format("module flags [{}], contactor error {}", flags, ce));
    set_error(ERR_OT, bit(f, Otp), "module over temperature");
    set_error(ERR_UT, bit(f, Utp), "module under temperature");
    set_error(ERR_UVAC, bit(f, UvpInput) || bit(f, AcPhaseLoss), fmt::format("AC input [{}]", flags));
    set_error(ERR_OVAC, bit(f, OvpInput), "AC input over voltage");
    set_error(ERR_OVDC, bit(f, OvpOutput), "DC output over voltage");
    set_error(ERR_OCDC, bit(f, Ocp) || bit(f, OverPower), fmt::format("DC output [{}]", flags));
    set_error(ERR_OCAC, bit(f, AcOverload), "AC overload");

    // Validity: a command the module does not confirm in time is a fault, never a
    // silent state.
    const auto now = Clock::now();
    const bool on_late = want_export && on_requested_at && !(on && closed) &&
                         now - *on_requested_at > std::chrono::duration<double>(mod->config.power_on_timeout_s);
    const bool off_late = !want_export && off_requested_at && !off_confirmed &&
                          now - *off_requested_at > std::chrono::duration<double>(mod->config.power_off_timeout_s);
    if (want_export && on && closed) {
        on_requested_at.reset();
    }
    if (!want_export && off_confirmed) {
        off_requested_at.reset();
    }
    set_error(ERR_VENDOR, on_late || off_late,
              on_late ? fmt::format("switch-on not confirmed within {} s", mod->config.power_on_timeout_s)
                      : fmt::format("switch-off not confirmed within {} s", mod->config.power_off_timeout_s));
}

void power_supply_DCImpl::update_reported_mode() {
    types::power_supply_DC::Mode m;
    {
        std::lock_guard<std::mutex> lock(mtx);
        m = (power_on() && contactor_closed()) ? types::power_supply_DC::Mode::Export
                                               : types::power_supply_DC::Mode::Off;
    }
    if (!reported_mode || *reported_mode != m) {
        EVLOG_info << "module reports " << (m == types::power_supply_DC::Mode::Export ? "Export" : "Off");
        publish_mode(m);
        reported_mode = m;
    }
}

void power_supply_DCImpl::ready() {
    bool caps_published = false;
    while (true) {
        if (!can) {
            try {
                can = std::make_unique<charx::Canopen>(mod->config.device, mod->config.node_id,
                                                       mod->config.master_node_id,
                                                       std::chrono::milliseconds(mod->config.sdo_timeout_ms));
                can->nmt_start_remote_node();
                EVLOG_info << "CAN " << mod->config.device << " open, node " << mod->config.node_id;
            } catch (const std::exception& e) {
                set_error(ERR_COMM, true, e.what());
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }
        }

        if (!caps_published) {
            caps_published = read_capabilities();
        }

        const bool ok = caps_published && poll_status();
        if (!ok) {
            if (++comm_failures >= 3) {
                set_error(ERR_COMM, true, "no answer from the CHARX module");
                // after recovery, everything is sent again (interface rule: re-apply,
                // then clear the error)
                applied_generation = ~0ull;
                sent_off = false;
                std::lock_guard<std::mutex> lock(mtx);
                status_valid = false;
            }
            if (comm_failures % 20 == 0) {
                can->nmt_start_remote_node();
            }
        } else {
            actuate();
            if (comm_failures > 0) {
                comm_failures = 0;
                set_error(ERR_COMM, false, "");
            }
            handle_errors();
            update_reported_mode();
            types::power_supply_DC::VoltageCurrent vc;
            {
                std::lock_guard<std::mutex> lock(mtx);
                vc.voltage_V = v_meas;
                vc.current_A = i_meas;
            }
            publish_voltage_current(vc);
        }
        cv.notify_all();

        // sleep until the next poll, or earlier when EvseManager changes something
        std::unique_lock<std::mutex> lock(mtx);
        const uint64_t seen = target_generation;
        cv.wait_for(lock, std::chrono::milliseconds(mod->config.poll_interval_ms),
                    [&] { return target_generation != seen; });
    }
}

void power_supply_DCImpl::handle_setMode(types::power_supply_DC::Mode& mode,
                                         types::power_supply_DC::ChargingPhase& phase) {
    const bool want_export = (mode == types::power_supply_DC::Mode::Export);
    std::unique_lock<std::mutex> lock(mtx);
    if (want_export != target_export) {
        target_export = want_export;
        target_generation++;
        cv.notify_all();
    }
    if (!want_export) {
        // Interface rule 3: return only when it is safe to open the charger relays
        // (current below threshold or power stage off), but never block for long.
        cv.wait_for(lock, std::chrono::duration<double>(mod->config.power_off_timeout_s), [&] {
            return status_valid && (!power_on() || std::fabs(i_meas) < mod->config.off_current_threshold_A);
        });
    }
}

void power_supply_DCImpl::handle_setExportVoltageCurrent(double& voltage, double& current) {
    std::lock_guard<std::mutex> lock(mtx);
    if (voltage != target_voltage || current != target_current) {
        target_voltage = voltage;
        target_current = current;
        target_generation++;
        cv.notify_all();
    }
}

void power_supply_DCImpl::handle_setImportVoltageCurrent(double& voltage, double& current) {
    // the module only exports
}

} // namespace main
} // namespace module
