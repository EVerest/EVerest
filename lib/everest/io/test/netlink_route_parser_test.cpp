// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Tests for netlink/route_parser.cpp against hand-built rtnetlink datagrams. No socket, no
// privileges: the point is the decoding contract, and the one case that matters most is the
// IFF_LOWER_UP vs IFF_RUNNING discrimination (measured, see link_tracker's documented carrier contract) - a TAP device
// that was created carrier-off is still announced once with IFF_RUNNING set, because the operstate behind that flag is
// only corrected by the kernel's linkwatch work about a second later. A carrier watcher keyed on IFF_RUNNING therefore
// sees a spurious link-up on every tap creation.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <arpa/inet.h>
#include <linux/neighbour.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>

#include <everest/io/netlink/route_parser.hpp>

namespace {

using namespace everest::lib::io::netlink;

#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP 0x10000
#endif

/// Accumulates netlink messages the way the kernel packs them into one datagram.
class message_builder {
public:
    /// Append a message with \p type, a fixed-size header struct and a list of attributes.
    template <typename HeaderT>
    message_builder& message(std::uint16_t type, HeaderT const& fixed,
                             std::vector<std::pair<std::uint16_t, std::vector<std::uint8_t>>> const& attributes = {}) {
        auto const start = m_buffer.size();
        m_buffer.resize(start + NLMSG_HDRLEN, 0);
        append_aligned(&fixed, sizeof(fixed), NLMSG_ALIGNTO);
        for (auto const& [attribute_type, payload] : attributes) {
            append_attribute(attribute_type, payload);
        }
        nlmsghdr header{};
        header.nlmsg_len = static_cast<std::uint32_t>(m_buffer.size() - start);
        header.nlmsg_type = type;
        std::memcpy(m_buffer.data() + start, &header, sizeof(header));
        // The kernel aligns the next message; nlmsg_len itself stays unaligned.
        m_buffer.resize(start + NLMSG_ALIGN(header.nlmsg_len), 0);
        return *this;
    }

    /// Append a bare message with no body at all (NLMSG_DONE in a dump).
    message_builder& bare(std::uint16_t type) {
        auto const start = m_buffer.size();
        m_buffer.resize(start + NLMSG_HDRLEN, 0);
        nlmsghdr header{};
        header.nlmsg_len = NLMSG_HDRLEN;
        header.nlmsg_type = type;
        std::memcpy(m_buffer.data() + start, &header, sizeof(header));
        return *this;
    }

    std::vector<std::uint8_t> const& buffer() const {
        return m_buffer;
    }

    std::size_t size() const {
        return m_buffer.size();
    }

    /// Overwrite the nlmsg_len of the message starting at \p offset - the only way to build the
    /// malformed inputs the parser has to survive.
    void set_length(std::size_t offset, std::uint32_t length) {
        nlmsghdr header{};
        std::memcpy(&header, m_buffer.data() + offset, sizeof(header));
        header.nlmsg_len = length;
        std::memcpy(m_buffer.data() + offset, &header, sizeof(header));
    }

private:
    void append_aligned(void const* data, std::size_t length, std::size_t alignment) {
        auto const start = m_buffer.size();
        m_buffer.resize(start + length);
        std::memcpy(m_buffer.data() + start, data, length);
        auto const padded = (m_buffer.size() + alignment - 1) & ~(alignment - 1);
        m_buffer.resize(padded, 0);
    }

    void append_attribute(std::uint16_t type, std::vector<std::uint8_t> const& payload) {
        rtattr attribute{};
        attribute.rta_len = static_cast<unsigned short>(RTA_LENGTH(payload.size()));
        attribute.rta_type = type;
        auto const start = m_buffer.size();
        m_buffer.resize(start + sizeof(attribute));
        std::memcpy(m_buffer.data() + start, &attribute, sizeof(attribute));
        append_aligned(payload.data(), payload.size(), RTA_ALIGNTO);
    }

    std::vector<std::uint8_t> m_buffer;
};

ifinfomsg link_header(int ifindex, unsigned int flags) {
    ifinfomsg info{};
    info.ifi_family = AF_UNSPEC;
    info.ifi_index = ifindex;
    info.ifi_flags = flags;
    return info;
}

ndmsg neighbor_header(int ifindex, std::uint8_t family, std::uint16_t state) {
    ndmsg neighbor{};
    neighbor.ndm_family = family;
    neighbor.ndm_ifindex = ifindex;
    neighbor.ndm_state = state;
    return neighbor;
}

std::vector<std::uint8_t> name_attribute(std::string const& name) {
    std::vector<std::uint8_t> payload(name.begin(), name.end());
    payload.push_back(0);
    return payload;
}

std::vector<std::uint8_t> ipv6_attribute(std::string const& text) {
    in6_addr address{};
    EXPECT_EQ(1, ::inet_pton(AF_INET6, text.c_str(), &address));
    std::vector<std::uint8_t> payload(sizeof(address));
    std::memcpy(payload.data(), &address, sizeof(address));
    return payload;
}

parse_result parse_of(message_builder const& builder) {
    return parse(builder.buffer().data(), builder.size());
}

TEST(NetlinkRouteParser, LinkUpIsReportedWithNameAndCarrier) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_RUNNING | IFF_LOWER_UP),
                    {{IFLA_IFNAME, name_attribute("cb_plc")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.links.size());
    auto const& link = result.links.front();
    EXPECT_EQ(7, link.ifindex);
    EXPECT_EQ("cb_plc", link.name);
    EXPECT_TRUE(link.lower_up);
    EXPECT_TRUE(link.admin_up);
    EXPECT_FALSE(link.deleted);
    EXPECT_TRUE(is_carrier_up(link));
    EXPECT_TRUE(result.neighbors.empty());
    EXPECT_FALSE(result.truncated);
    EXPECT_EQ(0, result.error);
}

// The whole reason this module keys on IFF_LOWER_UP: a carrier-off TAP is announced with
// IFF_RUNNING set, and IFF_RUNNING must never be mistaken for carrier.
TEST(NetlinkRouteParser, RunningWithoutLowerUpIsNotCarrier) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_RUNNING), {{IFLA_IFNAME, name_attribute("cb_plc")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.links.size());
    EXPECT_TRUE(result.links.front().running);
    EXPECT_FALSE(result.links.front().lower_up);
    EXPECT_FALSE(is_carrier_up(result.links.front()));
}

// ... and the inverse combination is honoured too: LOWER_UP without RUNNING is carrier, which
// is what the kernel reports in the window before linkwatch has updated the operstate.
TEST(NetlinkRouteParser, LowerUpWithoutRunningIsCarrier) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_LOWER_UP));

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.links.size());
    EXPECT_FALSE(result.links.front().running);
    EXPECT_TRUE(is_carrier_up(result.links.front()));
    EXPECT_TRUE(result.links.front().name.empty());
}

TEST(NetlinkRouteParser, DelLinkIsNeverCarrierEvenWithLowerUpSet) {
    message_builder builder;
    builder.message(RTM_DELLINK, link_header(7, IFF_UP | IFF_RUNNING | IFF_LOWER_UP),
                    {{IFLA_IFNAME, name_attribute("cb_plc")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.links.size());
    EXPECT_TRUE(result.links.front().deleted);
    EXPECT_TRUE(result.links.front().lower_up);
    EXPECT_FALSE(is_carrier_up(result.links.front()));
}

TEST(NetlinkRouteParser, SeveralMessagesInOneDatagramAreAllDecoded) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(1, IFF_UP | IFF_LOWER_UP), {{IFLA_IFNAME, name_attribute("lo")}})
        .message(RTM_NEWLINK, link_header(7, IFF_UP), {{IFLA_IFNAME, name_attribute("cb_plc")}})
        .bare(NLMSG_DONE);

    auto const result = parse_of(builder);

    ASSERT_EQ(2u, result.links.size());
    EXPECT_EQ("lo", result.links[0].name);
    EXPECT_TRUE(is_carrier_up(result.links[0]));
    EXPECT_EQ("cb_plc", result.links[1].name);
    EXPECT_FALSE(is_carrier_up(result.links[1]));
    EXPECT_TRUE(result.dump_done);
}

TEST(NetlinkRouteParser, ReachableNeighborCarriesAddressAndUpperCaseMac) {
    message_builder builder;
    builder.message(
        RTM_NEWNEIGH, neighbor_header(7, AF_INET6, NUD_REACHABLE),
        {{NDA_DST, ipv6_attribute("fe80::1e2b:3c4d:5e6f:7a8b")}, {NDA_LLADDR, {0x0a, 0x1b, 0x2c, 0xd3, 0xe4, 0xf5}}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.neighbors.size());
    auto const& neighbor = result.neighbors.front();
    EXPECT_EQ(7, neighbor.ifindex);
    EXPECT_EQ("fe80::1e2b:3c4d:5e6f:7a8b", neighbor.address);
    EXPECT_EQ("0A:1B:2C:D3:E4:F5", neighbor.mac);
    EXPECT_TRUE(is_neighbor_alive(neighbor.nud_state));
    EXPECT_FALSE(is_neighbor_failed(neighbor.nud_state));
    EXPECT_FALSE(neighbor.deleted);
}

TEST(NetlinkRouteParser, FailedNeighborIsNeitherAliveNorDeleted) {
    message_builder builder;
    builder.message(RTM_NEWNEIGH, neighbor_header(7, AF_INET6, NUD_FAILED), {{NDA_DST, ipv6_attribute("fe80::1")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.neighbors.size());
    EXPECT_TRUE(is_neighbor_failed(result.neighbors.front().nud_state));
    EXPECT_FALSE(is_neighbor_alive(result.neighbors.front().nud_state));
    EXPECT_FALSE(result.neighbors.front().deleted);
    EXPECT_TRUE(result.neighbors.front().mac.empty()) << "a failed entry has no link layer address";
}

TEST(NetlinkRouteParser, DelNeighIsMarkedDeleted) {
    message_builder builder;
    builder.message(RTM_DELNEIGH, neighbor_header(7, AF_INET6, NUD_REACHABLE), {{NDA_DST, ipv6_attribute("fe80::1")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.neighbors.size());
    EXPECT_TRUE(result.neighbors.front().deleted);
}

// STALE means "was reachable, not re-verified" - the kernel only probes when something sends.
// Treating it as dead would tear down an idle but perfectly healthy session.
TEST(NetlinkRouteParser, StaleDelayAndProbeCountAsAlive) {
    for (std::uint16_t state : {NUD_STALE, NUD_DELAY, NUD_PROBE, NUD_PERMANENT, NUD_REACHABLE}) {
        EXPECT_TRUE(is_neighbor_alive(state)) << "state " << state;
        EXPECT_FALSE(is_neighbor_failed(state)) << "state " << state;
    }
    for (std::uint16_t state : {NUD_FAILED, NUD_NONE, NUD_INCOMPLETE}) {
        EXPECT_FALSE(is_neighbor_alive(state)) << "state " << state;
    }
    EXPECT_TRUE(is_neighbor_failed(NUD_FAILED));
}

TEST(NetlinkRouteParser, LinkLayerAddressOfUnexpectedLengthYieldsNoMac) {
    message_builder builder;
    builder.message(RTM_NEWNEIGH, neighbor_header(7, AF_INET6, NUD_REACHABLE), {{NDA_LLADDR, {0x01, 0x02, 0x03}}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.neighbors.size());
    EXPECT_TRUE(result.neighbors.front().mac.empty());
}

TEST(NetlinkRouteParser, IPv4NeighborAddressIsRendered) {
    in_addr address{};
    ASSERT_EQ(1, ::inet_pton(AF_INET, "192.0.2.9", &address));
    std::vector<std::uint8_t> payload(sizeof(address));
    std::memcpy(payload.data(), &address, sizeof(address));

    message_builder builder;
    builder.message(RTM_NEWNEIGH, neighbor_header(7, AF_INET, NUD_REACHABLE), {{NDA_DST, payload}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.neighbors.size());
    EXPECT_EQ("192.0.2.9", result.neighbors.front().address);
}

TEST(NetlinkRouteParser, ErrorMessageIsReportedAndAckIsNot) {
    nlmsgerr failure{};
    failure.error = -ENODEV;
    message_builder failing;
    failing.message(NLMSG_ERROR, failure);
    EXPECT_EQ(-ENODEV, parse_of(failing).error);

    nlmsgerr ack{};
    message_builder acked;
    acked.message(NLMSG_ERROR, ack);
    EXPECT_EQ(0, parse_of(acked).error);
}

TEST(NetlinkRouteParser, UnknownMessageTypesAreSkipped) {
    message_builder builder;
    builder.message(RTM_NEWROUTE, link_header(7, IFF_UP | IFF_LOWER_UP))
        .message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_LOWER_UP), {{IFLA_IFNAME, name_attribute("cb_plc")}});

    auto const result = parse_of(builder);

    ASSERT_EQ(1u, result.links.size());
    EXPECT_EQ("cb_plc", result.links.front().name);
}

TEST(NetlinkRouteParser, MessageClaimingMoreThanTheBufferHoldsIsTruncatedAfterEarlierOnes) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(1, IFF_UP | IFF_LOWER_UP), {{IFLA_IFNAME, name_attribute("lo")}});
    auto const second_offset = builder.size();
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP), {{IFLA_IFNAME, name_attribute("cb_plc")}});
    builder.set_length(second_offset, static_cast<std::uint32_t>(builder.size() * 4));

    auto const result = parse_of(builder);

    EXPECT_TRUE(result.truncated);
    ASSERT_EQ(1u, result.links.size()) << "the message decoded before the bad one must survive";
    EXPECT_EQ("lo", result.links.front().name);
}

TEST(NetlinkRouteParser, ShortHeaderLengthDoesNotLoopForever) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_LOWER_UP));
    builder.set_length(0, 4);

    auto const result = parse_of(builder);

    EXPECT_TRUE(result.truncated);
    EXPECT_TRUE(result.links.empty());
}

TEST(NetlinkRouteParser, LinkMessageWithoutRoomForIfinfomsgIsTruncated) {
    message_builder builder;
    builder.bare(RTM_NEWLINK);

    auto const result = parse_of(builder);

    EXPECT_TRUE(result.truncated);
    EXPECT_TRUE(result.links.empty());
}

TEST(NetlinkRouteParser, OversizedAttributeIsIgnoredWithoutReadingPastTheBuffer) {
    message_builder builder;
    builder.message(RTM_NEWLINK, link_header(7, IFF_UP | IFF_LOWER_UP), {{IFLA_IFNAME, name_attribute("cb_plc")}});
    auto buffer = builder.buffer();

    // Corrupt the rta_len of the single attribute so it claims to run past the message.
    auto const attribute_offset = NLMSG_HDRLEN + NLMSG_ALIGN(sizeof(ifinfomsg));
    rtattr attribute{};
    std::memcpy(&attribute, buffer.data() + attribute_offset, sizeof(attribute));
    attribute.rta_len = static_cast<unsigned short>(buffer.size() * 2);
    std::memcpy(buffer.data() + attribute_offset, &attribute, sizeof(attribute));

    auto const result = parse(buffer.data(), buffer.size());

    ASSERT_EQ(1u, result.links.size());
    EXPECT_TRUE(result.links.front().name.empty());
    EXPECT_TRUE(is_carrier_up(result.links.front()));
}

TEST(NetlinkRouteParser, EmptyAndTinyBuffersAreHandled) {
    EXPECT_TRUE(parse(nullptr, 0).links.empty());

    std::vector<std::uint8_t> tiny(3, 0xff);
    auto const result = parse(tiny.data(), tiny.size());
    EXPECT_TRUE(result.links.empty());
    EXPECT_FALSE(result.truncated) << "less than a header is not a truncated message, just nothing";
}

} // namespace
