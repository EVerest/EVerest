// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/fd_event_handler.hpp>
#include <everest/io/socket/socket.hpp>

#include <cerrno>
#include <utility>

namespace everest::lib::io::event {

namespace {
// Error state 0 means cleared, so a protocol level failure with no errno of its own must
// not be reported as 0 or the client reports success on a connection it gave up on.
int genuine_error_code(int error_code) {
    return error_code != 0 ? error_code : ECONNRESET;
}

// A local failure with no errno of its own is not a peer reset, so it must not be reported as one.
int local_error_code(int error_code) {
    return error_code != 0 ? error_code : EPROTO;
}

bool is_monitorable(poll_events desired) {
    return desired == poll_events::read or desired == poll_events::write;
}

constexpr char unsupported_event_text[]{"handshake policy requested an event that is neither read nor write"};
constexpr char monitor_failed_text[]{"the connection could not be monitored for the event the handshake waits for"};
constexpr char deadline_arm_failed_text[]{"handshake deadline could not be armed"};
constexpr char deadline_register_failed_text[]{"handshake deadline could not be registered"};
} // namespace

generic_fd_event_client_impl::generic_fd_event_client_impl(action const& send_one, action const& receive_one,
                                                           action const& reset_client, error_status const& get_error,
                                                           handshake_status const& handshake_pending,
                                                           action const& drive_handshake,
                                                           handshake_events const& handshake_desired_events,
                                                           handshake_timeout const& get_handshake_timeout,
                                                           error_text const& get_error_string) :
    m_send_one(send_one),
    m_receive_one(receive_one),
    m_reset_client(reset_client),
    m_get_error(get_error),
    m_handshake_pending(handshake_pending),
    m_drive_handshake(drive_handshake),
    m_handshake_desired_events(handshake_desired_events),
    m_get_handshake_timeout(get_handshake_timeout),
    m_get_error_string(get_error_string) {
    m_event_handler = std::make_unique<event::fd_event_handler>();
    m_handshake_timer.set_single_shot(true);
    // Registered once and for all: a timerfd notifies only while it is running.
    auto const deadline_registered =
        m_event_handler->register_event_handler(&m_handshake_timer, [this]() { handshake_deadline_expired(); });
    if (not deadline_registered) {
        // Read errno before anything else overwrites it.
        auto const error_code = errno;
        m_handshake_deadline_register_error = error_code != 0 ? error_code : ENOTSUP;
    }
}

generic_fd_event_client_impl::~generic_fd_event_client_impl() {
    unregister_recorded_events();
}

int generic_fd_event_client_impl::get_poll_fd() {
    return m_event_handler->get_poll_fd();
}

sync_status generic_fd_event_client_impl::sync() {
    return sync_impl(-1);
}

sync_status generic_fd_event_client_impl::sync_impl(int timeout_ms) {
    auto result = m_event_handler->poll(std::chrono::milliseconds(timeout_ms));
    m_event_handler->run_actions();

    // The error handler must be called after all event handlers have run.
    // Removing handlers during error processing is likely to result in
    // inconsistent state and and segmentations fault.
    return result ? sync_status::ok : sync_status::timeout;
}

bool generic_fd_event_client_impl::setup_error_event_handler() {
    return m_event_handler->register_event_handler(&m_error_status_event_fd, [this](auto) {
        // A fresh client has no failure to report and no error to clear, so it stays inert here.
        // Tearing it down would be unpaired: error_handler does not reopen the device.
        auto const state = current_connection_state();
        if (state == utilities::connection_state::failed) {
            error_handler();
            call_error_handler(m_error, take_error_text());
            return sync_status::error;
        }
        if (state == utilities::connection_state::connected and clear_error_pending()) {
            clear_error_handler(m_error);
        }
        return sync_status::ok;
    });
}

void generic_fd_event_client_impl::setup_io_event_handler(int fd) {
    using namespace everest::lib::io::event;
    m_connection_failed = false;
    // Registered for reading below.
    m_rx_paused = false;
    // Belongs to the previous connection, so it may not describe a failure of this one.
    m_local_error_text.clear();
    m_event_handler->register_event_handler(
        fd,
        [this, fd](auto events) {
            // A terminal notification wins over a pending handshake: the connection it would be
            // negotiated on is gone. read_hungup is monitored only while paused, see pause_rx.
            if (events.count(poll_events::error) or events.count(poll_events::hungup) or
                events.count(poll_events::read_hungup)) {
                // Level triggered, so the fd keeps notifying until the queued teardown removes it,
                // and reading SO_ERROR clears it.
                if (not m_connection_failed) {
                    m_connection_failed = true;
                    auto const kind = events.count(poll_events::error) != 0    ? poll_events::error
                                      : events.count(poll_events::hungup) != 0 ? poll_events::hungup
                                                                               : poll_events::read_hungup;
                    set_error_status_and_notify(consume_poll_error(fd, kind));
                }
                return;
            }
            if (m_handshake_pending()) {
                drive_handshake(fd);
                return;
            }
            auto success = true;
            if (events.count(poll_events::read)) {
                success = rx_handler();
            }
            if (success and events.count(poll_events::write)) {
                tx_handler(fd);
            }
        },
        poll_events::read);
    m_event_handler->register_event_handler(&m_io_event_fd, [this, fd](auto) {
        if (m_handshake_pending()) {
            // The handshake monitors a single event, so monitoring write here would step it on an
            // event it never asked for. The queue is flushed once it completes.
            return;
        }
        m_event_handler->modify_event_handler(fd, poll_events::write, event_modification::add);
    });
}

void generic_fd_event_client_impl::set_error_handler(cb_error const& handler) {
    add_action([this, handler]() { m_error = handler; });
}

bool generic_fd_event_client_impl::unregister_source(int fd) {
    return m_event_handler->remove_event_handler(fd);
}

bool generic_fd_event_client_impl::set_error_status_and_notify(int error_code) {
    auto result = set_error_status(error_code);
    m_error_status_event_fd.notify();
    return result;
}

bool generic_fd_event_client_impl::rx_handler() {
    auto status = m_receive_one();
    if (status == action_status::empty) {
        // The read belongs to a connection that has been retired. Reporting a result on it would
        // put the state back to connected, or its errno to failed, for the connection replacing
        // it. Returning false also keeps the write branch of this dispatch shut.
        return false;
    }
    auto error_code = status == action_status::success ? 0 : m_get_error();
    auto result = set_error_status_and_notify(error_code);
    return result;
}

bool generic_fd_event_client_impl::tx_handler(int fd) {
    // We send one message only, even if more data is queued.
    // This prevents the kernel buffer from filling up
    auto status = m_send_one();
    switch (status) {
    case action_status::empty: {
        // if there are no more message we no longer listen to writeable events
        // otherwise we wait for the socket to become writeable again.
        m_event_handler->modify_event_handler(fd, event::poll_events::write, event::event_modification::remove);
        return true;
    }
    case action_status::fail: {
        auto error_code = m_get_error();
        set_error_status_and_notify(error_code);
        return false;
    }
    case action_status::success: {
        set_error_status_and_notify(0);
        return true;
    }
    }
    return true;
}

void generic_fd_event_client_impl::error_handler() {
    add_action([this]() { m_reset_client(); });
}

void generic_fd_event_client_impl::drive_handshake(int fd) {
    // Teardown is queued, so events keep arriving until it runs. Never step a dead handshake.
    if (m_connection_failed) {
        return;
    }
    if (not start_handshake_deadline()) {
        return;
    }
    switch (m_drive_handshake()) {
    case action_status::success: {
        auto const desired = m_handshake_desired_events();
        if (not is_monitorable(desired)) {
            fail_connection(local_error_code(m_get_error()), unsupported_event_text);
            return;
        }
        if (not monitor_for(fd, desired)) {
            fail_connection(local_error_code(m_get_error()), monitor_failed_text);
        }
        return;
    }
    case action_status::empty: {
        m_handshake_timer.disarm();
        if (not monitor_for(fd, poll_events::read)) {
            // A ready client on an unmonitored fd would leave the consumer waiting forever.
            fail_connection(local_error_code(m_get_error()), monitor_failed_text);
            return;
        }
        maybe_fire_ready();
        return;
    }
    case action_status::fail: {
        fail_connection();
        return;
    }
    default:
        fail_connection();
        return;
    }
}

void generic_fd_event_client_impl::fail_connection() {
    fail_connection(m_get_error());
}

void generic_fd_event_client_impl::fail_connection(int error_code) {
    m_connection_failed = true;
    m_handshake_timer.disarm();
    set_error_status_and_notify(genuine_error_code(error_code));
}

void generic_fd_event_client_impl::fail_connection(int error_code, std::string text) {
    m_local_error_text = std::move(text);
    fail_connection(error_code);
}

std::string generic_fd_event_client_impl::take_error_text() {
    if (not m_local_error_text.empty()) {
        return std::exchange(m_local_error_text, std::string{});
    }
    return m_get_error_string ? m_get_error_string() : std::string{};
}

bool generic_fd_event_client_impl::start_handshake_deadline() {
    if (m_handshake_deadline_register_error != 0) {
        fail_connection(local_error_code(m_handshake_deadline_register_error), deadline_register_failed_text);
        return false;
    }
    auto const generation = m_connected_generation.load();
    // The bound is on the whole handshake, so later steps keep the deadline the first one armed.
    if (m_handshake_deadline_generation == generation) {
        return true;
    }
    auto const timeout = m_get_handshake_timeout ? m_get_handshake_timeout() : default_handshake_timeout;
    // A non positive timeout stops a timerfd, which is the unbounded handshake this guards.
    if (not m_handshake_timer.set_timeout(timeout > std::chrono::milliseconds::zero() ? timeout
                                                                                      : default_handshake_timeout)) {
        // Read errno before anything else overwrites it.
        auto const error_code = errno;
        fail_connection(local_error_code(error_code), deadline_arm_failed_text);
        return false;
    }
    // Recorded only once the deadline is in force: no record may claim a bound that never armed.
    m_handshake_deadline_generation = generation;
    return true;
}

void generic_fd_event_client_impl::handshake_deadline_expired() {
    m_handshake_timer.disarm();
    // One poll batch can carry both the expiry and the event that finished or replaced the
    // handshake, so only the pending handshake this deadline was started for may be failed.
    if (m_connection_failed or m_handshake_deadline_generation != m_connected_generation.load() or
        not m_handshake_pending()) {
        return;
    }
    fail_connection(ETIMEDOUT);
}

void generic_fd_event_client_impl::retire_handshake_deadline() {
    m_handshake_timer.disarm();
}

int generic_fd_event_client_impl::consume_poll_error(int fd, poll_events kind) {
    auto error = m_get_error ? m_get_error() : 0;
    if (error != 0) {
        return error;
    }
    // With no code to read back, the fallback describes the notification rather than naming a
    // cause: this client also drives serial lines and tun/tap devices, where ECONNRESET would
    // invent a peer that reset nothing. read_hungup is a stream peer closing: ECONNRESET, as the
    // read path reports it.
    auto const fallback = kind == poll_events::hungup ? ENOTCONN : kind == poll_events::read_hungup ? ECONNRESET : EIO;
    return socket::consume_poll_error(fd, kind, fallback);
}

bool generic_fd_event_client_impl::pause_rx() {
    return set_rx_paused(true);
}

bool generic_fd_event_client_impl::resume_rx() {
    return set_rx_paused(false);
}

int generic_fd_event_client_impl::connected_fd() {
    auto client_status = m_client_status.handle();
    return client_status->ok ? client_status->fd : -1;
}

bool generic_fd_event_client_impl::rx_paused() const {
    return m_rx_paused;
}

bool generic_fd_event_client_impl::set_rx_paused(bool paused) {
    // The handshake owns the monitored events.
    if (m_handshake_pending()) {
        return false;
    }
    // Between a reopen and the registration of its descriptor there is nothing to pause.
    auto const fd = connected_fd();
    if (fd < 0 or not m_event_handler->is_registered(fd)) {
        return false;
    }
    if (m_rx_paused == paused) {
        return true;
    }
    // Paused, EPOLLRDHUP stands in for readability to notice the peer closing (non-socket
    // descriptors ignore it). One modification, so a failure changes nothing.
    auto const off = paused ? poll_events::read : poll_events::read_hungup;
    auto const on = paused ? poll_events::read_hungup : poll_events::read;
    if (not m_event_handler->modify_event_handler(fd, fd_event_handler::event_list{on},
                                                  fd_event_handler::event_list{off})) {
        return false;
    }
    m_rx_paused = paused;
    return true;
}

bool generic_fd_event_client_impl::monitor_for(int fd, poll_events desired) {
    // Otherwise both would be removed and the fd would be monitored for nothing.
    if (not is_monitorable(desired)) {
        return false;
    }
    // One modification: never both or neither in between.
    auto const other = desired == poll_events::read ? poll_events::write : poll_events::read;
    return m_event_handler->modify_event_handler(fd, fd_event_handler::event_list{desired},
                                                 fd_event_handler::event_list{other});
}

poll_events generic_fd_event_client_impl::default_desired_events() {
    return poll_events::read;
}

void generic_fd_event_client_impl::prepare_io_event_handler() {
    m_event_handler->register_event_handler(&m_connected_event_fd, [this](auto) {
        auto ok = false;
        auto fd = -1;
        {
            // Copy out under the lock only: the handshake setup below runs a policy callback.
            auto client_status = m_client_status.handle();
            auto generation = m_connected_generation.load();
            if (client_status->generation != generation) {
                return sync_status::ok;
            }
            ok = client_status->ok;
            fd = client_status->fd;
        }

        auto error_code = m_get_error();
        set_error_status_and_notify(error_code);
        if (not ok) {
            return sync_status::ok;
        }

        setup_io_event_handler(fd);
        if (m_handshake_pending()) {
            // The client owes the first handshake message, so it cannot wait for readability.
            drive_handshake(fd);
        } else {
            maybe_fire_ready();
        }

        return sync_status::ok;
    });
}

void generic_fd_event_client_impl::on_client_ready(std::uint64_t generation, bool ok, int fd) {
    auto client_status = m_client_status.handle();
    client_status->generation = generation;
    client_status->ok = ok;
    client_status->fd = fd;
    set_connected_generation(generation);
    m_connected_event_fd.notify();
}

void generic_fd_event_client_impl::set_connected_generation(std::uint64_t generation) {
    m_connected_generation.store(generation);
}

void generic_fd_event_client_impl::add_action(fd_event_handler::task&& item) {
    m_event_handler->add_action(std::forward<fd_event_handler::task>(item));
}

void generic_fd_event_client_impl::register_async_connect_event_handler(event_fd* event_fd,
                                                                        std::function<void()> handler) {
    m_event_handler->register_event_handler(event_fd, [handler = std::move(handler)](auto) mutable { handler(); });
}

void generic_fd_event_client_impl::set_on_ready_action(ready_action&& item) {
    // Assign on the loop thread, where it is read when a connection becomes ready.
    add_action([this, item = std::move(item)]() mutable {
        m_on_ready_action = std::move(item);
        // A connected transport is not a ready client while its handshake is outstanding, and
        // the connected status outlives a failure until the queued teardown runs.
        if (connected_fd() >= 0 and not m_connection_failed and not m_handshake_pending()) {
            // Explicit registration on a ready connection asks to be called now.
            m_ready_fired_generation = 0;
            maybe_fire_ready();
        }
    });
}

void generic_fd_event_client_impl::maybe_fire_ready() {
    // Recorded per connection, not per monitor change: the record can be cleared before it changes.
    auto generation = m_connected_generation.load();
    if (m_ready_fired_generation == generation or not m_on_ready_action) {
        return;
    }
    m_ready_fired_generation = generation;
    m_event_handler->add_action(m_on_ready_action);
}

} // namespace everest::lib::io::event
