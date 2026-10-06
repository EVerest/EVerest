// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include "CpStateFrameEmitter.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <everest/logging.hpp>

namespace module {

namespace {

constexpr std::uint16_t ethertype_homeplug_av{0x88E1};
constexpr std::uint16_t mmtype_stp_cpstate_ind{0xA22E};
constexpr std::array<std::uint8_t, 3> oui_st_iotecha{0x00, 0x80, 0xE1};
// Local address of Qualcomm PLC modems, the destination the dSPACE reference captures use.
constexpr MacAddress destination_mac{0x00, 0xB0, 0x52, 0x00, 0x00, 0x01};
constexpr std::size_t min_frame_size{60};
constexpr std::uint16_t pwm_frequency_hz{1000};

std::uint8_t cp_state_code(RawCPState cp_state) {
    switch (cp_state) {
    case RawCPState::A:
        return 0x01;
    case RawCPState::B:
        return 0x03;
    case RawCPState::C:
        return 0x05;
    case RawCPState::D:
        return 0x07;
    case RawCPState::E:
        return 0x09;
    case RawCPState::F:
        return 0x0A;
    case RawCPState::Disabled:
        break;
    }
    return 0x00;
}

std::uint16_t nominal_cp_voltage_mv(RawCPState cp_state) {
    switch (cp_state) {
    case RawCPState::A:
        return 12000;
    case RawCPState::B:
        return 9000;
    case RawCPState::C:
        return 6000;
    case RawCPState::D:
        return 3000;
    case RawCPState::E:
    case RawCPState::F:
    case RawCPState::Disabled:
        break;
    }
    return 0;
}

void append_le16(std::vector<std::uint8_t>& frame, std::uint16_t value) {
    frame.push_back(static_cast<std::uint8_t>(value & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(value >> 8));
}

} // namespace

std::vector<std::uint8_t> build_cp_state_frame(const MacAddress& source, RawCPState cp_state,
                                               double duty_cycle_percent) {
    const bool pwm_active = duty_cycle_percent > 0.0 and duty_cycle_percent < 100.0;
    const auto duty_cycle = static_cast<std::uint8_t>(std::lround(std::clamp(duty_cycle_percent, 0.0, 100.0)));

    std::vector<std::uint8_t> frame;
    frame.reserve(min_frame_size);
    frame.insert(frame.end(), destination_mac.begin(), destination_mac.end());
    frame.insert(frame.end(), source.begin(), source.end());
    frame.push_back(static_cast<std::uint8_t>(ethertype_homeplug_av >> 8));
    frame.push_back(static_cast<std::uint8_t>(ethertype_homeplug_av & 0xFF));
    frame.push_back(0x01); // MMV: HomePlug AV 1.1
    append_le16(frame, mmtype_stp_cpstate_ind);
    append_le16(frame, 0x0000); // FMI: unfragmented
    frame.insert(frame.end(), oui_st_iotecha.begin(), oui_st_iotecha.end());
    frame.insert(frame.end(), {0x00, 0x00, 0x00, 0x00, 0x00}); // MME version, reserved, message version
    frame.push_back(cp_state_code(cp_state));
    frame.push_back(duty_cycle);
    append_le16(frame, pwm_active ? pwm_frequency_hz : 0);
    append_le16(frame, nominal_cp_voltage_mv(cp_state));
    frame.push_back(0x00); // ADC channel bitmask: none
    frame.resize(min_frame_size, 0x00);
    return frame;
}

CpStateFrameEmitter::CpStateFrameEmitter(const std::string& device) : m_device(device) {
    m_if_index = static_cast<int>(if_nametoindex(device.c_str()));
    if (m_if_index == 0) {
        throw std::runtime_error("Unknown network interface '" + device + "'");
    }

    m_socket_fd = ::socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, 0);
    if (m_socket_fd < 0) {
        throw std::runtime_error("Could not open raw socket for '" + device + "': " + std::strerror(errno) +
                                 " (requires CAP_NET_RAW)");
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, device.c_str(), IFNAMSIZ - 1);
    if (::ioctl(m_socket_fd, SIOCGIFHWADDR, &ifr) < 0) {
        const std::string what = std::strerror(errno);
        ::close(m_socket_fd);
        m_socket_fd = -1;
        throw std::runtime_error("Could not read MAC address of '" + device + "': " + what);
    }
    std::memcpy(m_source_mac.data(), ifr.ifr_hwaddr.sa_data, m_source_mac.size());
}

CpStateFrameEmitter::~CpStateFrameEmitter() {
    if (m_socket_fd >= 0) {
        ::close(m_socket_fd);
    }
}

void CpStateFrameEmitter::cp_state_changed(RawCPState cp_state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cp_state = cp_state;
    send_if_changed();
}

void CpStateFrameEmitter::pwm_duty_cycle_changed(double duty_cycle_percent) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_duty_cycle_percent = duty_cycle_percent;
    send_if_changed();
}

void CpStateFrameEmitter::send_if_changed() {
    auto frame = build_cp_state_frame(m_source_mac, m_cp_state, m_duty_cycle_percent);
    if (frame == m_last_frame) {
        return;
    }

    struct sockaddr_ll address;
    std::memset(&address, 0, sizeof(address));
    address.sll_family = AF_PACKET;
    address.sll_ifindex = m_if_index;
    address.sll_protocol = htons(ethertype_homeplug_av);
    address.sll_halen = destination_mac.size();
    std::memcpy(address.sll_addr, destination_mac.data(), destination_mac.size());

    const auto sent = ::sendto(m_socket_fd, frame.data(), frame.size(), 0,
                               reinterpret_cast<const struct sockaddr*>(&address), sizeof(address));
    if (sent < 0) {
        if (not m_send_error_logged) {
            EVLOG_warning << "Could not send CP state frame on '" << m_device << "': " << std::strerror(errno);
            m_send_error_logged = true;
        }
        return;
    }
    m_last_frame = std::move(frame);
}

} // namespace module
