// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest

#include <everest/io/mdns/mdns.hpp>

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using everest::lib::io::mdns::create_mdns_query;
using everest::lib::io::mdns::parse_mdns_packet;

namespace {

void append_name(std::vector<std::uint8_t>& packet, std::initializer_list<char const*> labels) {
    for (auto const* label : labels) {
        std::string const str(label);
        packet.push_back(static_cast<std::uint8_t>(str.size()));
        packet.insert(packet.end(), str.begin(), str.end());
    }
    packet.push_back(0);
}

void append_pointer(std::vector<std::uint8_t>& packet, std::size_t target) {
    packet.push_back(static_cast<std::uint8_t>(0xC0 | (target >> 8)));
    packet.push_back(static_cast<std::uint8_t>(target));
}

void append_record_header(std::vector<std::uint8_t>& packet, std::uint16_t type, std::uint16_t rdlen) {
    packet.push_back(static_cast<std::uint8_t>(type >> 8));
    packet.push_back(static_cast<std::uint8_t>(type & 0xFF));
    packet.push_back(0x00); // class IN
    packet.push_back(0x01);
    packet.insert(packet.end(), {0x00, 0x00, 0x00, 0x78}); // TTL 120
    packet.push_back(static_cast<std::uint8_t>(rdlen >> 8));
    packet.push_back(static_cast<std::uint8_t>(rdlen & 0xFF));
}

// Minimal mDNS response with a single record of the given type, declaring rdlen bytes of rdata but
// carrying only rdata.size() of them (equal unless a test wants a truncated packet).
std::vector<std::uint8_t> make_record_packet(std::uint16_t type, std::vector<std::uint8_t> const& rdata,
                                             std::uint16_t rdlen) {
    std::vector<std::uint8_t> packet = {0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
    append_name(packet, {"cb001", "local"});
    append_record_header(packet, type, rdlen);
    packet.insert(packet.end(), rdata.begin(), rdata.end());
    return packet;
}

void append_labels(std::vector<std::uint8_t>& packet, std::vector<std::uint8_t> const& lengths) {
    for (auto const len : lengths) {
        packet.push_back(len);
        packet.insert(packet.end(), len, 'a');
    }
}

std::vector<std::uint8_t> make_compression_chain_packet(int hops) {
    std::vector<std::uint8_t> packet = {0, 0, 0x84, 0, 0, 0, 0, 2, 0, 0, 0, 0};
    packet.push_back(0);
    int const rdlen = 2 * (hops - 1);
    packet.insert(packet.end(),
                  {0, 0x10, 0, 1, 0, 0, 0, 0, static_cast<std::uint8_t>(rdlen >> 8), static_cast<std::uint8_t>(rdlen)});

    for (int i = 0; i < hops; ++i) {
        int const target = i == 0 ? 12 : static_cast<int>(packet.size()) - 2;
        packet.push_back(static_cast<std::uint8_t>(0xC0 | (target >> 8)));
        packet.push_back(static_cast<std::uint8_t>(target));
    }
    packet.insert(packet.end(), {0, 0xFF, 0, 1, 0, 0, 0, 0, 0, 0});
    return packet;
}

} // namespace

TEST(mdns_test, parse_compressed_service_response) {
    std::vector<std::uint8_t> packet = {0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00};

    auto const service_offset = packet.size();
    append_name(packet, {"_chargebridge", "_udp", "local"});
    auto const local_offset = packet.size() - 7;
    append_record_header(packet, 0x0C, 8);
    auto const instance_offset = packet.size();
    append_name(packet, {"cb001"});
    packet.pop_back();
    append_pointer(packet, service_offset);

    append_pointer(packet, instance_offset);
    append_record_header(packet, 0x21, 14);
    packet.insert(packet.end(), {0x00, 0x00, 0x00, 0x00, 0x17, 0x70}); // priority, weight, port 6000
    auto const host_offset = packet.size();
    append_name(packet, {"cb001"});
    packet.pop_back();
    append_pointer(packet, local_offset);

    std::string const txt = "board_type=CB-CCS-EVSE-LU";
    append_pointer(packet, instance_offset);
    append_record_header(packet, 0x10, static_cast<std::uint16_t>(txt.size() + 1));
    packet.push_back(static_cast<std::uint8_t>(txt.size()));
    packet.insert(packet.end(), txt.begin(), txt.end());

    append_pointer(packet, host_offset);
    append_record_header(packet, 0x01, 4);
    packet.insert(packet.end(), {10, 0, 0, 5});

    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->service_instance, "cb001._chargebridge._udp.local");
    EXPECT_EQ(parsed->hostname, "cb001.local");
    EXPECT_EQ(parsed->port, 6000);
    EXPECT_EQ(parsed->ip, "10.0.0.5");
    ASSERT_EQ(parsed->txt.count("board_type"), 1u);
    EXPECT_EQ(parsed->txt.at("board_type"), "CB-CCS-EVSE-LU");
}

TEST(mdns_test, parse_ignores_query) {
    EXPECT_FALSE(parse_mdns_packet(create_mdns_query("_chargebridge._udp.local")).has_value());
}

TEST(mdns_test, parse_rejects_truncated_rdata) {
    std::vector<std::uint8_t> const rdata(4, 0xfd);
    EXPECT_FALSE(parse_mdns_packet(make_record_packet(0x10, rdata, 16)).has_value());
    EXPECT_FALSE(parse_mdns_packet(make_record_packet(0x01, {10, 0}, 4)).has_value());
    EXPECT_FALSE(parse_mdns_packet(make_record_packet(0x21, {0, 0, 0, 0}, 8)).has_value());
}

TEST(mdns_test, parse_rejects_truncated_record_header) {
    auto packet = make_record_packet(0x01, {10, 0, 0, 5}, 4);
    packet.resize(packet.size() - 6);
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_self_referencing_compression_pointer) {
    std::vector<std::uint8_t> question(18, 0);
    question[2] = 0x80;
    question[5] = 1;
    question[12] = 0xC0;
    question[13] = 12;
    EXPECT_FALSE(parse_mdns_packet(question).has_value());

    std::vector<std::uint8_t> record(14, 0);
    record[2] = 0x80;
    record[7] = 1;
    record[12] = 0xC0;
    record[13] = 12;
    EXPECT_FALSE(parse_mdns_packet(record).has_value());
}

TEST(mdns_test, parse_rejects_compression_pointer_cycles_in_record_data) {
    auto ptr = make_record_packet(0x0C, {0xC0, 0}, 2);
    ptr.back() = static_cast<std::uint8_t>(ptr.size() - 2);
    EXPECT_FALSE(parse_mdns_packet(ptr).has_value());

    std::vector<std::uint8_t> srv_data(8, 0);
    srv_data[6] = 0xC0;
    auto srv = make_record_packet(0x21, srv_data, 8);
    srv.back() = static_cast<std::uint8_t>(srv.size() - 2);
    EXPECT_FALSE(parse_mdns_packet(srv).has_value());
}

TEST(mdns_test, parse_rejects_truncated_compression_pointer) {
    std::vector<std::uint8_t> packet(13, 0);
    packet[2] = 0x80;
    packet[5] = 1;
    packet[12] = 0xC0;
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_truncated_question_fields) {
    std::vector<std::uint8_t> packet(14, 0);
    packet[2] = 0x80;
    packet[5] = 1;
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_name_extending_past_record_data) {
    auto packet = make_record_packet(0x0C, {1, 'x'}, 2);
    packet[7] = 2;
    // The second record supplies a terminator beyond the PTR record's data.
    packet.insert(packet.end(), 11, 0);
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_forward_compression_pointer) {
    std::vector<std::uint8_t> packet(18, 0);
    packet[2] = 0x80;
    packet[5] = 1;
    packet[12] = 0xC0;
    packet[13] = 14;
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_compression_pointer_into_header) {
    std::vector<std::uint8_t> packet(18, 0);
    packet[2] = 0x80;
    packet[5] = 1;
    packet[12] = 0xC0;
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_rejects_backward_pointer_cycle_through_label) {
    std::vector<std::uint8_t> packet(20, 0);
    packet[2] = 0x80;
    packet[5] = 1;
    packet[12] = 1;
    packet[13] = 'x';
    packet[14] = 0xC0;
    packet[15] = 12;
    EXPECT_FALSE(parse_mdns_packet(packet).has_value());
}

TEST(mdns_test, parse_limits_name_length) {
    auto make_packet = [](std::uint8_t last_label_length) {
        std::vector<std::uint8_t> packet = {0, 0, 0x84, 0, 0, 1, 0, 0, 0, 0, 0, 0};
        append_labels(packet, {63, 63, 63, last_label_length});
        packet.push_back(0);
        packet.insert(packet.end(), {0, 0x0C, 0, 1});
        return packet;
    };
    EXPECT_TRUE(parse_mdns_packet(make_packet(61)).has_value());
    EXPECT_FALSE(parse_mdns_packet(make_packet(62)).has_value());
}

TEST(mdns_test, parse_limits_name_length_across_compression_pointer) {
    auto make_packet = [](std::uint8_t first_label_length) {
        std::vector<std::uint8_t> packet = {0, 0, 0x84, 0, 0, 2, 0, 0, 0, 0, 0, 0};
        append_labels(packet, {63, 63, 63});
        packet.push_back(0);
        packet.insert(packet.end(), {0, 0x0C, 0, 1});
        append_labels(packet, {first_label_length});
        packet.insert(packet.end(), {0xC0, 12, 0, 0x0C, 0, 1});
        return packet;
    };
    EXPECT_TRUE(parse_mdns_packet(make_packet(61)).has_value());
    EXPECT_FALSE(parse_mdns_packet(make_packet(62)).has_value());
}

TEST(mdns_test, parse_limits_compression_pointer_hops) {
    EXPECT_TRUE(parse_mdns_packet(make_compression_chain_packet(128)).has_value());
    EXPECT_FALSE(parse_mdns_packet(make_compression_chain_packet(129)).has_value());
}

TEST(mdns_test, parse_accepts_compressed_ptr_name) {
    std::vector<std::uint8_t> packet = {0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0};
    append_name(packet, {"_x", "_udp", "local"});
    packet.insert(packet.end(), {0, 0x0C, 0, 1, 0, 0, 0, 120, 0, 7, 4, 'i', 'n', 's', 't', 0xC0, 12});
    auto const parsed = parse_mdns_packet(packet);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->service_instance, "inst._x._udp.local");
}
