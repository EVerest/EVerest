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
     * @param[in] carrier_on Carrier state the device is left in. The kernel default is on.
     * @note With \p carrier_on false the carrier is dropped via \p TUNSETCARRIER before the device is brought
     * up, so a device this function creates is never announced carrier-on to an rtnetlink watcher. A persistent
     * TAP device that is already \p IFF_UP is announced anyway: \p TUNSETIFF raises the carrier unconditionally.
     * \p IFF_NO_CARRIER (kernel 6.1) would avoid this; \p TUNSETCARRIER works from 5.0.
     * @note A failed carrier request does not fail this function; its errno is in \ref carrier_setup_error.
     * \ref get_error is zero after a successful open.
     * @return True on success, false otherwise.
     */
    bool open(std::string const& device, std::string const& ip, std::string const& netmask, int mtu,
              bool carrier_on = true);

    /**
     * @brief Set the carrier of the TAP device
     * @details Issues \p TUNSETCARRIER (\p netif_carrier_on / \p netif_carrier_off); orthogonal to \p IFF_UP.
     * No state is cached: the handler is re-created per connection by \ref event::fd_event_client, so the
     * owner restores the carrier across a reset through the \ref open argument.
     * @param[in] on True to raise the carrier, false to drop it.
     * @return True on success, false otherwise; the errno is readable via \ref get_error.
     * @note \p EINVAL or \p ENOTTY mean the running kernel does not implement \p TUNSETCARRIER (v5.0).
     */
    bool set_carrier(bool on);

    /**
     * @brief Get the errno of the carrier request made by \ref open
     * @details Separate from \ref get_error because \ref event::fd_event_client fails a fresh connection on a
     * nonzero \ref get_error right after a successful \ref open, and a kernel without \p TUNSETCARRIER must
     * still bridge. Reset by each \ref open; \ref set_carrier does not touch it.
     * @return Zero if the request succeeded or was not made (\p carrier_on true), otherwise its errno.
     * @note \p EINVAL or \p ENOTTY mean the running kernel does not implement \p TUNSETCARRIER.
     */
    int carrier_setup_error() const;

    /**
     * @brief Get the carrier of the TAP device as the kernel reports it
     * @details \p SIOCGIFFLAGS on a temporary control socket, reporting \p IFF_RUNNING. For diagnostics and
     * tests; consumers watch rtnetlink instead.
     * @note Lags \ref set_carrier: \p IFF_RUNNING follows the operstate, which linkwatch updates asynchronously
     * at roughly one update per second. The instantaneous bit \p IFF_LOWER_UP (0x10000) does not fit the 16 bit
     * \p ifr_flags; only the 32 bit \p ifi_flags of an rtnetlink \p RTM_NEWLINK message carries both.
     * @return The carrier state, or no value if the query failed or no device is held (also after a failed open).
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
