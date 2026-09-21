// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "IECStateMachine.hpp"

namespace module {

using MacAddress = std::array<std::uint8_t, 6>;

/// Ethernet frame carrying a HomePlug AV STP_CPSTATE.IND vendor MME (ST/IoTecha OUI 00:80:E1, MMTYPE 0xA22E), the
/// format Wireshark's homeplug-av dissector and the dsV2Gshark plugin decode. \p duty_cycle_percent 100 means no PWM.
std::vector<std::uint8_t> build_cp_state_frame(const MacAddress& source, RawCPState cp_state,
                                               double duty_cycle_percent);

/// Debugging aid: sends a CP state frame on a network interface whenever the measured CP state or the commanded PWM
/// duty cycle changes, so packet captures of the PLC interface show the CP state. Needs CAP_NET_RAW.
class CpStateFrameEmitter {
public:
    /// \throws std::runtime_error if the raw socket cannot be opened on \p device
    explicit CpStateFrameEmitter(const std::string& device);
    ~CpStateFrameEmitter();
    CpStateFrameEmitter(const CpStateFrameEmitter&) = delete;
    CpStateFrameEmitter& operator=(const CpStateFrameEmitter&) = delete;

    void cp_state_changed(RawCPState cp_state);
    void pwm_duty_cycle_changed(double duty_cycle_percent);

private:
    void send_if_changed();

    std::string m_device;
    int m_socket_fd{-1};
    int m_if_index{0};
    MacAddress m_source_mac{};
    std::mutex m_mutex;
    RawCPState m_cp_state{RawCPState::Disabled};
    double m_duty_cycle_percent{100.0};
    std::vector<std::uint8_t> m_last_frame;
    bool m_send_error_logged{false};
};

} // namespace module
