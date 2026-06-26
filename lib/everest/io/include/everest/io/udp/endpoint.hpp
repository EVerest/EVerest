// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

/** \file */

#pragma once

#include <cstdint>
#include <string>

#include <netinet/in.h>
#include <sys/socket.h>

namespace everest::lib::io::udp {

/**
 * @class endpoint
 * @brief A protocol-agnostic (IPv4 or IPv6) UDP address/port pair.
 *
 * Wraps a @c sockaddr_storage plus its valid length. The family is selected
 * automatically from the host string: numeric IPv4 first, then numeric IPv6,
 * then a name resolution via @c getaddrinfo (first result). The port is kept
 * in host byte order at the API surface; @c htons / @c ntohs are applied only
 * at the syscall boundary inside this class.
 *
 * For IPv6 link-local or multicast targets a non-empty @p iface is resolved to
 * @c sin6_scope_id and additionally retained as a SO_BINDTODEVICE hint
 * retrievable via @ref iface.
 *
 * Scoped to the unconnected UDP client; not a general networking primitive.
 */
class endpoint {
public:
    /**
     * @brief Default constructed endpoint (family AF_UNSPEC, no address).
     */
    endpoint() = default;

    /**
     * @brief Construct from a host string and port.
     * @param[in] host Numeric IPv4, numeric IPv6, or a resolvable name.
     * @param[in] port Port in host byte order.
     * @param[in] iface Optional interface name. Used as the scope id for IPv6
     * link-local / multicast addresses and retained as a bind hint.
     * @throws std::runtime_error if @p host cannot be parsed or resolved, or if
     * @p iface is non-empty but unknown for a scoped IPv6 address.
     */
    endpoint(std::string const& host, std::uint16_t port, std::string iface = {});

    /**
     * @brief Construct from a raw source address (e.g. from @c recvfrom).
     * @param[in] src The source address storage.
     * @param[in] len The valid length of @p src.
     */
    endpoint(sockaddr_storage const& src, socklen_t len);

    /**
     * @brief Address family.
     * @return AF_INET, AF_INET6, or AF_UNSPEC when default constructed.
     */
    sa_family_t family() const;

    /**
     * @brief Port in host byte order.
     */
    std::uint16_t port() const;

    /**
     * @brief Numeric address string (inet_ntop).
     */
    std::string addr_str() const;

    /**
     * @brief Pointer to the raw address, usable directly by @c sendto.
     */
    sockaddr const* sa() const;

    /**
     * @brief Valid length of the address returned by @ref sa.
     */
    socklen_t sa_len() const;

    /**
     * @brief Interface name hint supplied at construction (may be empty).
     */
    std::string const& iface() const;

    /**
     * @brief True iff this is an IPv4-mapped IPv6 address (::ffff:a.b.c.d).
     */
    bool is_v4_mapped() const;

    /**
     * @brief IPv4 view of a v4-mapped address.
     * @return If @ref is_v4_mapped(): an AF_INET endpoint with the embedded
     * IPv4 address and the same port. Otherwise a default endpoint
     * (family AF_UNSPEC, sa_len()==0).
     */
    endpoint as_v4() const;

    /**
     * @brief Value equality (family, address, port, IPv6 scope id).
     */
    bool operator==(endpoint const& other) const;
    bool operator!=(endpoint const& other) const {
        return not(*this == other);
    }

private:
    sockaddr_storage m_storage{};
    socklen_t m_len{0};
    std::string m_iface;
};

} // namespace everest::lib::io::udp
