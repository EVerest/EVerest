// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/udp/endpoint.hpp>

#include <cstring>
#include <stdexcept>

#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

namespace everest::lib::io::udp {

namespace {

// Set sin6_scope_id from @p iface for IPv6 addresses that need a scope
// (link-local fe80::/10 or multicast ff00::/8). Throws if the interface is
// unknown. No-op for global addresses or an empty interface.
void apply_v6_scope(sockaddr_in6& sin6, std::string const& iface) {
    if (iface.empty()) {
        return;
    }
    if (not IN6_IS_ADDR_LINKLOCAL(&sin6.sin6_addr) && not IN6_IS_ADDR_MULTICAST(&sin6.sin6_addr)) {
        return;
    }
    unsigned int ifindex = if_nametoindex(iface.c_str());
    if (ifindex == 0) {
        throw std::runtime_error("if_nametoindex(\"" + iface + "\") failed");
    }
    sin6.sin6_scope_id = ifindex;
}

} // namespace

endpoint::endpoint(std::string const& host, std::uint16_t port, std::string iface) : m_iface(std::move(iface)) {
    sockaddr_in v4{};
    if (inet_pton(AF_INET, host.c_str(), &v4.sin_addr) == 1) {
        v4.sin_family = AF_INET;
        v4.sin_port = htons(port);
        std::memcpy(&m_storage, &v4, sizeof(v4));
        m_len = sizeof(v4);
        return;
    }

    sockaddr_in6 v6{};
    if (inet_pton(AF_INET6, host.c_str(), &v6.sin6_addr) == 1) {
        v6.sin6_family = AF_INET6;
        v6.sin6_port = htons(port);
        apply_v6_scope(v6, m_iface);
        std::memcpy(&m_storage, &v6, sizeof(v6));
        m_len = sizeof(v6);
        return;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* res = nullptr;
    const auto err = ::getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res);
    if (err != 0 || res == nullptr) {
        throw std::runtime_error("Failed to resolve endpoint \"" + host + "\": " + ::gai_strerror(err));
    }
    std::memcpy(&m_storage, res->ai_addr, res->ai_addrlen);
    m_len = res->ai_addrlen;
    ::freeaddrinfo(res);

    if (m_storage.ss_family == AF_INET6) {
        sockaddr_in6 scoped{};
        std::memcpy(&scoped, &m_storage, sizeof(scoped));
        scoped.sin6_port = htons(port);
        apply_v6_scope(scoped, m_iface);
        std::memcpy(&m_storage, &scoped, sizeof(scoped));
    }
}

endpoint::endpoint(sockaddr_storage const& src, socklen_t len) : m_len(len) {
    std::memcpy(&m_storage, &src, sizeof(m_storage));
}

sa_family_t endpoint::family() const {
    return m_len == 0 ? AF_UNSPEC : m_storage.ss_family;
}

std::uint16_t endpoint::port() const {
    if (m_storage.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<sockaddr_in const*>(&m_storage)->sin_port);
    }
    if (m_storage.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<sockaddr_in6 const*>(&m_storage)->sin6_port);
    }
    return 0;
}

std::string endpoint::addr_str() const {
    char buf[INET6_ADDRSTRLEN]{};
    if (m_storage.ss_family == AF_INET) {
        inet_ntop(AF_INET, &reinterpret_cast<sockaddr_in const*>(&m_storage)->sin_addr, buf, sizeof(buf));
    } else if (m_storage.ss_family == AF_INET6) {
        inet_ntop(AF_INET6, &reinterpret_cast<sockaddr_in6 const*>(&m_storage)->sin6_addr, buf, sizeof(buf));
    }
    return std::string(buf);
}

sockaddr const* endpoint::sa() const {
    return reinterpret_cast<sockaddr const*>(&m_storage);
}

socklen_t endpoint::sa_len() const {
    return m_len;
}

std::string const& endpoint::iface() const {
    return m_iface;
}

bool endpoint::is_v4_mapped() const {
    if (m_storage.ss_family != AF_INET6) {
        return false;
    }
    auto const* s6 = reinterpret_cast<sockaddr_in6 const*>(&m_storage);
    return IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr);
}

endpoint endpoint::as_v4() const {
    if (not is_v4_mapped()) {
        return endpoint{};
    }
    auto const* s6 = reinterpret_cast<sockaddr_in6 const*>(&m_storage);
    sockaddr_storage ss{};
    auto* s4 = reinterpret_cast<sockaddr_in*>(&ss);
    s4->sin_family = AF_INET;
    s4->sin_port = s6->sin6_port; // network order preserved
    std::memcpy(&s4->sin_addr.s_addr, reinterpret_cast<uint8_t const*>(&s6->sin6_addr) + 12, 4);
    return endpoint(ss, sizeof(sockaddr_in));
}

bool endpoint::operator==(endpoint const& other) const {
    if (m_storage.ss_family != other.m_storage.ss_family) {
        return false;
    }
    if (m_storage.ss_family == AF_INET) {
        auto const* a = reinterpret_cast<sockaddr_in const*>(&m_storage);
        auto const* b = reinterpret_cast<sockaddr_in const*>(&other.m_storage);
        return a->sin_port == b->sin_port && a->sin_addr.s_addr == b->sin_addr.s_addr;
    }
    if (m_storage.ss_family == AF_INET6) {
        auto const* a = reinterpret_cast<sockaddr_in6 const*>(&m_storage);
        auto const* b = reinterpret_cast<sockaddr_in6 const*>(&other.m_storage);
        return a->sin6_port == b->sin6_port && a->sin6_scope_id == b->sin6_scope_id &&
               std::memcmp(&a->sin6_addr, &b->sin6_addr, sizeof(in6_addr)) == 0;
    }
    return m_len == other.m_len;
}

} // namespace everest::lib::io::udp
