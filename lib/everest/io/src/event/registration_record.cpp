// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/registration_record.hpp>

#include <everest/io/event/fd_event_handler.hpp>

#include <utility>

namespace everest::lib::io::event {

bool registration_record::active() const {
    auto const live = m_handler.lock();
    return live and live->handler and live->handler->is_registered(m_fd);
}

void registration_record::record(std::shared_ptr<handler_liveness> handler, int fd) {
    m_handler = std::move(handler);
    m_fd = fd;
}

bool registration_record::drop() {
    auto const live = m_handler.lock();
    auto const fd = std::exchange(m_fd, -1);
    m_handler.reset();
    return live and live->handler and live->handler->remove_event_handler(fd);
}

bool registration_record::drop_if(std::shared_ptr<handler_liveness> const& handler) {
    if (m_handler.lock() != handler) {
        return false;
    }
    return drop();
}

} // namespace everest::lib::io::event
