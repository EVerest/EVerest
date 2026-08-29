// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/netlink/route_parser.hpp>

#include <cstring>

#include <arpa/inet.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

namespace everest::lib::io::netlink {

namespace {

// <net/if.h> stops at IFF_DYNAMIC; the flag mirroring netif_carrier_ok() is a kernel addition,
// and including <linux/if.h> next to <net/if.h> collides on struct ifreq.
#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP 0x10000
#endif

constexpr char hex_digits[] = "0123456789ABCDEF";

/// Walk a netlink attribute region, copying headers out instead of casting into the buffer so
/// unaligned input cannot trap. \p visit receives (type, payload, payload_length).
template <typename VisitT> void for_each_attribute(std::uint8_t const* base, std::size_t length, VisitT visit) {
    std::size_t offset = 0;
    // Addition rather than subtraction: RTA_ALIGN can push offset past length on the last
    // attribute, and length - offset would then wrap.
    while (offset + sizeof(rtattr) <= length) {
        rtattr attribute{};
        std::memcpy(&attribute, base + offset, sizeof(attribute));
        if (attribute.rta_len < sizeof(rtattr) or attribute.rta_len > length - offset) {
            return;
        }
        auto const payload_length = static_cast<std::size_t>(attribute.rta_len) - RTA_LENGTH(0);
        visit(attribute.rta_type, base + offset + RTA_LENGTH(0), payload_length);
        offset += RTA_ALIGN(attribute.rta_len);
    }
}

std::string to_mac_string(std::uint8_t const* data, std::size_t length) {
    constexpr std::size_t mac_length = 6;
    if (length != mac_length) {
        // Anything but a 48 bit address (InfiniBand, a firewire link layer, an IP-over-X tunnel)
        // cannot satisfy the ev_mac_address pattern, so report none rather than a truncation.
        return {};
    }
    std::string mac;
    mac.reserve(3 * mac_length - 1);
    for (std::size_t i = 0; i < mac_length; ++i) {
        if (i != 0) {
            mac.push_back(':');
        }
        mac.push_back(hex_digits[(data[i] >> 4) & 0x0F]);
        mac.push_back(hex_digits[data[i] & 0x0F]);
    }
    return mac;
}

std::string to_hex_string(std::uint8_t const* data, std::size_t length) {
    std::string text;
    text.reserve(2 * length);
    for (std::size_t i = 0; i < length; ++i) {
        text.push_back(hex_digits[(data[i] >> 4) & 0x0F]);
        text.push_back(hex_digits[data[i] & 0x0F]);
    }
    return text;
}

/// Render NDA_DST. The address is only ever compared and logged, so an unknown family degrades
/// to hex rather than being dropped - a neighbor entry the module cannot name is still an entry
/// whose NUD state matters for liveness.
std::string to_address_string(std::uint8_t family, std::uint8_t const* data, std::size_t length) {
    char text[INET6_ADDRSTRLEN] = {};
    if (family == AF_INET6 and length == sizeof(in6_addr)) {
        in6_addr address{};
        std::memcpy(&address, data, sizeof(address));
        if (::inet_ntop(AF_INET6, &address, text, sizeof(text)) != nullptr) {
            return text;
        }
    }
    if (family == AF_INET and length == sizeof(in_addr)) {
        in_addr address{};
        std::memcpy(&address, data, sizeof(address));
        if (::inet_ntop(AF_INET, &address, text, sizeof(text)) != nullptr) {
            return text;
        }
    }
    return to_hex_string(data, length);
}

void parse_link(nlmsghdr const& header, std::uint8_t const* payload, std::size_t payload_length, parse_result& out) {
    if (payload_length < sizeof(ifinfomsg)) {
        out.truncated = true;
        return;
    }
    ifinfomsg info{};
    std::memcpy(&info, payload, sizeof(info));

    link_report report;
    report.ifindex = info.ifi_index;
    report.lower_up = (info.ifi_flags & IFF_LOWER_UP) != 0;
    report.running = (info.ifi_flags & IFF_RUNNING) != 0;
    report.admin_up = (info.ifi_flags & IFF_UP) != 0;
    report.deleted = header.nlmsg_type == RTM_DELLINK;

    auto const header_length = NLMSG_ALIGN(sizeof(ifinfomsg));
    if (payload_length > header_length) {
        for_each_attribute(payload + header_length, payload_length - header_length,
                           [&report](unsigned short type, std::uint8_t const* data, std::size_t length) {
                               if (type != IFLA_IFNAME) {
                                   return;
                               }
                               // IFLA_IFNAME is NUL terminated; length includes the terminator.
                               auto const* text = reinterpret_cast<char const*>(data);
                               report.name.assign(text, ::strnlen(text, length));
                           });
    }
    out.links.push_back(std::move(report));
}

void parse_neighbor(nlmsghdr const& header, std::uint8_t const* payload, std::size_t payload_length,
                    parse_result& out) {
    if (payload_length < sizeof(ndmsg)) {
        out.truncated = true;
        return;
    }
    ndmsg neighbor{};
    std::memcpy(&neighbor, payload, sizeof(neighbor));

    neighbor_report report;
    report.ifindex = neighbor.ndm_ifindex;
    report.nud_state = neighbor.ndm_state;
    report.deleted = header.nlmsg_type == RTM_DELNEIGH;

    auto const family = neighbor.ndm_family;
    auto const header_length = NLMSG_ALIGN(sizeof(ndmsg));
    if (payload_length > header_length) {
        for_each_attribute(payload + header_length, payload_length - header_length,
                           [&report, family](unsigned short type, std::uint8_t const* data, std::size_t length) {
                               if (type == NDA_DST) {
                                   report.address = to_address_string(family, data, length);
                               } else if (type == NDA_LLADDR) {
                                   report.mac = to_mac_string(data, length);
                               }
                           });
    }
    out.neighbors.push_back(std::move(report));
}

} // namespace

parse_result parse(void const* buffer, std::size_t length) {
    parse_result out;
    auto const* base = static_cast<std::uint8_t const*>(buffer);
    std::size_t offset = 0;

    // See for_each_attribute: NLMSG_ALIGN can push offset past length, so no subtraction here.
    while (offset + sizeof(nlmsghdr) <= length) {
        nlmsghdr header{};
        std::memcpy(&header, base + offset, sizeof(header));
        if (header.nlmsg_len < sizeof(nlmsghdr) or header.nlmsg_len > length - offset) {
            out.truncated = true;
            break;
        }

        auto const* payload = base + offset + NLMSG_HDRLEN;
        auto const payload_length = static_cast<std::size_t>(header.nlmsg_len) - NLMSG_HDRLEN;

        switch (header.nlmsg_type) {
        case RTM_NEWLINK:
        case RTM_DELLINK:
            parse_link(header, payload, payload_length, out);
            break;
        case RTM_NEWNEIGH:
        case RTM_DELNEIGH:
            parse_neighbor(header, payload, payload_length, out);
            break;
        case NLMSG_DONE:
            out.dump_done = true;
            break;
        case NLMSG_ERROR: {
            nlmsgerr error{};
            if (payload_length >= sizeof(error)) {
                std::memcpy(&error, payload, sizeof(error));
                // error == 0 is an ack, not a failure.
                if (error.error != 0) {
                    out.error = error.error;
                }
            } else {
                out.truncated = true;
            }
            break;
        }
        default:
            break;
        }

        offset += NLMSG_ALIGN(header.nlmsg_len);
    }

    return out;
}

bool is_carrier_up(link_report const& report) {
    return not report.deleted and report.lower_up;
}

bool is_neighbor_alive(std::uint16_t nud_state) {
    constexpr std::uint16_t alive = NUD_REACHABLE | NUD_STALE | NUD_DELAY | NUD_PROBE | NUD_PERMANENT;
    return (nud_state & alive) != 0;
}

bool is_neighbor_failed(std::uint16_t nud_state) {
    return (nud_state & NUD_FAILED) != 0;
}

} // namespace everest::lib::io::netlink
