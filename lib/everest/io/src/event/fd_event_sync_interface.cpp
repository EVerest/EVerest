// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_sync_interface.hpp>

#include <everest/io/event/fd_event_handler.hpp>

namespace everest::lib::io::event {

fd_event_sync_interface::~fd_event_sync_interface() {
    unregister_recorded_events();
}

bool fd_event_sync_interface::register_events(fd_event_handler& handler) {
    if (m_registered_handler != nullptr) {
        return false;
    }
    auto const fd = get_poll_fd();
    auto const registered = handler.register_event_handler(
        fd, [this](fd_event_handler::event_list const&) { sync(); }, poll_events::read);
    if (registered) {
        m_registered_handler = &handler;
        m_registered_fd = fd;
    }
    return registered;
}

bool fd_event_sync_interface::unregister_events(fd_event_handler& handler) {
    if (m_registered_handler != &handler) {
        return false;
    }
    return unregister_recorded_events();
}

bool fd_event_sync_interface::unregister_recorded_events() {
    if (m_registered_handler == nullptr) {
        return false;
    }
    auto* const handler = m_registered_handler;
    auto const fd = m_registered_fd;
    m_registered_handler = nullptr;
    m_registered_fd = -1;
    return handler->unregister_event_handler(fd);
}

} // namespace everest::lib::io::event
