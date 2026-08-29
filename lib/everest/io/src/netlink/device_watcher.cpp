// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/device_watcher.hpp>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>

#include <everest/io/event/fd_event_handler.hpp>

namespace everest::lib::io::netlink {

namespace {

/// One recv() buffer. Netlink dumps arrive in chunks of at most one page by default, and a single
/// message is far smaller still, so 32 KiB cannot realistically be clipped - and MSG_TRUNC makes it
/// detectable rather than silent if it ever is (see handle_readable).
constexpr std::size_t receive_buffer_size = 32768;

/// Bytes the socket may queue before the kernel starts dropping and reporting ENOBUFS. A link and
/// neighbour dump of a busy host has to fit, otherwise the watcher spends startup resynchronising.
constexpr int receive_buffer_bytes = 256 * 1024;

} // namespace

device_watcher::device_watcher(std::string device, bool watch_neighbors) :
    m_tracker(std::move(device)), m_watch_neighbors(watch_neighbors) {
}

device_watcher::~device_watcher() = default;

void device_watcher::set_callbacks(callbacks handlers) {
    m_callbacks = std::move(handlers);
}

bool device_watcher::open() {
    m_fd = event::unique_fd(::socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (not m_fd.is_fd()) {
        m_error = errno;
        return false;
    }

    // Best effort: a small receive buffer only costs resynchronisation, not correctness.
    int buffer_bytes = receive_buffer_bytes;
    (void)::setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, &buffer_bytes, sizeof(buffer_bytes));

    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = RTMGRP_LINK;
    if (m_watch_neighbors) {
        address.nl_groups |= RTMGRP_NEIGH;
    }
    if (::bind(m_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        m_error = errno;
        m_fd.close();
        return false;
    }

    // Subscribe first, dump second: an event that races the dump is then seen twice rather than
    // missed, and both carrier and presence are edge-filtered, so a duplicate is free.
    if (not start_link_dump()) {
        m_fd.close();
        return false;
    }

    m_error = 0;
    return true;
}

int device_watcher::error() const {
    return m_error;
}

bool device_watcher::register_events(event::fd_event_handler& handler) {
    if (not m_fd.is_fd()) {
        return false;
    }
    return handler.register_event_handler(
        m_fd.operator int(), [this](auto const&) { handle_readable(); }, event::poll_events::read);
}

bool device_watcher::unregister_events(event::fd_event_handler& handler) {
    if (not m_fd.is_fd()) {
        return false;
    }
    return handler.unregister_event_handler(m_fd.operator int());
}

bool device_watcher::device_present() const {
    return m_tracker.present();
}

bool device_watcher::carrier_up() const {
    return m_tracker.carrier();
}

int device_watcher::ifindex() const {
    return m_tracker.ifindex();
}

std::string const& device_watcher::device() const {
    return m_tracker.device();
}

void device_watcher::handle_readable() {
    std::vector<std::uint8_t> buffer(receive_buffer_size);

    // Drain: the socket is non-blocking and level-triggered, but reading everything available in
    // one wake-up keeps a dump from taking one loop iteration per datagram.
    while (true) {
        // MSG_TRUNC makes recv report the datagram's real length instead of what fitted, which is
        // the only way to tell a clipped datagram from a complete one - without it the tail is lost
        // silently and the parser just sees fewer messages than were sent.
        auto const received = ::recv(m_fd, buffer.data(), buffer.size(), MSG_TRUNC);
        if (received > 0) {
            auto const length = static_cast<std::size_t>(received);
            if (length > buffer.size()) {
                // Never observed in practice; if it happens the cached state may be missing an
                // update that only a fresh dump can supply.
                report(diagnostic_severity::error, "rtnetlink datagram of " + std::to_string(length) +
                                                       " bytes exceeded the " + std::to_string(buffer.size()) +
                                                       " byte receive buffer on device " + m_tracker.device() +
                                                       "; resynchronising with a fresh dump");
                request_resync();
                return;
            }
            dispatch(parse(buffer.data(), length));
            continue;
        }
        if (received == 0) {
            return;
        }
        auto const reason = errno;
        if (reason == EAGAIN or reason == EWOULDBLOCK) {
            return;
        }
        if (reason == EINTR) {
            continue;
        }
        if (reason == ENOBUFS) {
            // The kernel dropped multicast messages, so the cached carrier and presence may be
            // stale and no incremental update can repair that. Re-dump and let the edge filters
            // sort out what actually changed.
            report(diagnostic_severity::warning,
                   "rtnetlink socket overrun on device " + m_tracker.device() + ", resynchronising with a fresh dump");
            request_resync();
            return;
        }
        fail(std::string("rtnetlink receive failed: ") + std::strerror(reason));
        return;
    }
}

void device_watcher::dispatch(parse_result const& parsed) {
    if (parsed.error == -EBUSY) {
        // A dump was already running. Transient by definition, so it is not a fault: give up on
        // this request and start over once the socket is free again.
        //
        // INVARIANT this relies on: only this object ever sends on this socket, so EBUSY can only
        // mean a dump *we* started is still running, and its NLMSG_DONE is therefore still coming -
        // which is what drains the queued resync. Were EBUSY ever possible without one of our own
        // dumps outstanding, the queued resync would never be sent and the cached carrier state
        // would stay stale for good.
        report(diagnostic_severity::warning, "rtnetlink dump on device " + m_tracker.device() +
                                                 " was rejected as busy; retrying once the running dump finishes");
        m_resync_queued = true;
        return;
    }
    if (parsed.error != 0) {
        fail(std::string("rtnetlink reported an error: ") + std::strerror(-parsed.error));
        return;
    }
    if (parsed.truncated) {
        // Whatever decoded before the bad message is still valid, but the rest of this datagram is
        // lost, so ask for a fresh dump rather than carrying on with a possibly incomplete view.
        report(diagnostic_severity::warning,
               "truncated rtnetlink message on device " + m_tracker.device() + "; resynchronising with a fresh dump");
        request_resync();
    }

    for (auto const& report_item : parsed.links) {
        auto const change = m_tracker.apply(report_item);
        // Presence first: "the device is back" before "and it has carrier" is the order a
        // consumer can act on, and a removal reports the carrier loss before the disappearance.
        if (change.presence_changed and change.present and m_callbacks.on_presence_change) {
            m_callbacks.on_presence_change(true);
        }
        if (change.carrier_changed and m_callbacks.on_carrier_change) {
            m_callbacks.on_carrier_change(change.carrier);
        }
        if (change.presence_changed and not change.present and m_callbacks.on_presence_change) {
            m_callbacks.on_presence_change(false);
        }
    }

    if (parsed.dump_done) {
        m_dump_in_progress = false;
        // The link dump is requested first, so the first NLMSG_DONE ends it and presence and
        // carrier are settled from here on. Before that they only say "nothing seen yet".
        if (not m_initial_state_reported) {
            m_initial_state_reported = true;
            if (m_callbacks.on_initial_state) {
                m_callbacks.on_initial_state();
            }
        }
        continue_dumps();
    }

    if (not m_watch_neighbors or m_tracker.ifindex() == 0 or not m_callbacks.on_neighbor) {
        return;
    }
    for (auto const& neighbor : parsed.neighbors) {
        if (neighbor.ifindex == m_tracker.ifindex()) {
            m_callbacks.on_neighbor(neighbor);
        }
    }
}

void device_watcher::request_resync() {
    // Collapses repeats: m_resync_queued is a flag, not a counter, so a storm of overruns or
    // truncations cannot queue an unbounded number of dumps.
    if (not start_link_dump()) {
        fail(std::string("failed to resynchronise the rtnetlink socket: ") + std::strerror(m_error));
    }
}

bool device_watcher::start_link_dump() {
    if (m_dump_in_progress) {
        // One dump at a time per socket; the kernel answers a second request with EBUSY.
        m_resync_queued = true;
        return true;
    }
    if (not request_dump(RTM_GETLINK, AF_UNSPEC, sizeof(ifinfomsg))) {
        return false;
    }
    m_dump_in_progress = true;
    m_neighbor_dump_queued = m_watch_neighbors;
    return true;
}

void device_watcher::continue_dumps() {
    if (m_resync_queued) {
        m_resync_queued = false;
        m_neighbor_dump_queued = false;
        if (not start_link_dump()) {
            fail("failed to restart the rtnetlink dump");
        }
        return;
    }
    if (not m_neighbor_dump_queued) {
        return;
    }
    m_neighbor_dump_queued = false;
    if (request_dump(RTM_GETNEIGH, AF_UNSPEC, sizeof(ndmsg))) {
        m_dump_in_progress = true;
    } else {
        fail(std::string("failed to request the neighbour table dump: ") + std::strerror(m_error));
    }
}

bool device_watcher::request_dump(std::uint16_t type, std::uint8_t family, std::size_t body_size) {
    // One buffer for either body; both start with a family byte, which is all a dump request needs.
    std::vector<std::uint8_t> request(NLMSG_SPACE(body_size), 0);
    nlmsghdr header{};
    header.nlmsg_len = static_cast<std::uint32_t>(NLMSG_LENGTH(body_size));
    header.nlmsg_type = type;
    header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    header.nlmsg_seq = ++m_sequence;
    std::memcpy(request.data(), &header, sizeof(header));
    request[NLMSG_HDRLEN] = family;

    if (::send(m_fd, request.data(), header.nlmsg_len, 0) < 0) {
        m_error = errno;
        return false;
    }
    return true;
}

void device_watcher::report(diagnostic_severity severity, std::string const& message) const {
    if (m_callbacks.on_diagnostic) {
        // Swallowed on purpose: this is the reporting path for failures, so it must not turn a
        // handler's exception into a second, worse one. Falls back to std::cerr like the
        // no-handler case, so nothing is lost.
        try {
            m_callbacks.on_diagnostic(severity, message);
            return;
        } catch (...) {
            // fall through to std::cerr
        }
    }
    std::cerr << message << std::endl;
}

void device_watcher::fail(std::string const& reason) {
    if (m_failed) {
        return;
    }
    m_failed = true;
    if (m_callbacks.on_fatal_error) {
        // One event, one channel: the consumer that installed this owns the reporting, so the
        // watcher deliberately does not also log the reason. Documented on the callback.
        m_callbacks.on_fatal_error(reason);
        return;
    }
    // Nobody is listening for the fatal path, so the failure goes out as a diagnostic instead of
    // vanishing.
    report(diagnostic_severity::error, reason + " (device " + m_tracker.device() + ")");
}

} // namespace everest::lib::io::netlink
