// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "everest/io/event/fd_event_sync_interface.hpp"
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/event/unique_fd.hpp>

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <vector>

#include <poll.h>
#include <sys/epoll.h>

namespace everest::lib::io::event {

namespace {
uint32_t poll_event_to_bitmask(poll_events e) {
    switch (e) {
    case poll_events::read:
        return EPOLLIN;
    case poll_events::priority:
        return EPOLLPRI;
    case poll_events::write:
        return EPOLLOUT;
    case poll_events::error:
        return EPOLLERR;
    case poll_events::hungup:
        return EPOLLHUP;
    }
    return 0;
}

std::set<poll_events> bitmask_to_poll_events(uint32_t bitmask) {
    std::set<poll_events> result;
    if (bitmask & EPOLLIN) {
        result.insert(poll_events::read);
    }
    if (bitmask & EPOLLPRI) {
        result.insert(poll_events::priority);
    }
    if (bitmask & EPOLLOUT) {
        result.insert(poll_events::write);
    }
    if (bitmask & EPOLLERR) {
        result.insert(poll_events::error);
    }
    if (bitmask & EPOLLHUP) {
        result.insert(poll_events::hungup);
    }
    return result;
}

uint32_t sum_events(std::set<poll_events> const& events) {
    uint32_t result = 0;
    for (auto e : events) {
        result = result | poll_event_to_bitmask(e);
    }
    return result;
}
} // namespace

std::set<poll_events> operator|(poll_events lhs, poll_events rhs) {
    return {lhs, rhs};
}

std::set<poll_events>& operator|(std::set<poll_events>& lhs, poll_events rhs) {
    lhs.insert(rhs);
    return lhs;
}

bool operator&(std::set<poll_events> const& lhs, poll_events rhs) {
    return lhs.count(rhs) == 1;
}

class EventHandlerMap {
public:
    EventHandlerMap() : m_epoll_fd(epoll_create1(0)) {
        if (not m_epoll_fd.is_fd()) {
            ::perror("epoll_create");
        }
    }

    bool add(int fd, fd_event_handler::event_handler_type handler, fd_event_handler::event_list const& events) {
        epoll_event event;
        event.events = sum_events(events);
        event.data.fd = fd;
        auto result = epoll_ctl(m_epoll_fd, EPOLL_CTL_ADD, fd, &event) == 0;
        if (result) {
            m_event_map[fd] = {std::move(handler), event};
            // Never shrinks, so a dispatch loop reading the array of the running poll cannot
            // see an entry destroyed or reset by a handler. epoll_wait accepts a maxevents
            // larger than the number of registered descriptors.
            m_pollfds.resize(std::max(m_pollfds.size(), m_event_map.size()));
        }
        return result;
    }

    bool remove(int fd) {
        auto epoll_removed = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr) == 0;
        auto handler_removed = m_event_map.count(fd) != 0;
        if (handler_removed) {
            m_event_map.erase(fd);
        }
        // Closing a descriptor drops it from the epoll set, so EPOLL_CTL_DEL then fails
        // EBADF on a live registration. Erasing the map entry alone is still a removal.
        return epoll_removed or handler_removed;
    }
    bool modify_remove(int fd, fd_event_handler::event_list const& events) {
        auto action = [](uint32_t current, fd_event_handler::event_list const& change) {
            auto raw_change = sum_events(change);
            auto result = current & (~raw_change);
            return result;
        };
        return modify(fd, events, action);
    }

    bool modify_add(int fd, fd_event_handler::event_list const& events) {
        auto action = [](uint32_t current, fd_event_handler::event_list const& change) {
            auto raw_change = sum_events(change);
            auto result = current | raw_change;
            return result;
        };
        return modify(fd, events, action);
    }

    bool modify_replace(int fd, fd_event_handler::event_list const& events) {
        auto action = [](uint32_t, fd_event_handler::event_list const& change) {
            auto raw_change = sum_events(change);
            auto result = raw_change;
            return result;
        };
        return modify(fd, events, action);
    }

    // By value. A handler is allowed to erase its own entry, which would destroy the
    // std::function the caller is still executing.
    auto get(int fd) const {
        return std::get<fd_event_handler::event_handler_type>(m_event_map.at(fd));
    }

    bool exists(int fd) const {
        return m_event_map.count(fd);
    }

    auto& get_pollfds() {
        return m_pollfds;
    }

    int get_epoll_fd() {
        return static_cast<int>(m_epoll_fd);
    }

private:
    using event_state = std::tuple<fd_event_handler::event_handler_type, epoll_event>;
    bool modify(int fd, fd_event_handler::event_list const& events,
                std::function<uint32_t(uint32_t, fd_event_handler::event_list const&)> const& event_action) {
        auto result = false;
        if (m_event_map.count(fd)) {
            auto& event = std::get<epoll_event>(m_event_map.at(fd));
            auto backup = event.events;
            event.events = event_action(backup, events);
            result = epoll_ctl(m_epoll_fd, EPOLL_CTL_MOD, fd, &event) == 0;
            if (not result) {
                event.events = backup;
            }
        }
        return result;
    }

    std::vector<epoll_event> m_pollfds;
    std::map<int, event_state> m_event_map;
    unique_fd m_epoll_fd;
};

fd_event_handler::~fd_event_handler() = default;

fd_event_handler::fd_event_handler() {
    m_handlers = std::make_unique<EventHandlerMap>();
    register_event_handler(&m_action_event, [](auto&&) {});
}

bool fd_event_handler::register_event_handler(int fd, event_handler_type const& handler, event_list const& events) {
    if (fd == -1 or not handler or m_handlers->exists(fd)) {
        return false;
    }
    return m_handlers->add(fd, handler, events);
}

bool fd_event_handler::register_event_handler(int fd, event_handler_type const& handler, poll_events event) {
    return register_event_handler(fd, handler, event_list{event});
}

bool fd_event_handler::register_event_handler(event_fd* fd, event_handler_type const& handler) {
    if (not fd) {
        return false;
    }
    auto raw = fd->get_raw_fd();
    return register_event_handler(
        raw,
        [handler, fd](event_list const& e) {
            fd->read();
            handler(e);
        },
        poll_events::read);
}

bool fd_event_handler::register_event_handler(event_fd* fd, event_handler_simple_type const& handler) {
    return register_event_handler(fd, [handler](event_list const&) { handler(); });
}

bool fd_event_handler::register_event_handler(timer_fd* fd, event_handler_type const& handler) {
    if (not fd) {
        return false;
    }
    auto raw = fd->get_raw_fd();
    return register_event_handler(
        raw,
        [handler, fd](event_list const& e) {
            fd->read();
            handler(e);
        },
        poll_events::read);
}

bool fd_event_handler::register_event_handler(timer_fd* fd, event_handler_simple_type const& handler) {
    return register_event_handler(fd, [handler](event_list const&) { handler(); });
}

bool fd_event_handler::register_event_handler(fd_event_sync_interface* obj) {
    if (not obj) {
        return false;
    }
    return obj->register_events(*this);
}

bool fd_event_handler::register_event_handler(fd_event_register_interface* obj) {
    if (not obj) {
        return false;
    }
    return obj->register_events(*this);
}

bool fd_event_handler::register_event_handler(fd_event_handler* obj) {
    if (not obj or obj == this) {
        return false;
    }
    auto raw = obj->get_poll_fd();
    return register_event_handler(
        raw,
        [obj](event_list const&) {
            obj->poll();
            obj->run_actions();
        },
        poll_events::read);
}

bool fd_event_handler::unregister_event_handler(fd_event_register_interface* obj) {
    if (not obj) {
        return false;
    }
    return obj->unregister_events(*this);
}

bool fd_event_handler::unregister_event_handler(fd_event_sync_interface* obj) {
    if (not obj) {
        return false;
    }
    return obj->unregister_events(*this);
}

bool fd_event_handler::unregister_event_handler(timer_fd* obj) {
    if (not obj) {
        return false;
    }
    return remove_event_handler(obj->get_raw_fd());
}

bool fd_event_handler::unregister_event_handler(event_fd* obj) {
    if (not obj) {
        return false;
    }
    return remove_event_handler(obj->get_raw_fd());
}

bool fd_event_handler::unregister_event_handler(int fd) {
    if (fd == -1) {
        return false;
    }
    return remove_event_handler(fd);
}

bool fd_event_handler::modify_event_handler(int fd, event_list const& events, event_modification change) {
    if (fd == -1) {
        return false;
    }
    switch (change) {
    case event_modification::add:
        return m_handlers->modify_add(fd, events);
    case event_modification::remove:
        return m_handlers->modify_remove(fd, events);
    case event_modification::replace:
        return m_handlers->modify_replace(fd, events);
    default:
        return false;
    }
}

bool fd_event_handler::modify_event_handler(int fd, poll_events event, event_modification change) {
    return modify_event_handler(fd, event_list{event}, change);
}

bool fd_event_handler::remove_event_handler(int fd) {
    if (fd == -1) {
        return false;
    }
    return m_handlers->remove(fd);
}

bool fd_event_handler::is_registered(int fd) const {
    if (fd == -1) {
        return false;
    }
    return m_handlers->exists(fd);
}

void fd_event_handler::poll() {
    poll_impl(-1);
}

bool fd_event_handler::poll_impl(int timeout_ms) {
    auto& pollfds = m_handlers->get_pollfds();
    auto status = ::epoll_wait(m_handlers->get_epoll_fd(), pollfds.data(), pollfds.size(), timeout_ms);

    if (status > 0) {
        for (int i = 0; i < status; ++i) {
            auto const item = pollfds[i];
            // A handler may unregister any descriptor, including one later in this batch.
            // Such an entry has no handler left to call and must not be dispatched.
            if (not m_handlers->exists(item.data.fd)) {
                continue;
            }
            auto const handler = m_handlers->get(item.data.fd);
            handler(bitmask_to_poll_events(item.events));
        }
        return true;
    }
    return false;
}

int fd_event_handler::get_poll_fd() {
    return m_handlers->get_epoll_fd();
}

void fd_event_handler::add_action(task&& item) {
    task_pool.push(std::forward<task>(item));
    m_action_event.notify();
}

void fd_event_handler::add_action(task const& item) {
    task_pool.push(std::move(item));
    m_action_event.notify();
}

void fd_event_handler::run_actions() {
    while (true) {
        auto item = task_pool.try_pop();
        if (item.has_value()) {
            try {
                item.value()();
            } catch (...) {
            }
        } else {
            break;
        }
    }
}

void fd_event_handler::run_once() {
    poll();
    run_actions();
}

void fd_event_handler::run(std::atomic_bool& online) {
    while (online.load()) {
        poll();
        run_actions();
    }
}

} // namespace everest::lib::io::event
