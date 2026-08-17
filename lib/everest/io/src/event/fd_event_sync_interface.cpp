// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_sync_interface.hpp>

#include <everest/io/event/fd_event_handler.hpp>

namespace everest::lib::io::event {

fd_event_sync_interface::~fd_event_sync_interface() {
    unregister_recorded_events();
}

bool fd_event_sync_interface::register_events(fd_event_handler& handler) {
    if (m_record.active()) {
        return false;
    }
    auto const fd = get_poll_fd();
    auto const registered = handler.register_event_handler(
        fd, [this](fd_event_handler::event_list const&) { sync(); }, poll_events::read);
    if (registered) {
        m_record.record(handler.liveness(), fd);
    }
    return registered;
}

bool fd_event_sync_interface::unregister_events(fd_event_handler& handler) {
    return m_record.drop_if(handler.liveness());
}

bool fd_event_sync_interface::unregister_recorded_events() {
    return m_record.drop();
}

} // namespace everest::lib::io::event
