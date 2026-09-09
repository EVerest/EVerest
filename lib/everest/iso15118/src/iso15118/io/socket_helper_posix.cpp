// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
//
// POSIX/Linux implementation of the interface-lookup half of socket_helper.hpp
// (check_and_update_interface, get_first_sockaddr_in6_for_interface), built instead of
// socket_helper_zephyr.cpp when ISO15118_TARGET_ZEPHYR is OFF (the default - see
// src/iso15118/CMakeLists.txt). Uses getifaddrs(), which Zephyr's socket layer does not provide.
// The rest of socket_helper.hpp (set_tcp_keepalive, create_tcp_listen_socket,
// sockaddr_in6_to_name) is plain POSIX socket code shared by both platforms, and stays in
// socket_helper.cpp.
#include <iso15118/detail/io/socket_helper.hpp>

#include <cstring>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <iso15118/detail/helper.hpp>

namespace iso15118::io {

namespace {

auto choose_first_ipv6_interface() {
    std::string interface_name{};
    struct ifaddrs* if_list_head;
    const auto get_if_addrs_result = getifaddrs(&if_list_head);

    if (get_if_addrs_result == -1) {
        logf_error("Failed to call getifaddrs");
        return std::string("");
    }

    for (auto current_if = if_list_head; current_if != nullptr; current_if = current_if->ifa_next) {
        if (current_if->ifa_addr == nullptr or current_if->ifa_addr->sa_family != AF_INET6) {
            continue;
        }

        // NOTE (aw): because we did the check for AF_INET6, we can assume that ifa_addr is indeed an sockaddr_in6
        const auto current_addr = reinterpret_cast<const sockaddr_in6*>(current_if->ifa_addr);
        if (not IN6_IS_ADDR_LINKLOCAL(&(current_addr->sin6_addr))) {
            continue;
        }
        interface_name = current_if->ifa_name;
        break; // Stop the loop if a interface is found
    }
    freeifaddrs(if_list_head);

    return interface_name;
}

} // namespace

bool check_and_update_interface(std::string& interface_name) {

    if (interface_name == "auto") {
        logf_info("Search for the first available ipv6 interface");
        interface_name = choose_first_ipv6_interface();
    }

    struct ipv6_mreq mreq {};
    mreq.ipv6mr_interface = if_nametoindex(interface_name.c_str());
    if (!mreq.ipv6mr_interface) {
        logf_error("No such interface: %s", interface_name.c_str());
        return false;
    }
    return not interface_name.empty();
}

bool get_first_sockaddr_in6_for_interface(const std::string& interface_name, sockaddr_in6& address) {
    struct ifaddrs* if_list_head;
    const auto get_if_addrs_result = getifaddrs(&if_list_head);

    if (get_if_addrs_result == -1) {
        log_and_throw("Failed to call getifaddrs");
    }

    bool found_interface = false;

    for (auto current_if = if_list_head; current_if != nullptr; current_if = current_if->ifa_next) {
        if (current_if->ifa_addr == nullptr) {
            continue;
        }

        if (current_if->ifa_addr->sa_family != AF_INET6) {
            continue;
        }

        if (interface_name.compare("auto") != 0 && interface_name.compare(current_if->ifa_name) != 0) {
            continue;
        }

        // NOTE (aw): because we did the check for AF_INET6, we can assume that ifa_addr is indeed an sockaddr_in6
        const auto current_addr = reinterpret_cast<const sockaddr_in6*>(current_if->ifa_addr);

        // NOTE (sl): If using loopback device, accept any address. Loopback usually does not have a link local address
        if (interface_name.compare("lo") != 0 and not IN6_IS_ADDR_LINKLOCAL(&(current_addr->sin6_addr))) {
            continue;
        }

        if (interface_name == "auto") {
            logf_info("Found an ipv6 link local address for interface: %s", current_if->ifa_name);
        }

        memcpy(&address, current_addr, sizeof(address));
        found_interface = true;
        break; // Stop the loop if a interface is found
    }

    freeifaddrs(if_list_head);

    // Todo(sl): What to do if interface was not found?
    return found_interface;
}

void set_ipv6_mreq_interface(ipv6_mreq& mreq, unsigned int ifindex) {
    mreq.ipv6mr_interface = ifindex;
}

} // namespace iso15118::io
