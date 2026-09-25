// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "TelemetryExample.hpp"

#include <chrono>
#include <cmath>

namespace module {

void TelemetryExample::init() {
    invoke_init(*p_main);

    // telemetry may be published from init(): the telemetry socket exists before any module starts
    tel.firmware_version.set("1.4.2");
}

void TelemetryExample::ready() {
    invoke_ready(*p_main);

    m_publisher = std::thread([this] { run(); });
}

void TelemetryExample::shutdown() {
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_stop_cv.notify_all();
    if (m_publisher.joinable()) {
        m_publisher.join();
    }

    invoke_shutdown(*p_main);
}

void TelemetryExample::run() {
    const auto interval = std::chrono::milliseconds(config.publish_interval_ms);
    std::uint64_t round = 0;
    std::unique_lock lock(m_mutex);
    while (not m_stop) {
        lock.unlock();
        publish_round(round++);
        lock.lock();
        m_stop_cv.wait_for(lock, interval, [this] { return m_stop; });
    }
}

void TelemetryExample::publish_round(std::uint64_t round) {
    constexpr double pi = 3.14159265358979323846;
    const auto phase = static_cast<double>(round % 60) / 60.0 * 2.0 * pi;

    tel.temperature.set(35.0 + 10.0 * std::sin(phase));
    tel.supply_voltage_raw.set(3000 + static_cast<std::int64_t>(round % 100));
    tel.energy_delivered.increase(3.05);
    tel.firmware_state.set(round % 30 < 20 ? tel::types::FirmwareState::Measuring : tel::types::FirmwareState::Idle);
    tel.charge_mode.set(types::evse_manager::ChargeMode::AC);
    tel.relay_closed.set(round % 20 < 10);

    if (round % 10 == 0) {
        tel.plug_ins.increase();
        tel.cp_event.publish({types::board_support_common::Event::B});
    }
}

} // namespace module
