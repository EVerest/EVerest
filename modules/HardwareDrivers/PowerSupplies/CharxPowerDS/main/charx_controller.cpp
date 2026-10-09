// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "charx_controller.hpp"

#include <algorithm>
#include <cmath>

#include <everest/logging.hpp>
#include <fmt/core.h>

namespace charx {

namespace {
bool bit(uint32_t v, int b) {
    return (v >> b) & 1u;
}

int32_t milli(double v) {
    return static_cast<int32_t>(std::lround(v * 1000.0));
}

const char* error_name(ErrorType type) {
    switch (type) {
    case ErrorType::CommunicationFault:
        return "CommunicationFault";
    case ErrorType::HardwareFault:
        return "HardwareFault";
    case ErrorType::OverTemperature:
        return "OverTemperature";
    case ErrorType::UnderTemperature:
        return "UnderTemperature";
    case ErrorType::UnderVoltageAC:
        return "UnderVoltageAC";
    case ErrorType::OverVoltageAC:
        return "OverVoltageAC";
    case ErrorType::OverVoltageDC:
        return "OverVoltageDC";
    case ErrorType::OverCurrentDC:
        return "OverCurrentDC";
    case ErrorType::OverCurrentAC:
        return "OverCurrentAC";
    case ErrorType::VendorError:
        return "VendorError";
    }
    return "?";
}
} // namespace

Controller::Controller(const ControllerConfig& config_, ControllerOutputs& outputs_) :
    config(config_), outputs(outputs_) {
}

void Controller::set_mode(bool exporting) {
    std::lock_guard<std::mutex> lock(mtx);
    if (exporting != target_export) {
        target_export = exporting;
        target_generation++;
        cv.notify_all();
    }
}

void Controller::set_export_setpoint(double voltage_V, double current_A) {
    std::lock_guard<std::mutex> lock(mtx);
    if (voltage_V != target_voltage || current_A != target_current) {
        target_voltage = voltage_V;
        target_current = current_A;
        target_generation++;
        cv.notify_all();
    }
}

bool Controller::stage_running() const {
    return status_valid && !bit(flags1, flag::PfcOff) && !bit(flags1, flag::DcOutputSideOff);
}

bool Controller::stage_alive() const {
    // DcOutputSideOff alone is tolerated for dc_stage_switch_tolerance only (see step())
    return status_valid && !bit(flags1, flag::PfcOff) && (!bit(flags1, flag::DcOutputSideOff) || dc_side_off_tolerated);
}

bool Controller::contactor_closed() const {
    return status_valid && ds_status == config.contactor;
}

bool Controller::off_confirmed() const {
    return status_valid && !stage_running() && ds_status == 0;
}

bool Controller::low_current_after_power_off() const {
    // Only a reading polled after PowerOff was written counts: before it, the current may still be ramping up
    // to the last setpoint, and the flags blink while the DC stages switch.
    return status_valid && power_off_written_at_poll && poll_count > *power_off_written_at_poll &&
           std::fabs(i_meas) < config.off_current_threshold_A;
}

bool Controller::off_safe() const {
    // Current can only flow to the output through the module's closed contactor - but an "open" read before
    // the last close command (which may already have closed it) proves nothing.
    const bool open_read_after_close =
        ds_status == 0 && (!contactor_close_written_at_poll || poll_count > *contactor_close_written_at_poll);
    return status_valid && (open_read_after_close || low_current_after_power_off());
}

bool Controller::wait_until_off_safe(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx);
    // Without communication nothing can be confirmed: return at once (CommunicationFault is raised).
    return cv.wait_for(lock, timeout, [this] { return off_safe() || comm_fault; });
}

void Controller::wait_for_target_change(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mtx);
    const uint64_t seen = target_generation;
    cv.wait_for(lock, timeout, [&] { return target_generation != seen; });
}

int Controller::failed_polls_in_row() const {
    std::lock_guard<std::mutex> lock(mtx);
    return failed_polls;
}

bool Controller::step(SdoClient& sdo, Clock::time_point now) {
    bool caps;
    {
        std::lock_guard<std::mutex> lock(mtx);
        caps = caps_known;
    }
    const bool ok = (caps || read_capabilities(sdo)) && poll(sdo);
    if (!ok) {
        communication_lost("no answer from the CHARX module");
        const int n = failed_polls_in_row();
        if (n == config.comm_failure_limit || n % 20 == 0) {
            sdo.start_remote_node();
        }
        return false;
    }

    int failed_before;
    bool want_export, exporting;
    {
        std::lock_guard<std::mutex> lock(mtx);
        const bool dc_side_off = bit(flags1, flag::DcOutputSideOff) && !bit(flags1, flag::PfcOff);
        if (!dc_side_off) {
            dc_side_off_since.reset();
        } else if (!dc_side_off_since) {
            dc_side_off_since = now;
        }
        dc_side_off_tolerated = dc_side_off && now - *dc_side_off_since <= config.dc_stage_switch_tolerance;
        failed_before = failed_polls;
        want_export = target_export;
        exporting = stage_alive() && contactor_closed();
    }
    if (failed_before > 0) {
        // The module may have rebooted in the meantime and wait pre-operational, without its state: start it,
        // and if it should export but does not, run the whole switch-on sequence in this very cycle.
        sdo.start_remote_node();
        if (want_export && !exporting) {
            sent_contactor = power_on_accepted = sent_current = false;
            deviation_since.reset();
            if (!on_requested_at) {
                on_requested_at = now; // a pending switch-on keeps its deadline across short outages
            }
            export_confirmed = false;
        }
    }

    // Interface rule: after a communication loss, the settings are sent again before the error is cleared.
    actuate(sdo, now);
    {
        std::lock_guard<std::mutex> lock(mtx);
        failed_polls = 0;
        comm_fault = false;
    }
    set_error(ErrorType::CommunicationFault, false, "");
    update_errors(now);
    update_mode();
    double v, i;
    {
        std::lock_guard<std::mutex> lock(mtx);
        v = v_meas;
        i = i_meas;
    }
    outputs.on_measurement(v, i);
    cv.notify_all();
    return true;
}

void Controller::report_unreachable(const std::string& reason) {
    communication_lost(reason);
}

void Controller::communication_lost(const std::string& reason) {
    int n;
    {
        std::lock_guard<std::mutex> lock(mtx);
        n = ++failed_polls;
        if (n >= config.comm_failure_limit) {
            status_valid = false;
            comm_fault = true;
        }
    }
    if (n >= config.comm_failure_limit) {
        set_error(ErrorType::CommunicationFault, true, reason);
        // Send the setpoints, or the Off, again once the module answers (see step()).
        applied_generation = ~0ull;
        sent_voltage = sent_current = false;
        sent_power_off = sent_open = false;
    }
    cv.notify_all();
}

bool Controller::read_capabilities(SdoClient& sdo) {
    const auto vmin = sdo.read(obj::v_min_avl);
    const auto vmax = sdo.read(obj::v_max_avl);
    const auto imax = sdo.read(obj::i_max_avl);
    if (!vmin.ok() || !vmax.ok() || !imax.ok()) {
        return false;
    }
    Capabilities caps;
    {
        std::lock_guard<std::mutex> lock(mtx);
        cap_v_min = std::max(config.min_export_voltage_V, vmin.value / 1000.0);
        cap_v_max = std::min(config.max_export_voltage_V, vmax.value / 1000.0);
        cap_i_max = std::max(0.0, std::min(config.max_export_current_A, imax.value / 1000.0));
        if (cap_v_min > cap_v_max) {
            EVLOG_warning << fmt::format("CHARX: voltage range of config and module do not overlap ({:.0f} > {:.0f} V)",
                                         cap_v_min, cap_v_max);
            cap_v_min = cap_v_max;
        }
        caps_known = true;
        caps.min_export_voltage_V = cap_v_min;
        caps.max_export_voltage_V = cap_v_max;
        caps.max_export_current_A = cap_i_max;
        caps.max_export_power_W = config.max_export_power_W;
        // per Phoenix Contact: +-1 % of max current, at least 0.3 A; ripple 1.3 % of max current
        caps.current_regulation_tolerance_A = std::max(0.3, 0.01 * cap_i_max);
        caps.peak_current_ripple_A = 0.013 * cap_i_max;
    }
    EVLOG_info << fmt::format("CHARX capabilities: {:.0f}-{:.0f} V, 0-{:.0f} A, {:.0f} W", caps.min_export_voltage_V,
                              caps.max_export_voltage_V, caps.max_export_current_A, caps.max_export_power_W);
    outputs.on_capabilities(caps);
    return true;
}

bool Controller::poll(SdoClient& sdo) {
    const auto f1 = sdo.read(obj::flags1);
    const auto ds = sdo.read(obj::ds_output_status);
    const auto v = sdo.read(obj::v_meas);
    const auto i = sdo.read(obj::i_meas);
    if (!f1.ok() || !ds.ok() || !v.ok() || !i.ok()) {
        return false;
    }
    std::optional<int32_t> ce;
    if (poll_cycle++ % static_cast<unsigned>(std::max(1, config.contactor_error_every)) == 0) {
        const auto r = sdo.read(obj::contactor_error);
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
    poll_count++;
    return true;
}

SdoResult Controller::write(SdoClient& sdo, Obj o, int32_t value, const char* what) {
    const auto r = sdo.write(o, value);
    if (!r.ok() && !(r.status == SdoResult::Status::Abort && r.abort_code == ABORT_DEVICE_STATE)) {
        EVLOG_warning << fmt::format("CHARX: write {} ({:04X}:{:02X} = {}) failed: {}", what, o.index, o.sub, value,
                                     r.describe());
    }
    return r;
}

void Controller::actuate(SdoClient& sdo, Clock::time_point now) {
    bool want_export;
    double v, i;
    uint64_t gen;
    {
        std::lock_guard<std::mutex> lock(mtx);
        want_export = target_export;
        v = std::min(std::max(target_voltage, cap_v_min), cap_v_max);
        i = std::min(std::max(target_current, 0.0), cap_i_max);
        gen = target_generation;
    }

    if (gen != applied_generation) {
        if (want_export && !applied_export) {
            EVLOG_info << fmt::format("CHARX: switch on requested: {:.1f} V / {:.1f} A", v, i);
            {
                std::lock_guard<std::mutex> lock(mtx);
                power_off_written_at_poll.reset();
            }
            sent_contactor = power_on_accepted = sent_current = false;
            on_requested_at = now;
            off_requested_at.reset();
            deviation_since.reset();
            export_confirmed = false;
        } else if (!want_export && applied_export) {
            EVLOG_info << "CHARX: switch off requested";
            sent_power_off = sent_open = false;
            off_requested_at = now;
            on_requested_at.reset();
            deviation_since.reset();
            export_confirmed = false;
        }
        sent_voltage = false; // setpoints may have changed
        sent_current = false;
        applied_export = want_export;
        applied_generation = gen;
    }

    if (want_export) {
        actuate_export(sdo, now, v, i);
    } else {
        actuate_off(sdo, now);
    }
}

void Controller::actuate_export(SdoClient& sdo, Clock::time_point now, double v, double i) {
    if (!sent_voltage && write(sdo, obj::v_set, milli(v), "voltage setpoint").ok()) {
        sent_voltage = true;
    }
    if (!sent_contactor) {
        // current stays 0 until PowerOn is accepted (as the PS_CHARX_POWER library does it)
        {
            std::lock_guard<std::mutex> lock(mtx);
            contactor_close_written_at_poll = poll_count;
        }
        if (write(sdo, obj::i_set, 0, "current 0").ok() &&
            write(sdo, obj::ds_output, config.contactor, "close contactor").ok()) {
            sent_contactor = true;
            last_resend = now;
        }
    }
    if (sent_contactor && !power_on_accepted) {
        // refused with ABORT_DEVICE_STATE until the contactor reports closed (~0.8 s) - retry every cycle
        if (write(sdo, obj::readiness, 1, "PowerOn").ok()) {
            power_on_accepted = true;
            EVLOG_info << "CHARX: PowerOn accepted";
        }
    }
    if (power_on_accepted && !sent_current && write(sdo, obj::i_set, milli(i), "current setpoint").ok()) {
        sent_current = true;
    }

    // The module does not follow (contactor open or PFC off) after it accepted PowerOn, or after it already
    // exported. DcOutputSideOff alone is not a reason: the module drops it ~1.3 s while switching its DC stages.
    // Only a deviation that lasts resend_after counts, so a single odd poll never interrupts the current.
    bool following;
    {
        std::lock_guard<std::mutex> lock(mtx);
        following = !status_valid || (contactor_closed() && stage_alive());
    }
    if (following) {
        deviation_since.reset();
    } else if (power_on_accepted || export_confirmed) {
        if (!deviation_since) {
            deviation_since = now;
        }
        if (power_on_accepted && now - *deviation_since > config.resend_after &&
            now - last_resend > config.resend_after) {
            EVLOG_warning << "CHARX: module does not follow the switch-on, sending the commands again";
            sent_voltage = sent_contactor = power_on_accepted = sent_current = false;
            last_resend = now;
        }
    }
}

void Controller::actuate_off(SdoClient& sdo, Clock::time_point now) {
    // Current to 0 and the power stage off first; the module's contactor opens once a reading polled after
    // PowerOff shows low current - or after resend_after at the latest, so a module that keeps driving current
    // against PowerOff is still cut off.
    if (!sent_power_off) {
        const bool a = write(sdo, obj::i_set, 0, "current 0").ok();
        const bool b = write(sdo, obj::readiness, 0, "PowerOff").ok();
        sent_power_off = a && b;
        sent_open = false;
        power_off_sent_at = now;
        last_resend = now;
        if (sent_power_off) {
            std::lock_guard<std::mutex> lock(mtx);
            power_off_written_at_poll = poll_count;
        }
    }
    if (sent_power_off && !sent_open) {
        bool may_open;
        {
            std::lock_guard<std::mutex> lock(mtx);
            may_open = low_current_after_power_off();
        }
        if (may_open || now - power_off_sent_at > config.resend_after) {
            sent_open = write(sdo, obj::ds_output, 0, "open contactor").ok();
        }
    }

    bool off;
    {
        std::lock_guard<std::mutex> lock(mtx);
        off = !status_valid || off_confirmed();
    }
    if (off) {
        deviation_since.reset();
    } else if (sent_power_off && sent_open) {
        if (!deviation_since) {
            deviation_since = now;
        }
        if (now - *deviation_since > config.resend_after && now - last_resend > config.resend_after) {
            EVLOG_warning << "CHARX: module is not off, sending PowerOff and contactor open again";
            sent_power_off = sent_open = false;
        }
    }
}

void Controller::update_errors(Clock::time_point now) {
    uint32_t f;
    int32_t ce;
    bool on_confirmed, is_off, want_export;
    {
        std::lock_guard<std::mutex> lock(mtx);
        f = flags1;
        ce = contactor_error;
        on_confirmed = stage_running() && contactor_closed();
        is_off = off_confirmed();
        want_export = target_export;
    }
    if (f != logged_flags) {
        EVLOG_info << fmt::format("CHARX: module flags1 0x{:08X} [{}]", f, flags1_to_string(f));
        logged_flags = f;
    }
    const std::string flags = flags1_to_string(f);
    using namespace flag;
    set_error(ErrorType::HardwareFault,
              bit(f, InternalFailure) || bit(f, ConverterError) || bit(f, ShortCircuit) || bit(f, EmergencyStop) ||
                  bit(f, DischargeProblem) || bit(f, FanFault) || bit(f, ModuleIdRepetition) || ce != 0,
              fmt::format("module flags [{}], contactor error {}", flags, ce));
    set_error(ErrorType::OverTemperature, bit(f, Otp), "module over temperature");
    set_error(ErrorType::UnderTemperature, bit(f, Utp), "module under temperature");
    set_error(ErrorType::UnderVoltageAC, bit(f, UvpInput) || bit(f, AcPhaseLoss), fmt::format("AC input [{}]", flags));
    set_error(ErrorType::OverVoltageAC, bit(f, OvpInput), "AC input over voltage");
    set_error(ErrorType::OverVoltageDC, bit(f, OvpOutput), "DC output over voltage");
    set_error(ErrorType::OverCurrentDC, bit(f, Ocp) || bit(f, OverPower), fmt::format("DC output [{}]", flags));
    set_error(ErrorType::OverCurrentAC, bit(f, AcOverload), "AC overload");

    // Validity: a state the module does not reach or leaves on its own is a fault, never a silent state.
    if (want_export && on_confirmed) {
        on_requested_at.reset();
    }
    if (!want_export && is_off) {
        off_requested_at.reset();
    }
    const bool on_late =
        want_export && on_requested_at && !on_confirmed && now - *on_requested_at > config.power_on_timeout;
    const bool dropped =
        want_export && export_confirmed && deviation_since && now - *deviation_since > config.resend_after;
    const bool off_late =
        !want_export && off_requested_at && !is_off && now - *off_requested_at > config.power_off_timeout;
    std::string message;
    if (on_late) {
        message = fmt::format("switch-on not confirmed within {} ms", config.power_on_timeout.count());
    } else if (dropped) {
        message = fmt::format("module left Export on its own (contactor open or power stage off) [{}]", flags);
    } else if (off_late) {
        message = fmt::format("switch-off not confirmed within {} ms", config.power_off_timeout.count());
    }
    set_error(ErrorType::VendorError, on_late || dropped || off_late, message);
}

void Controller::update_mode() {
    bool exporting, want_export;
    {
        std::lock_guard<std::mutex> lock(mtx);
        want_export = target_export;
        // Entering Export needs the stage fully up; staying in it tolerates DcOutputSideOff, which blinks while
        // the module switches its DC stages.
        const bool was_exporting = reported_mode.value_or(false);
        exporting = contactor_closed() && (was_exporting ? stage_alive() : stage_running());
    }
    if (exporting && want_export) {
        export_confirmed = true;
    }
    if (!reported_mode || *reported_mode != exporting) {
        EVLOG_info << "CHARX: module reports " << (exporting ? "Export" : "Off");
        outputs.on_mode(exporting);
        reported_mode = exporting;
    }
}

void Controller::set_error(ErrorType type, bool active, const std::string& message) {
    bool& state = error_active[static_cast<int>(type)];
    if (state == active) {
        return;
    }
    state = active;
    if (active) {
        EVLOG_warning << "CHARX: raising " << error_name(type) << ": " << message;
    } else {
        EVLOG_info << "CHARX: clearing " << error_name(type);
    }
    outputs.on_error(type, active, message);
}

} // namespace charx
