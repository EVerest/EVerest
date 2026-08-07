// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#include <iso15118/io/poll_manager.hpp>

#include <cerrno>
#include <type_traits>
#include <vector>

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

static PollSet create_poll_set(const std::map<int, PollCallback>& map, int event_fd) {
    const auto total_size = map.size() + 1; // including event_fd
    decltype(PollSet::fds) fds(total_size);
    decltype(PollSet::callbacks) callbacks(total_size);

    auto index = 0;
    for (auto it = map.begin(); it != map.end(); ++it, ++index) {
        fds[index].fd = it->first;
        fds[index].events = POLLIN;
        callbacks[index] = &it->second;
    }

    fds[index].fd = event_fd;
    fds[index].events = POLLIN;

    return {std::move(fds), std::move(callbacks)};
}

PollManager::PollManager() {
    event_fd = eventfd(0, 0);
    if (event_fd == -1) {
        log_and_throw("Failed to create eventfd");
    }
}

void PollManager::register_fd(int fd, PollCallback& poll_callback) {
    registered_fds.emplace(fd, poll_callback);
    poll_set = create_poll_set(registered_fds, event_fd);
}

void PollManager::unregister_fd(int fd) {
    registered_fds.erase(fd);
    poll_set = create_poll_set(registered_fds, event_fd);
}

void PollManager::poll(int timeout_ms) {

    auto& pollfds = poll_set.fds;

    const auto ret = ::poll(pollfds.data(), pollfds.size(), timeout_ms);

    if (ret == -1) {
        // EINTR (a signal interrupted the syscall) is transient: report no events this round
        // instead of killing the controller loop. Anything else is a genuine poll failure.
        if (errno == EINTR) {
            return;
        }
        log_and_throw("Poll failed\n");
    }

    if (ret == 0) {
        // timeout
        return;
    }

    // first check for event_fd
    if (pollfds[pollfds.size() - 1].revents & POLLIN) {
        eventfd_t tmp;
        eventfd_read(event_fd, &tmp);

        // just break;
        return;
    }

    // check fds
    for (std::size_t i = 0; i < pollfds.size() - 1; ++i) {
        if (pollfds[i].revents & POLLIN) {
            // Contain callback failures here instead of letting them escape into the controller
            // loop, whose catch exits permanently and leaves the SECC dead until a process restart.
            // Unregister the offending fd so a level-triggered readable fd whose callback keeps
            // throwing cannot spin the loop hot. Unregistration is safe for every registrant: the
            // SDP server callback contains its own exceptions (an unregistered SDP fd would never
            // come back), and a session whose connection fd is unregistered is reaped by the
            // controller's communication-setup timeout or its sequence timeout. Stop dispatching
            // for this round afterwards: unregister_fd rebuilds poll_set, and poll() is
            // level-triggered, so pending events on other fds re-surface next round.
            const auto callback_fd = pollfds[i].fd;
            try {
                (*poll_set.callbacks[i])();
            } catch (const std::exception& e) {
                logf_error("Poll callback for fd %d threw: %s; unregistering this fd", callback_fd, e.what());
                unregister_fd(callback_fd);
                return;
            }
        }
    }
}

void PollManager::abort() {
    eventfd_write(event_fd, 1);
}

} // namespace iso15118::io
