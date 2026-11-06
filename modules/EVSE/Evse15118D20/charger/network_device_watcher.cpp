// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include "network_device_watcher.hpp"

#include <system_error>
#include <utility>

#include <everest/io/event/fd_event_handler.hpp>
#include <everest/logging.hpp>

namespace module::charger {

NetworkDeviceWatcher::NetworkDeviceWatcher(std::string device, PresenceCallback on_presence) :
    m_watcher(std::move(device), /*watch_neighbors=*/false), m_on_presence(std::move(on_presence)) {
    using severity = everest::lib::io::netlink::device_watcher::diagnostic_severity;

    everest::lib::io::netlink::device_watcher::callbacks handlers;
    handlers.on_presence_change = [this](bool present) {
        EVLOG_info << "Network device " << m_watcher.device() << (present ? " appeared" : " disappeared");
        m_on_presence(present);
    };
    handlers.on_initial_state = [this]() {
        if (not m_watcher.device_present()) {
            EVLOG_warning << "Network device " << m_watcher.device() << " does not exist; waiting for it";
        }
    };
    handlers.on_diagnostic = [](severity level, const std::string& message) {
        if (level == severity::error) {
            EVLOG_error << message;
        } else {
            EVLOG_warning << message;
        }
    };
    handlers.on_fatal_error = [this](const std::string& reason) {
        EVLOG_error << "Supervision of network device " << m_watcher.device() << " stopped: " << reason;
        assume_present();
    };
    m_watcher.set_callbacks(std::move(handlers));
}

NetworkDeviceWatcher::~NetworkDeviceWatcher() {
    m_online.store(false);
    m_exit_event.notify();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool NetworkDeviceWatcher::start() {
    if (not m_watcher.open()) {
        EVLOG_error << "Could not open the rtnetlink socket to watch network device " << m_watcher.device() << ": "
                    << std::generic_category().message(m_watcher.error());
        return false;
    }
    m_thread = std::thread([this] { run(); });
    return true;
}

void NetworkDeviceWatcher::assume_present() {
    if (not m_watcher.device_present()) {
        EVLOG_warning << "Assuming network device " << m_watcher.device() << " is present";
        m_on_presence(true);
    }
}

void NetworkDeviceWatcher::run() {
    everest::lib::io::event::fd_event_handler handler;

    if (not handler.register_event_handler(&m_watcher) or not handler.register_event_handler(&m_exit_event, []() {})) {
        EVLOG_error << "Could not set up the event loop watching network device " << m_watcher.device();
        assume_present();
        return;
    }

    handler.run(m_online);

    (void)handler.unregister_event_handler(&m_watcher);
    (void)handler.unregister_event_handler(&m_exit_event);
}

} // namespace module::charger
