// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
//
// Zephyr implementation of the interface-lookup half of socket_helper.hpp
// (check_and_update_interface, get_first_sockaddr_in6_for_interface), built instead of
// socket_helper_posix.cpp when ISO15118_TARGET_ZEPHYR is ON (see src/iso15118/CMakeLists.txt and
// zephyr/CMakeLists.txt). Zephyr's socket layer has no getifaddrs(); interface/address lookup
// goes through Zephyr's native net_if_* API instead. if_nametoindex() itself is still used
// unchanged - Zephyr's POSIX layer does implement it (subsys/portability/posix/... +
// CONFIG_NET_INTERFACE_NAME, both pulled in transitively - see zephyr/Kconfig).
//
// Known behavior difference from socket_helper_posix.cpp: the POSIX version special-cases
// interface_name == "lo" to accept any address, not just a link-local one (loopback usually has
// none). This is not replicated here; ISO 15118 SECC deployments bind to a real network
// interface, not loopback.
#include <iso15118/detail/io/socket_helper.hpp>

#include <cstring>

#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

namespace {

sockaddr_in6 make_link_local_sockaddr(struct net_if* iface, const struct net_in6_addr& addr) {
    sockaddr_in6 out{};
    out.sin6_family = AF_INET6;
    std::memcpy(&out.sin6_addr, &addr, sizeof(out.sin6_addr));
    out.sin6_scope_id = static_cast<uint32_t>(net_if_get_by_iface(iface));
    return out;
}

std::string get_interface_name(struct net_if* iface) {
    char name[CONFIG_NET_INTERFACE_NAME_LEN + 1] = {};
    if (net_if_get_name(iface, name, sizeof(name)) < 0) {
        return {};
    }
    return name;
}

} // namespace

bool check_and_update_interface(std::string& interface_name) {

    if (interface_name == "auto") {
        logf_info("Search for the first available ipv6 interface");
        struct net_if* iface = nullptr;
        if (net_if_ipv6_get_ll_addr(NET_ADDR_PREFERRED, &iface) == nullptr) {
            logf_error("No interface with a link-local IPv6 address found");
            interface_name.clear();
            return false;
        }
        interface_name = get_interface_name(iface);
    }

    if (interface_name.empty() or if_nametoindex(interface_name.c_str()) == 0) {
        logf_error("No such interface: %s", interface_name.c_str());
        return false;
    }
    return true;
}

bool get_first_sockaddr_in6_for_interface(const std::string& interface_name, sockaddr_in6& address) {
    struct net_if* iface = nullptr;
    struct net_in6_addr* ll_addr = nullptr;

    if (interface_name == "auto") {
        ll_addr = net_if_ipv6_get_ll_addr(NET_ADDR_PREFERRED, &iface);
    } else {
        const auto ifindex = if_nametoindex(interface_name.c_str());
        if (ifindex != 0) {
            iface = net_if_get_by_index(static_cast<int>(ifindex));
        }
        if (iface != nullptr) {
            ll_addr = net_if_ipv6_get_ll(iface, NET_ADDR_PREFERRED);
        }
    }

    if (ll_addr == nullptr) {
        // Todo(sl): What to do if interface was not found?
        return false;
    }

    if (interface_name == "auto") {
        logf_info("Found an ipv6 link local address for interface: %s", get_interface_name(iface).c_str());
    }

    address = make_link_local_sockaddr(iface, *ll_addr);
    return true;
}

void set_ipv6_mreq_interface(ipv6_mreq& mreq, unsigned int ifindex) {
    mreq.ipv6mr_ifindex = static_cast<int>(ifindex);
}

bool bind_socket_to_interface(int fd, const std::string& interface_name) {
    struct net_ifreq ifr {};
    std::strncpy(ifr.ifr_name, interface_name.c_str(), sizeof(ifr.ifr_name) - 1);
    return setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof(ifr)) != -1;
}

void ensure_link_local_scope(int fd, sockaddr_in6& address) {
    if (address.sin6_family != AF_INET6 or address.sin6_scope_id != 0) {
        return;
    }
    if (not IN6_IS_ADDR_LINKLOCAL(&address.sin6_addr)) {
        return;
    }

    // Zephyr does not fill sin6_scope_id on recvfrom(); take the interface the socket was
    // bound to via SO_BINDTODEVICE and turn its name into the scope id sendto() needs.
    struct net_ifreq ifr {};
    socklen_t ifr_len = sizeof(ifr);
    if (getsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, &ifr_len) != 0 or ifr.ifr_name[0] == '\0') {
        return;
    }
    address.sin6_scope_id = if_nametoindex(ifr.ifr_name);
}

} // namespace iso15118::io
