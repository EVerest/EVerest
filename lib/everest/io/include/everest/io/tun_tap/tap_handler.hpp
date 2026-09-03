// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2025 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <everest/io/event/unique_fd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace everest::lib::io::tun_tap {

/**
 * tap_handler bundles basic <a href="https://docs.kernel.org/networking/tuntap.html">TAP device</a>
 * related functionality. This includes setup of the device, ip settings, lifetime management,
 * reading, writing and fundamental error checking.
 * Although this class can be used on its own, the main purpose is to be used as base class for
 * implementation the \p ClientPolicy of \ref event::fd_event_client
 */
class tap_handler {
public:
    /**
     * @var PayloadT
     * @brief The type of the payload
     */
    using PayloadT = std::vector<uint8_t>;
    /**
     * The class is default constructed
     */
    tap_handler() = default;

    /**
     * @brief Create and open a TAP device
     * @details This functions tries to create and bring up a new TAP device with the
     * given name and IP settings. A possible reason for failure is if the \p device name is not availble.
     * @param[in] device The requested name for the TAP device.
     * @param[in] ip IP address to be assigned for the TAP device
     * @param[in] netmask Netmask for the TAP device
     * @param[in] mtu The Maximum transmission unit, i.e. the maximum size of a message in bytes.
     * @param[in] carrier_on The carrier state the device is left in. The kernel creates a fresh TAP
     * device with the carrier on, so the default reproduces the kernel default. Passing \p false drops
     * the carrier before the device is brought up, which is what keeps the device from ever being
     * announced as carrier-on to an rtnetlink watcher - bringing it up while the carrier is still on
     * emits exactly such an announcement, and no later ioctl can retract it. A failed carrier request
     * does not fail this function; its errno is reported by \ref carrier_setup_error, not by
     * \ref get_error, which is required to be zero after a successful open.
     * @note The "never announced with a carrier" guarantee covers devices this function creates. It
     * does not extend to attaching to a pre-existing persistent TAP device that is already \p IFF_UP:
     * \p TUNSETIFF raises the carrier unconditionally, so that attach announces a carrier before this
     * function can drop it again. Creating the device with \p IFF_NO_CARRIER (kernel 6.1) would make
     * the initial state atomic and remove the ordering concern entirely; \p TUNSETCARRIER is used here
     * because it works from 5.0 onwards.
     * @return True on success, false otherwise.
     */
    bool open(std::string const& device, std::string const& ip, std::string const& netmask, int mtu,
              bool carrier_on = true);

    /**
     * @brief Set the carrier of the TAP device
     * @details Issues \p TUNSETCARRIER on the device fd, which calls \p netif_carrier_on / \p
     * netif_carrier_off on the netdev. This is orthogonal to \p IFF_UP, which \ref open leaves set.
     * No desired state is cached here: the handler is a thin syscall wrapper and is re-created per
     * connection by \ref event::fd_event_client, so keeping the carrier across a reset is the owner's
     * job (\ref open takes the initial state for exactly that reason).
     * @param[in] on True to raise the carrier, false to drop it.
     * @return True on success, false otherwise. On failure the errno is stored and readable via
     * \ref get_error. \p EINVAL or \p ENOTTY mean the running kernel does not implement
     * \p TUNSETCARRIER (added in v5.0), which the caller may want to treat differently from a
     * genuine error.
     */
    bool set_carrier(bool on);

    /**
     * @brief Get the outcome of the carrier request \ref open made on its own
     * @details Reported separately from \ref get_error because a successful \ref open must leave
     * \ref get_error at zero: \ref event::fd_event_client reads the policy's error right after a
     * successful open and marks the fresh connection as failed on any nonzero value, which would tear
     * the device down and drive the owner's retry loop into an endless create-destroy cycle. The
     * carrier request must not fail the open either - a kernel without \p TUNSETCARRIER has to keep
     * bridging - so its errno needs a channel of its own. This is that channel.
     * @details The value is reset by each \ref open and survives until the next one. Runtime calls to
     * \ref set_carrier do not touch it; those report through their return value and \ref get_error.
     * @return Zero when the carrier request succeeded or was never made (\p carrier_on was true),
     * otherwise the errno of the attempt. \p EINVAL or \p ENOTTY mean the running kernel does not
     * implement \p TUNSETCARRIER, which is what a caller with a fail-or-warn policy keys on.
     */
    int carrier_setup_error() const;

    /**
     * @brief Get the carrier of the TAP device as the kernel reports it
     * @details Queries \p SIOCGIFFLAGS on a temporary control socket and reports the presence of
     * \p IFF_RUNNING. Intended for diagnostics and tests; the authoritative signal for a consumer is
     * the flag change delivered by rtnetlink.
     * @note This lags \ref set_carrier. \p IFF_RUNNING is derived from the netdev's operstate, which
     * the kernel's linkwatch work updates asynchronously and dampens to roughly one update per
     * second, so a query issued right after \ref set_carrier legitimately still reports the previous
     * state. The instantaneous bit is \p IFF_LOWER_UP, which \p SIOCGIFFLAGS cannot carry at all:
     * \p ifr_flags is 16 bits wide and \p IFF_LOWER_UP is 0x10000. Only the 32 bit \p ifi_flags of an
     * rtnetlink \p RTM_NEWLINK message carries both, which is another reason for a consumer to watch
     * rtnetlink rather than poll here.
     * @return The carrier state, or no value if the query failed or this handler holds no device -
     * including after a failed \ref open, so a name that another process owns is never reported on.
     */
    std::optional<bool> carrier() const;

    /**
     * @brief Write a dataset to the TAP
     * @details Implementation for \p ClientPolicy
     * @param[in] data Payload
     * @return True on success, False otherwise.
     */
    bool tx(PayloadT const& data);
    /**
     * @brief Read a dataset from the TAP
     * @details Implementation for \p ClientPolicy
     * @param[in] data Payload
     * @return True on success, False otherwise.
     */
    bool rx(PayloadT& data);
    /**
     * @brief Get the current error
     * @details Implementation for \p ClientPolicy
     * @return The last errno. Zero if there is no error.
     */
    int get_fd() const;
    /**
     * @brief Get the current error
     * @details Implementation for \p ClientPolicy
     * @return The last errno. Zero if there is no error.
     */
    int get_error() const;

private:
    event::unique_fd m_fd;
    std::string m_device;
    int m_error{0};
    int m_carrier_setup_error{0};
    int m_mtu;
};

} // namespace everest::lib::io::tun_tap
