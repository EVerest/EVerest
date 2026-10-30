// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include <everest/io/event/event_fd.hpp>
#include <everest/io/netlink/device_watcher.hpp>

namespace module::charger {

// Reports whether a network device exists, from rtnetlink, on a thread of its own.
class NetworkDeviceWatcher {
public:
    using PresenceCallback = std::function<void(bool present)>;

    // on_presence runs on the watcher thread, on every change of the device's presence, the initial state
    // included when the device exists. Should supervision fail, the device is reported present so the owner
    // keeps working as it would without supervision.
    NetworkDeviceWatcher(std::string device, PresenceCallback on_presence);
    // Stops the thread.
    ~NetworkDeviceWatcher();

    // Opens the netlink socket and starts the thread; false when the socket could not be opened.
    bool start();

private:
    void run();
    // Supervision is unavailable: an absent device is reported present, a present one is left alone.
    void assume_present();

    everest::lib::io::netlink::device_watcher m_watcher;
    PresenceCallback m_on_presence;
    // Wakes the thread out of poll() so it can observe m_online.
    everest::lib::io::event::event_fd m_exit_event;
    std::atomic_bool m_online{true};
    std::thread m_thread;
};

} // namespace module::charger
