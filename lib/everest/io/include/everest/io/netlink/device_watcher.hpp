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
 * @brief Supervises one network device over a NETLINK_ROUTE socket: carrier and presence edges and,
 * optionally, neighbour table changes.
 * @details Registers its socket on an existing \ref event::fd_event_handler through
 * \ref event::fd_event_register_interface; every callback runs on that handler's thread, no thread is
 * started. Carrier means IFF_LOWER_UP, see \ref link_tracker for its lag and IPv6 DAD caveats. The device
 * need not exist yet; presence is reported on the RTM_NEWLINK that announces it, and
 * \ref callbacks::on_initial_state marks when \ref device_present becomes meaningful. The subscription
 * precedes the initial dump, so a racing event is seen twice, not missed. ENOBUFS, a clipped datagram or
 * a truncated message trigger a fresh dump, since the cached state cannot be repaired incrementally.
 */
class device_watcher : public event::fd_event_register_interface {
public:
    /**
     * @enum diagnostic_severity
     * @brief Severity of a \ref callbacks::on_diagnostic message.
     */
    enum class diagnostic_severity {
        /// Recovered from; the watcher carries on.
        warning,
        /// Should not happen; the watcher carries on.
        error,
    };

    /**
     * @brief The events this watcher delivers. Every handler is optional.
     * @details Handlers run inside the dispatch of one datagram, which keeps using this object afterwards:
     * a handler must not destroy the watcher or call \ref open or \ref unregister_events on it.
     */
    struct callbacks {
        /**
         * @brief The carrier state of the device changed. Called only on an actual change.
         */
        std::function<void(bool carrier_up)> on_carrier_change;
        /**
         * @brief The device appeared or disappeared.
         * @details Triggered by an RTM_NEWLINK for an unknown device, an RTM_DELLINK, or a rename away
         * from the watched name. On disappearance the carrier-down edge is reported first.
         */
        std::function<void(bool present)> on_presence_change;
        /**
         * @brief A neighbour table entry of the watched device changed.
         * @details Only with neighbour watching enabled and the interface index known; other devices are
         * filtered out.
         */
        std::function<void(neighbor_report const& report)> on_neighbor;
        /**
         * @brief The initial link dump completed.
         * @details Called once. Before it, \ref device_present and \ref carrier_up report nothing seen.
         */
        std::function<void()> on_initial_state;
        /**
         * @brief The socket is unusable; nothing further is reported.
         * @details Called at most once. The reason is not logged when this handler is installed; without
         * one it goes to \ref on_diagnostic at \ref diagnostic_severity::error. The socket stays open;
         * \ref unregister_events is still required before destruction.
         */
        std::function<void(std::string const& reason)> on_fatal_error;
        /**
         * @brief Sink for non-fatal diagnostics (overrun, truncated or clipped datagram, dump rejected busy).
         * @details One line without trailing newline. Without a handler, or if it throws, it goes to std::cerr.
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
     * @details Call \ref unregister_events first or destroy the event handler first; registration installs
     * a lambda capturing `this` that this destructor does not remove. Holds after a fatal error too.
     */
    ~device_watcher() override;

    device_watcher(device_watcher const&) = delete;
    device_watcher& operator=(device_watcher const&) = delete;
    device_watcher(device_watcher&&) = delete;
    device_watcher& operator=(device_watcher&&) = delete;

    /**
     * @brief Install the event handlers.
     * @details Call before \ref open, or events from the initial dump are dropped.
     * @param[in] handlers The handlers to install
     */
    void set_callbacks(callbacks handlers);

    /**
     * @brief Open and bind the socket and request the initial state.
     * @details No callback fires from here. Call once, before \ref register_events; a second call replaces
     * the socket and orphans an existing registration, so re-open as unregister, open, register. After a
     * fatal error a fresh object is needed; the fatal flag is not cleared.
     * @return True on success, false otherwise; \ref error then holds the errno
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
     * @details False while the device is absent. Meaningful once \ref callbacks::on_initial_state fired.
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
     * @details One dump per socket at a time (EBUSY otherwise), so the neighbour dump follows the link dump.
     * @return True unless the request could not be sent
     */
    bool start_link_dump();

    /// Request a fresh dump because the cached state may be incomplete. Queued if one is in flight.
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
    /// A dump is in flight; nothing else may be requested until it ends.
    bool m_dump_in_progress{false};
    /// Send the neighbour dump once the link dump completed.
    bool m_neighbor_dump_queued{false};
    /// Start a fresh link dump once the current one completed (overrun, EBUSY, truncation).
    bool m_resync_queued{false};
};

} // namespace everest::lib::io::netlink
