// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/mdns/mdns.hpp>

#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

using everest::lib::io::mdns::create_mdns_query;
using everest::lib::io::mdns::create_mdns_response;
using everest::lib::io::mdns::is_link_local_v6;
using everest::lib::io::mdns::is_query_for;
using everest::lib::io::mdns::mDNS_discovery;
using everest::lib::io::mdns::mDNS_registry;
using everest::lib::io::mdns::parse_mdns_packet;
using everest::lib::io::mdns::select_address;

namespace {

mDNS_discovery make_service() {
    mDNS_discovery svc;
    svc.hostname = "cb001";
    svc.service_instance = "cb001._chargebridge._udp.local";
    svc.port = 6000;
    svc.ip = "10.0.0.5";
    svc.add_string("board_type", "CB-CCS-EVSE-LU");
    return svc;
}

std::uint16_t answer_count(std::vector<std::uint8_t> const& packet) {
    return static_cast<std::uint16_t>((packet[6] << 8) | packet[7]);
}

void append_name(std::vector<std::uint8_t>& packet, std::initializer_list<char const*> labels) {
    for (auto const* label : labels) {
        std::string const str(label);
        packet.push_back(static_cast<std::uint8_t>(str.size()));
        packet.insert(packet.end(), str.begin(), str.end());
    }
    packet.push_back(0);
}

// Minimal mDNS response with a single record of the given type, declaring rdlen bytes of rdata but
// carrying only rdata.size() of them (equal unless a test wants a truncated packet).
std::vector<std::uint8_t> make_record_packet(std::uint16_t type, std::vector<std::uint8_t> const& rdata,
                                             std::uint16_t rdlen) {
    std::vector<std::uint8_t> packet = {0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    append_name(packet, {"cb001", "local"});
    packet.push_back(static_cast<std::uint8_t>(type >> 8));
    packet.push_back(static_cast<std::uint8_t>(type & 0xFF));
    packet.push_back(0x00); // class IN
    packet.push_back(0x01);
    packet.insert(packet.end(), {0x00, 0x00, 0x00, 0x78}); // TTL 120
    packet.push_back(static_cast<std::uint8_t>(rdlen >> 8));
    packet.push_back(static_cast<std::uint8_t>(rdlen & 0xFF));
    packet.insert(packet.end(), rdata.begin(), rdata.end());
    return packet;
}

std::vector<std::uint8_t> make_aaaa_packet(std::vector<std::uint8_t> const& rdata) {
    return make_record_packet(0x1C, rdata, static_cast<std::uint16_t>(rdata.size()));
}

} // namespace

TEST(mdns_test, response_roundtrip_v4_only) {
    auto const svc = make_service();
    auto const packet = create_mdns_response(svc, "_chargebridge._udp");

    // v4-only responses keep the pre-IPv6 shape: 5 answer RRs, no AAAA
    EXPECT_EQ(answer_count(packet), 5);

    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->ip, "10.0.0.5");
    EXPECT_TRUE(parsed->ipv6.empty());
    EXPECT_EQ(parsed->port, 6000);
    EXPECT_EQ(parsed->hostname, "cb001.local");
    EXPECT_EQ(parsed->service_instance, "cb001._chargebridge._udp.local");
    ASSERT_EQ(parsed->txt.count("board_type"), 1u);
    EXPECT_EQ(parsed->txt.at("board_type"), "CB-CCS-EVSE-LU");
}

TEST(mdns_test, response_roundtrip_v4_and_v6) {
    auto svc = make_service();
    svc.ipv6 = "fd00::1";
    auto const packet = create_mdns_response(svc, "_chargebridge._udp");

    EXPECT_EQ(answer_count(packet), 6);

    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->ip, "10.0.0.5");
    EXPECT_EQ(parsed->ipv6, "fd00::1");
    EXPECT_EQ(parsed->port, 6000);
    EXPECT_EQ(parsed->hostname, "cb001.local");
}

TEST(mdns_test, response_roundtrip_v6_only) {
    auto svc = make_service();
    svc.ip = "";
    svc.ipv6 = "fd00::1";
    auto const packet = create_mdns_response(svc, "_chargebridge._udp");

    // no A record for an IPv6-only service - a 0.0.0.0 filler would win select_address()
    EXPECT_EQ(answer_count(packet), 5);

    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->ip.empty());
    EXPECT_EQ(parsed->ipv6, "fd00::1");
    EXPECT_EQ(select_address(*parsed), "fd00::1");
    EXPECT_EQ(parsed->port, 6000);
    EXPECT_EQ(parsed->hostname, "cb001.local");
}

TEST(mdns_test, response_ignores_invalid_ipv6) {
    auto svc = make_service();
    // %scope and brackets are not valid on the announce side and must be skipped
    svc.ipv6 = "fe80::1%eth0";
    auto const packet = create_mdns_response(svc, "_chargebridge._udp");
    EXPECT_EQ(answer_count(packet), 5);
    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->ipv6.empty());
}

TEST(mdns_test, parse_aaaa_record) {
    std::vector<std::uint8_t> rdata(16, 0);
    rdata[0] = 0xfd;
    rdata[15] = 0x01;
    auto const parsed = parse_mdns_packet(make_aaaa_packet(rdata));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->ipv6, "fd00::1");
    EXPECT_TRUE(parsed->ip.empty());
}

TEST(mdns_test, parse_aaaa_record_malformed_rdlen_ignored) {
    std::vector<std::uint8_t> rdata(8, 0xfd);
    auto const parsed = parse_mdns_packet(make_aaaa_packet(rdata));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->ipv6.empty());
}

TEST(mdns_test, parse_truncated_rdata_ignored) {
    // The record header declares 16 bytes of rdata but the packet carries only 4;
    // the parser must not read past the buffer (would leak adjacent heap into ipv6).
    std::vector<std::uint8_t> const rdata(4, 0xfd);
    auto const parsed = parse_mdns_packet(make_record_packet(0x1C, rdata, 16));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->ipv6.empty());
}

TEST(mdns_test, parse_legacy_zero_a_record_treated_as_absent) {
    // Older announcers fill the A record with 0.0.0.0 when they have no IPv4 address;
    // it must not shadow an AAAA-provided address in select_address().
    std::vector<std::uint8_t> const rdata(4, 0x00);
    auto const parsed = parse_mdns_packet(make_record_packet(0x01, rdata, 4));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_TRUE(parsed->ip.empty());
}

TEST(mdns_test, registry_merges_a_and_aaaa) {
    mDNS_registry registry;

    mDNS_discovery v4_update;
    v4_update.service_instance = "cb001._chargebridge._udp.local";
    v4_update.ip = "10.0.0.5";

    mDNS_discovery v6_update;
    v6_update.service_instance = "cb001._chargebridge._udp.local";
    v6_update.ipv6 = "fe80::1%eth0";

    EXPECT_TRUE(registry.update(v4_update));
    EXPECT_TRUE(registry.update(v6_update));
    // identical updates carry nothing new
    EXPECT_FALSE(registry.update(v4_update));
    EXPECT_FALSE(registry.update(v6_update));

    auto const& data = registry.get();
    ASSERT_EQ(data.count("cb001._chargebridge._udp.local"), 1u);
    auto const& merged = data.at("cb001._chargebridge._udp.local");
    EXPECT_EQ(merged.ip, "10.0.0.5");
    EXPECT_EQ(merged.ipv6, "fe80::1%eth0");
}

TEST(mdns_test, select_address_prefers_v4) {
    mDNS_discovery info;
    EXPECT_TRUE(select_address(info).empty());

    info.ipv6 = "fd00::1";
    EXPECT_EQ(select_address(info), "fd00::1");

    info.ip = "10.0.0.5";
    EXPECT_EQ(select_address(info), "10.0.0.5");
}

TEST(mdns_test, is_link_local_v6_classification) {
    EXPECT_TRUE(is_link_local_v6("fe80::1"));
    EXPECT_TRUE(is_link_local_v6("fe80::1%eth0"));
    EXPECT_FALSE(is_link_local_v6("fd00::1"));
    EXPECT_FALSE(is_link_local_v6("2001:db8::1"));
    EXPECT_FALSE(is_link_local_v6("10.0.0.1"));
    EXPECT_FALSE(is_link_local_v6("not-an-address"));
    EXPECT_FALSE(is_link_local_v6(""));
}

TEST(mdns_test, query_matches_is_query_for) {
    // Note the convention: the query side passes the name WITH ".local",
    // is_query_for expects the service type WITHOUT it and appends ".local".
    auto const query = create_mdns_query("_chargebridge._udp.local");
    EXPECT_TRUE(is_query_for(query, "_chargebridge._udp"));
    EXPECT_FALSE(is_query_for(query, "_other._udp"));

    // responses are never queries
    auto const response = create_mdns_response(make_service(), "_chargebridge._udp");
    EXPECT_FALSE(is_query_for(response, "_chargebridge._udp"));
}
