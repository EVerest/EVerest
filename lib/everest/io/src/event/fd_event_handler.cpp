// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

#include "everest/io/event/fd_event_sync_interface.hpp"
#include <everest/io/event/event_fd.hpp>
#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/event/timer_fd.hpp>
#include <everest/io/event/unique_fd.hpp>

#include <array>
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

const fd_event_handler::event_list& bitmask_to_poll_events(uint32_t bitmask) {
    constexpr std::array<uint32_t, 5> epoll_bits = {EPOLLIN, EPOLLPRI, EPOLLOUT, EPOLLERR, EPOLLHUP};
    auto index = std::size_t{0};
    for (auto i = std::size_t{0}; i < epoll_bits.size(); ++i) {
        if (bitmask & epoll_bits[i]) {
            index |= (std::size_t{1} << i);
        }
    }

    static const auto cached_events = [] {
        constexpr std::array<poll_events, 5> cached_poll_event_values = {
            poll_events::read, poll_events::priority, poll_events::write, poll_events::error, poll_events::hungup};
        auto result = std::array<fd_event_handler::event_list, 32>{};
        for (auto mask = std::size_t{0}; mask < result.size(); ++mask) {
            for (auto i = std::size_t{0}; i < cached_poll_event_values.size(); ++i) {
                if (mask & (std::size_t{1} << i)) {
                    result[mask].insert(cached_poll_event_values[i]);
                }
            }
        }
        return result;
    }();

    return cached_events[index];
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
            m_pollfds.resize(m_pollfds.size() + 1);
        }
        return result;
    }

    bool remove(int fd) {
        auto epoll_result = epoll_ctl(m_epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
        auto handler_result = m_event_map.count(fd);
        if (handler_result) {
            m_event_map.erase(fd);
            m_pollfds.resize(m_pollfds.size() - 1);
        }
        return epoll_result or handler_result;
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

    const fd_event_handler::event_handler_type* find(int fd) const {
        auto it = m_event_map.find(fd);
        if (it == m_event_map.end()) {
            return nullptr;
        }
        return &std::get<fd_event_handler::event_handler_type>(it->second);
    }

    bool exists(int fd) const {
        return m_event_map.count(fd);
    }

    auto& get_pollfds() {
        return m_pollfds;
    }

    std::vector<epoll_event> snapshot_ready_events(int ready_count) const {
        return std::vector<epoll_event>(m_pollfds.begin(), m_pollfds.begin() + ready_count);
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
    m_handlers->add(fd, handler, events);
    return true;
}

bool fd_event_handler::register_event_handler(int fd, event_handler_type const& handler, poll_events event) {
    return register_event_handler(fd, handler, event_list{event});
}

bool fd_event_handler::register_event_handler(event_fd_base* fd, event_handler_type const& handler) {
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

bool fd_event_handler::register_event_handler(event_fd_base* fd, event_handler_simple_type const& handler) {
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
    auto raw = obj->get_poll_fd();
    return register_event_handler(
        raw, [obj](event_list const&) { obj->sync(); }, poll_events::read);
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
    return remove_event_handler(obj->get_poll_fd());
}

bool fd_event_handler::unregister_event_handler(timer_fd* obj) {
    if (not obj) {
        return false;
    }
    return remove_event_handler(obj->get_raw_fd());
}

bool fd_event_handler::unregister_event_handler(event_fd_base* obj) {
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

void fd_event_handler::poll() {
    poll_impl(-1);
}

bool fd_event_handler::poll_impl(int timeout_ms) {
    auto& pollfds = m_handlers->get_pollfds();
    auto status = ::epoll_wait(m_handlers->get_epoll_fd(), pollfds.data(), pollfds.size(), timeout_ms);
    m_last_ready_count = status > 0 ? static_cast<std::size_t>(status) : 0U;

    if (status > 0) {
        // Dispatch from a stable snapshot so callbacks may register, unregister, or modify handlers.
        const auto ready_events = m_handlers->snapshot_ready_events(status);
        for (const auto& item : ready_events) {
            if (auto handler = m_handlers->find(item.data.fd)) {
                auto handler_copy = *handler;
                handler_copy(bitmask_to_poll_events(item.events));
            }
        }
        return true;
    }
    return false;
}

int fd_event_handler::get_poll_fd() {
    return m_handlers->get_epoll_fd();
}

std::size_t fd_event_handler::last_ready_count() const {
    return m_last_ready_count;
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
