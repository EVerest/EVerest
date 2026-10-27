// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#ifndef INFY_CAN_DEVICE_HPP
#define INFY_CAN_DEVICE_HPP

#include "CanPackets.hpp"
#include <atomic>
#include <can_device/CanDevice.hpp>
#include <deque>
#include <functional>
#include <linux/can.h>
#include <map>
#include <mutex>
#include <optional>
#include <sigslot/signal.hpp>
#include <vector>

class InfyCanDevice : public can_device::CanDevice {
public:
    InfyCanDevice();
    ~InfyCanDevice();

    enum class OutputMode {
        Parallel = 0xA0,
        Series = 0xA1,
        Automatic = 0xA2
    };

    // Commands

    bool switch_on_off(bool on);
    bool set_walkin_enabled(bool on);
    bool set_inverter_mode(bool inverter);
    bool set_voltage_current(float voltage, float current);
    bool set_output_mode(OutputMode mode);
    bool adjust_power_factor(float pf);
    bool set_generic_setting(uint8_t byte0, uint8_t byte1, uint32_t value);
    // Queued behind the cyclic traffic, one frame per tick.
    bool request_rx(const uint8_t destination_address, const std::vector<uint8_t>& payload);

    // Data out
    sigslot::signal<float, float, float> signalacdcTemperatures;
    sigslot::signal<float, float> signalVoltageCurrent;
    sigslot::signal<can_packet_acdc::PowerModuleStatus, can_packet_acdc::InverterStatus> signalModuleStatus;
    // True once the module answers and mode, setpoints and settings have been sent to it again, false when the CAN
    // interface is lost or the module stops answering.
    sigslot::signal<bool> signal_connection;

    struct Telemetry {
        float ac_ab_line_voltage{0.};
        float ac_bc_line_voltage{0.};
        float ac_ca_line_voltage{0.};
        float ambient_temperature{0.};
        float dc_max_output_voltage{0.};
        float dc_min_output_voltage{0.};
        float dc_max_output_current{0.};
        float dc_rated_output_power{0.};
        float ac_phase_a_current{0.};
        float ac_phase_b_current{0.};
        float ac_phase_c_current{0.};
        float ac_phase_a_voltage{0.};
        float ac_phase_b_voltage{0.};
        float ac_phase_c_voltage{0.};
        float ac_frequency{0.};
        float ac_phase_a_active_power{0.};
        float ac_phase_b_active_power{0.};
        float ac_phase_c_active_power{0.};
        float ac_total_active_ower{0.};

        float ac_phase_a_reactive_power{0.};
        float ac_phase_b_reactive_power{0.};
        float ac_phase_c_reactive_power{0.};
        float ac_total_reactive_ower{0.};

        float ac_phase_a_apparent_power{0.};
        float ac_phase_b_apparent_power{0.};
        float ac_phase_c_apparent_power{0.};
        float ac_total_apparent_ower{0.};

        float dc_high_side_voltage{0.};
        float dc_high_side_current{0.};

        can_packet_acdc::SystemDCVoltage voltage;
        can_packet_acdc::SystemDCCurrent current;
        can_packet_acdc::PowerModuleStatus status;
    } telemetry;

    friend std::ostream& operator<<(std::ostream& out, const Telemetry& self);

protected:
    void rx_handler(uint32_t can_id, const std::vector<uint8_t>& payload) override;
    void tick_handler() override;
    void link_handler(bool up) override;

private:
    struct Frame {
        uint32_t can_id;
        std::vector<uint8_t> payload;
    };

    static Frame write_frame(uint8_t destination_address, const std::vector<uint8_t>& payload);
    static Frame read_frame(uint8_t destination_address, const std::vector<uint8_t>& payload);
    bool send(const Frame& frame);
    void queue(Frame frame);
    std::optional<Frame> pop_queued();
    void requeue_front(Frame frame);
    bool setpoint_valid() const;
    void send_cycle_step();
    enum class ResyncStep {
        Sent,
        Skipped,
        Dropped
    };
    static ResyncStep as_resync_step(bool sent);
    void start_resync();
    bool send_resync_step();
    void abort_resync();
    void report_connection(bool connected);

    struct CycleStep {
        enum Kind {
            Read,
            Write
        } kind;
        std::function<bool()> send;
    };
    std::vector<CycleStep> tx_cycle;
    std::size_t tx_cycle_step{0};

    std::atomic<float> setpoint_voltage{0}, setpoint_current{0};
    std::atomic_bool on{false};
    std::atomic_bool walkin_enable{false};
    std::atomic_bool inverter_mode{false};

    // Settings the module does not send cyclically, sent again after every reconnect.
    std::mutex settings_mutex;
    std::optional<float> power_factor;
    std::map<std::pair<uint8_t, uint8_t>, uint32_t> generic_settings;

    // Frames sent one per tick, interleaved with the cycle, while the module is reported connected.
    std::mutex queue_mutex;
    std::deque<Frame> queued;

    // CAN thread only
    bool link{false};
    // Sent one per tick after the module answers, before anything else. A step that drops its frame is retried
    // until the deadline runs out.
    std::deque<std::function<ResyncStep()>> resync_steps;
    unsigned resync_ticks_left{0};
    // All resync steps sent; the next reply to a poll reports the module connected.
    bool settings_restored{false};
    bool last_tick_was_cycle{false};
    unsigned ticks_since_rx{0};
    std::optional<bool> reported_connection;
};

#endif // INFY_CAN_DEVICE_HPP
