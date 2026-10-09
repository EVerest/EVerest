// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#ifndef CHARX_POWER_DS_CHARX_CONTROLLER_HPP
#define CHARX_POWER_DS_CHARX_CONTROLLER_HPP

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "charx_canopen.hpp"

namespace charx {

enum class ErrorType {
    CommunicationFault,
    HardwareFault,
    OverTemperature,
    UnderTemperature,
    UnderVoltageAC,
    OverVoltageAC,
    OverVoltageDC,
    OverCurrentDC,
    OverCurrentAC,
    VendorError,
};

struct Capabilities {
    double min_export_voltage_V{0};
    double max_export_voltage_V{0};
    double max_export_current_A{0};
    double max_export_power_W{0};
    double current_regulation_tolerance_A{0};
    double peak_current_ripple_A{0};
};

// Where the controller reports to. Called from the thread that runs Controller::step() only.
class ControllerOutputs {
public:
    virtual ~ControllerOutputs() = default;
    virtual void on_capabilities(const Capabilities& caps) = 0;
    // true = Export (power stage on and contactor closed, both confirmed by the module), false = Off
    virtual void on_mode(bool exporting) = 0;
    virtual void on_measurement(double voltage_V, double current_A) = 0;
    // Called on every change of an error's state, with the reason when it becomes active.
    virtual void on_error(ErrorType type, bool active, const std::string& message) = 0;
};

struct ControllerConfig {
    int contactor{1}; // value for ds_output: 1 = contactor A, 2 = contactor B
    double min_export_voltage_V{50};
    double max_export_voltage_V{1000};
    double max_export_current_A{125};
    double max_export_power_W{40000};
    std::chrono::milliseconds power_on_timeout{20000};
    std::chrono::milliseconds power_off_timeout{8000};
    double off_current_threshold_A{2};
    // Commands the module does not follow are sent again after this time.
    std::chrono::milliseconds resend_after{3000};
    // Failed polls in a row before CommunicationFault is raised.
    int comm_failure_limit{3};
    // The contactor error object is read on every n-th poll only.
    int contactor_error_every{8};
    // How long DcOutputSideOff alone is taken for the module switching its DC stages (it drops the flag for
    // ~1.3 s then) rather than for a DC side that is off.
    std::chrono::milliseconds dc_stage_switch_tolerance{3000};
};

// The logic of the driver, independent of EVerest and of the bus: switch sequence, validity checks and error
// mapping for one CHARX power 40 kW DS module.
//
// One thread calls step() periodically; set_mode(), set_export_setpoint() and the wait functions may be called
// from any other thread.
class Controller {
public:
    using Clock = std::chrono::steady_clock;

    Controller(const ControllerConfig& config, ControllerOutputs& outputs);

    // targets, from EvseManager
    void set_mode(bool exporting);
    void set_export_setpoint(double voltage_V, double current_A);

    // One cycle: read the capabilities (until done once), poll the module, then send what the targets require,
    // update the errors and publish mode and measurement. Returns false if the module did not answer.
    bool step(SdoClient& sdo, Clock::time_point now);

    // The CAN interface could not be opened: counts as a failed poll.
    void report_unreachable(const std::string& reason);

    // Blocks until it is safe to open the charger relays (module contactor open or current below the threshold),
    // communication with the module is lost, or the timeout expires. Returns true unless the timeout expired.
    bool wait_until_off_safe(std::chrono::milliseconds timeout);
    // Blocks for up to timeout; returns early when set_mode() or set_export_setpoint() changed a target.
    void wait_for_target_change(std::chrono::milliseconds timeout);

    int failed_polls_in_row() const;

private:
    bool read_capabilities(SdoClient& sdo);
    bool poll(SdoClient& sdo);
    void actuate(SdoClient& sdo, Clock::time_point now);
    void actuate_export(SdoClient& sdo, Clock::time_point now, double v, double i);
    void actuate_off(SdoClient& sdo, Clock::time_point now);
    void update_errors(Clock::time_point now);
    void update_mode();
    void communication_lost(const std::string& reason);
    void set_error(ErrorType type, bool active, const std::string& message);
    SdoResult write(SdoClient& sdo, Obj o, int32_t value, const char* what);

    // under mtx
    bool stage_running() const; // power stage fully up: neither PfcOff nor DcOutputSideOff
    bool stage_alive() const;   // power stage not shut down: no PfcOff (DcOutputSideOff blinks while DC stages switch)
    bool contactor_closed() const;
    bool off_confirmed() const;
    bool off_safe() const;
    bool low_current_after_power_off() const;

    const ControllerConfig config;
    ControllerOutputs& outputs;

    mutable std::mutex mtx;
    std::condition_variable cv;
    // targets (mtx)
    bool target_export{false};
    double target_voltage{0};
    double target_current{0};
    uint64_t target_generation{0};
    // module status from the last poll (mtx)
    bool status_valid{false};
    uint32_t flags1{0};
    int32_t ds_status{0};
    int32_t contactor_error{0};
    double v_meas{0};
    double i_meas{0};
    // capabilities (mtx)
    bool caps_known{false};
    double cap_v_min{0};
    double cap_v_max{0};
    double cap_i_max{0};
    // communication (mtx)
    int failed_polls{0};
    bool comm_fault{false};
    // completed polls, and the count when PowerOff was last written (mtx): a reading is "after PowerOff" only
    // if it was polled after the write
    uint64_t poll_count{0};
    std::optional<uint64_t> power_off_written_at_poll;
    std::optional<uint64_t> contactor_close_written_at_poll;
    bool dc_side_off_tolerated{false};

    // step() thread only
    uint64_t applied_generation{~0ull};
    bool applied_export{false};
    // switch-on sequence
    bool sent_voltage{false};
    bool sent_contactor{false};
    bool power_on_accepted{false};
    bool sent_current{false};
    // switch-off sequence
    bool sent_power_off{false};
    bool sent_open{false};
    Clock::time_point power_off_sent_at{};
    // validity
    std::optional<Clock::time_point> on_requested_at;
    std::optional<Clock::time_point> off_requested_at;
    std::optional<Clock::time_point> deviation_since; // module not in the state the last commands asked for
    std::optional<Clock::time_point> dc_side_off_since;
    bool export_confirmed{false};
    Clock::time_point last_resend{};
    unsigned poll_cycle{0};
    std::optional<bool> reported_mode;
    uint32_t logged_flags{0xFFFFFFFF};
    bool error_active[static_cast<int>(ErrorType::VendorError) + 1]{};
};

} // namespace charx

#endif // CHARX_POWER_DS_CHARX_CONTROLLER_HPP
