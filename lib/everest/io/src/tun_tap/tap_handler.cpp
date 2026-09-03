// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <everest/io/tun_tap/tap_handler.hpp>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <linux/if.h>
#include <linux/if_tun.h>

#include <everest/io/socket/socket.hpp>

namespace everest::lib::io::tun_tap {

namespace {

void set_ifr_name(struct ifreq& ifr, std::string const& dev_name) {
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev_name.c_str(),
            std::min(dev_name.size(), static_cast<std::string::size_type>(IFNAMSIZ - 1)));
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
}

} // namespace

bool tap_handler::open(std::string const& device, std::string const& ip, std::string const& netmask, int mtu,
                       bool carrier_on) {
    m_mtu = mtu;
    m_carrier_setup_error = 0;
    try {
        m_fd = socket::create_tap_device(device);
    } catch (...) {
        // The name is recorded only once the device exists, so a failed creation cannot leave
        // \ref carrier querying a device this handler does not own - possibly one belonging to
        // another process, which is what makes the name available in the first place. On this path
        // the previously held fd, if any, survives untouched, and so does the name that goes with it.
        m_error = EPERM;
        return false;
    }
    m_device = device;
    // The kernel creates a TAP device with the carrier on, so the request is issued here rather than
    // left to the caller - and it is issued before the device is brought up, not after. Bringing a
    // device up emits an rtnetlink notification synchronously, and with the carrier still on that
    // notification announces IFF_RUNNING and IFF_LOWER_UP: a false carrier-up that no later ioctl can
    // retract. Dropping the carrier first means the device's very first announcement is truthful.
    if (not carrier_on and not set_carrier(false)) {
        m_carrier_setup_error = m_error;
    }
    if (not socket::configure_tap_device_properties(m_fd, device, ip, netmask, mtu)) {
        m_error = EPERM;
        m_fd.close();
        m_device.clear();
        return false;
    }
    // A successful open must report no error: fd_event_client reads the policy's error right after
    // one and fails the fresh connection on anything nonzero. A kernel without TUNSETCARRIER must
    // not fail the open either, so the carrier request's errno goes to carrier_setup_error() - and
    // must be cleared from here, since set_carrier above left it in m_error.
    m_error = 0;
    return true;
}

bool tap_handler::set_carrier(bool on) {
    int value = on ? 1 : 0;
    if (ioctl(m_fd, TUNSETCARRIER, &value) == -1) {
        m_error = errno;
        return false;
    }
    m_error = 0;
    return true;
}

int tap_handler::carrier_setup_error() const {
    return m_carrier_setup_error;
}

std::optional<bool> tap_handler::carrier() const {
    // Unlike the rest of the class this queries by name rather than through the fd, so the fd is
    // consulted as the ownership token: without it there is no device whose carrier this handler is
    // entitled to report, and a name alone could resolve to somebody else's device.
    if (not m_fd.is_fd()) {
        return std::nullopt;
    }
    event::unique_fd control_sock_fd;
    try {
        control_sock_fd = socket::open_control_socket();
    } catch (...) {
        return std::nullopt;
    }

    struct ifreq ifr;
    set_ifr_name(ifr, m_device);
    if (ioctl(control_sock_fd, SIOCGIFFLAGS, &ifr) == -1) {
        return std::nullopt;
    }
    return (ifr.ifr_flags & IFF_RUNNING) != 0;
}

bool tap_handler::tx(PayloadT const& data) {
    auto res = ::write(m_fd, data.data(), data.size());
    if (res != static_cast<ssize_t>(data.size())) {
        m_error = errno;
        return false;
    }
    m_error = 0;
    return true;
}

bool tap_handler::rx(PayloadT& data) {
    // A TAP fd (IFF_NO_PI) delivers whole Ethernet frames: up to MTU bytes of
    // payload plus the 14 byte Ethernet header (+4 for an optional 802.1Q VLAN
    // tag). The kernel silently truncates frames larger than the read buffer.
    constexpr int ethernet_frame_overhead = 18;
    data.resize(m_mtu + ethernet_frame_overhead);
    auto res = ::read(m_fd, data.data(), data.size());
    if (res < 0) {
        m_error = errno;
        return false;
    }

    data.resize(res);
    m_error = 0;
    return true;
}

int tap_handler::get_fd() const {
    return m_fd;
}

int tap_handler::get_error() const {
    return m_error;
}

} // namespace everest::lib::io::tun_tap
