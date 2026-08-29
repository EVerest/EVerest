// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <everest/io/event/fd_event_register_interface.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/netlink/link_tracker.hpp>
#include <everest/io/netlink/route_parser.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace everest::lib::io::netlink {

/**
 * @brief Supervises one network device over a NETLINK_ROUTE socket, reporting carrier and presence
 * edges and, optionally, its neighbour table changes.
 *
 * @details Owns the socket, the subscription and the initial state dump, and reduces the message
 * stream to the handful of events a consumer cares about. Registers its socket on an existing
 * \ref event::fd_event_handler through \ref event::fd_event_register_interface, so everything -
 * including every callback - runs on that handler's thread. Nothing here starts a thread.
 *
 * <b>Carrier means IFF_LOWER_UP</b>, for the reasons documented on \ref link_tracker, which also
 * covers the two timing properties a consumer has to live with: a carrier-down announcement can lag
 * the physical event by up to about a second, and a carrier-up edge is not the same as "IPv6
 * usable" because the kernel re-runs duplicate address detection on it.
 *
 * <b>The device need not exist yet.</b> A device created at runtime - a TAP set up by an
 * application after this watcher started, typically - is a normal condition and not an error: the
 * watcher waits for the RTM_NEWLINK that announces it and reports presence then. Use
 * \ref callbacks::on_initial_state to find out when the answer to \ref device_present is real
 * rather than "nothing seen yet".
 *
 * <b>Robustness.</b> The subscription is established before the initial dump is requested, so an
 * event racing the dump is seen twice rather than missed - which the edge filters absorb. A socket
 * overrun (ENOBUFS), a clipped datagram or a truncated message all trigger a fresh dump, because
 * once messages have been dropped no incremental update can repair the cached state.
 */
class device_watcher : public event::fd_event_register_interface {
public:
    /**
     * @enum diagnostic_severity
     * @brief How bad a \ref callbacks::on_diagnostic message is.
     */
    enum class diagnostic_severity {
        /// Something recoverable happened and was recovered from; the watcher carries on.
        warning,
        /// Something that should not happen did; the watcher carries on, but say it loudly.
        error,
    };

    /**
     * @brief The events this watcher delivers. Every handler is optional.
     *
     * @details <b>Re-entrancy.</b> Every handler is called from inside the dispatch of one received
     * datagram, which keeps reading this object's members after the handler returns - the message
     * loop continues through the rest of the datagram and then reads the socket again. A handler
     * may therefore not destroy the watcher, and must not call \ref open or
     * \ref unregister_events on it. Feeding the events into a state machine, publishing them, and
     * arming timers is what they are for. If a handler concludes that the watcher must go away, it
     * has to record that and act on it after the event handler returns.
     */
    struct callbacks {
        /**
         * @brief The carrier state of the device changed.
         * @details Only called on an actual change, never to repeat the current state.
         */
        std::function<void(bool carrier_up)> on_carrier_change;
        /**
         * @brief The device appeared or disappeared.
         * @details Triggered by an RTM_NEWLINK for a device that was not there, by an RTM_DELLINK,
         * or by a rename away from the watched name. On a disappearance the carrier-down edge is
         * reported first, so a consumer sees the link go down before the device goes away.
         */
        std::function<void(bool present)> on_presence_change;
        /**
         * @brief A neighbour table entry of the watched device changed.
         * @details Only delivered when the watcher was constructed with neighbour watching enabled
         * and the device's interface index is known. Entries of other devices are filtered out.
         */
        std::function<void(neighbor_report const& report)> on_neighbor;
        /**
         * @brief The initial link dump completed.
         * @details Called once. Before it, \ref device_present and \ref carrier_up only say
         * "nothing seen yet"; afterwards they describe the device.
         */
        std::function<void()> on_initial_state;
        /**
         * @brief The socket is unusable and this watcher will report nothing further.
         * @details Called at most once. What to do about it is the consumer's decision - a
         * supervising module will typically surface it as an error.
         *
         * The watcher does <b>not</b> additionally log the reason when this handler is installed:
         * one event, one channel. A consumer that installs it owns the reporting, and a consumer
         * that does not gets the reason on \ref on_diagnostic at
         * \ref diagnostic_severity::error instead, so the failure is never silent either way.
         *
         * The socket is left open, so \ref unregister_events still works afterwards and is still
         * required before destruction.
         */
        std::function<void(std::string const& reason)> on_fatal_error;
        /**
         * @brief Sink for diagnostics that do not stop the watcher: a socket overrun, a truncated
         * or clipped datagram, a dump rejected as busy.
         * @details Receives one fully formatted line without a trailing newline, plus how bad it
         * is - a consumer with a logging framework is expected to map \ref diagnostic_severity
         * onto its own levels rather than flatten them. Without a handler these go to std::cerr.
         * Exceptions thrown by the handler are swallowed and the message falls back to std::cerr,
         * since reporting happens on error paths.
         */
        std::function<void(diagnostic_severity severity, std::string const& message)> on_diagnostic;
    };

    /**
     * @brief Constructor. Does not touch the network; call \ref open for that.
     * @param[in] device Name of the network device to watch
     * @param[in] watch_neighbors Subscribe RTMGRP_NEIGH and dump the neighbour table as well
     */
    device_watcher(std::string device, bool watch_neighbors);

    /**
     * @brief Destructor. Closes the socket.
     * @details <b>The watcher must be unregistered from its event handler first.</b> Registration
     * installs a lambda that captures `this`, and this destructor does not remove it: a handler
     * that outlives the watcher and still polls the (now closed) descriptor would call into freed
     * memory. Call \ref unregister_events, or destroy the event handler first. This holds after a
     * fatal error too - failing does not close the socket or drop the registration.
     */
    ~device_watcher() override;

    device_watcher(device_watcher const&) = delete;
    device_watcher& operator=(device_watcher const&) = delete;
    device_watcher(device_watcher&&) = delete;
    device_watcher& operator=(device_watcher&&) = delete;

    /**
     * @brief Install the event handlers.
     * @details Do this before \ref open, or events from the initial dump are dropped.
     * @param[in] handlers The handlers to install
     */
    void set_callbacks(callbacks handlers);

    /**
     * @brief Open and bind the socket and request the initial state.
     * @details No callback fires from here; they arrive once the socket is registered on a handler
     * and that handler polls.
     *
     * Call this once, before \ref register_events. A second call replaces the socket, which
     * orphans any existing registration: the event handler would keep polling the old descriptor,
     * which this object no longer owns. Re-opening therefore means unregister, open, register - and
     * re-opening after a fatal error additionally needs a fresh object, because the fatal flag is
     * not cleared (it exists to keep one failure from being reported repeatedly).
     * @return True on success, false otherwise - \ref error then holds the errno
     */
    bool open();

    /**
     * @brief The errno of the last failed socket operation.
     * @return The errno, or 0 if nothing failed
     */
    int error() const;

    /**
     * @brief Register the netlink socket with an event handler
     * @param[in] handler The event handler to register with
     * @return True on success, false otherwise
     */
    bool register_events(event::fd_event_handler& handler) override;

    /**
     * @brief Unregister the netlink socket from an event handler
     * @param[in] handler The event handler to unregister from
     * @return True on success, false otherwise
     */
    bool unregister_events(event::fd_event_handler& handler) override;

    /**
     * @brief Whether the watched device currently exists.
     * @details Meaningful once \ref callbacks::on_initial_state has fired.
     * @return True if the device is present, false otherwise
     */
    bool device_present() const;

    /**
     * @brief The current carrier state (\c IFF_LOWER_UP) of the watched device.
     * @details False whenever the device is absent. Meaningful once
     * \ref callbacks::on_initial_state has fired.
     * @return True if the device has carrier, false otherwise
     */
    bool carrier_up() const;

    /**
     * @brief The interface index of the watched device.
     * @return The interface index, or 0 while it is unknown
     */
    int ifindex() const;

    /**
     * @brief The device name this watcher was constructed with.
     * @return The device name
     */
    std::string const& device() const;

private:
    void handle_readable();
    void dispatch(parse_result const& parsed);

    /**
     * @brief Request a link dump, or queue one if a dump is already in flight.
     * @details A netlink socket allows only one dump at a time; a second request is rejected with
     * EBUSY. Requesting the neighbour dump only after the link dump completed follows from that.
     * @return True unless the request could not be sent
     */
    bool start_link_dump();

    /// Ask for a fresh dump because the cached state may be incomplete. Queued if one is in flight.
    void request_resync();

    /// Send the next queued dump, if any. Called when a dump completes.
    void continue_dumps();

    bool request_dump(std::uint16_t type, std::uint8_t family, std::size_t body_size);

    /// Report a diagnostic to callbacks::on_diagnostic, or to std::cerr if none is set.
    void report(diagnostic_severity severity, std::string const& message) const;

    /// Report the socket as unusable, once.
    void fail(std::string const& reason);

    link_tracker m_tracker;
    bool m_watch_neighbors{false};
    callbacks m_callbacks{};

    event::unique_fd m_fd;
    int m_error{0};
    std::uint32_t m_sequence{0};
    bool m_failed{false};
    bool m_initial_state_reported{false};
    /// A dump is in flight: nothing else may be requested until it ends.
    bool m_dump_in_progress{false};
    /// Send the neighbour dump once the link dump has completed.
    bool m_neighbor_dump_queued{false};
    /// Start a fresh link dump once the current dump has completed (overrun, EBUSY, truncation).
    bool m_resync_queued{false};
};

} // namespace everest::lib::io::netlink
