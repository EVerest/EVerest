// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <gtest/gtest.h>

#include <goose/frame.hpp>

#include "hex_to_vec.hpp"

TEST(SecureGooseFrame, real_world_example) {
    const char data_hex[] = "2c52afb6ed180080e11614028100A0C888B80001"
                            "00a200233dac618197801543432f3024"
                            "474f24506f7765725265717565737400"
                            "81022710821543432f3024474f24506f"
                            "7765725265717565737400831543432f"
                            "3024474f24506f776572526571756573"
                            "740084086361bd030000000a85040000"
                            "00018604000000008701008804000000"
                            "008901008a0400000008ab2486020001"
                            "8602ffff860200018602000087040000"
                            "00008704000000008602ffff8602ffff"
                            "ad00207929ec787000393de8800a61b2"
                            "b996f8d7b14bf55eda560562668fc890"
                            "2ba088";
    std::uint8_t data[sizeof(data_hex) / 2];

    hex_to_vec(data_hex, data, sizeof(data));

    goose_ethernet::EthernetFrame ethernet_frame(data, sizeof(data));
    goose::frame::SecureGooseFrame goose_frame(ethernet_frame);

    ASSERT_EQ(goose_frame.destination_mac_address[0], 0x2c);
    ASSERT_EQ(goose_frame.destination_mac_address[1], 0x52);
    ASSERT_EQ(goose_frame.destination_mac_address[2], 0xaf);
    ASSERT_EQ(goose_frame.destination_mac_address[3], 0xb6);
    ASSERT_EQ(goose_frame.destination_mac_address[4], 0xed);
    ASSERT_EQ(goose_frame.destination_mac_address[5], 0x18);

    ASSERT_EQ(goose_frame.source_mac_address[0], 0x00);
    ASSERT_EQ(goose_frame.source_mac_address[1], 0x80);
    ASSERT_EQ(goose_frame.source_mac_address[2], 0xe1);
    ASSERT_EQ(goose_frame.source_mac_address[3], 0x16);
    ASSERT_EQ(goose_frame.source_mac_address[4], 0x14);
    ASSERT_EQ(goose_frame.source_mac_address[5], 0x02);

    ASSERT_EQ(goose_frame.appid[0], 0x00);
    ASSERT_EQ(goose_frame.appid[1], 0x01);

    ASSERT_EQ(goose_frame.vlan_id, 0xC8);
    ASSERT_EQ(goose_frame.priority, 5);

    ASSERT_STREQ(goose_frame.pdu.go_cb_ref, "CC/0$GO$PowerRequest");
    ASSERT_EQ(goose_frame.pdu.time_allowed_to_live, 10000);
    ASSERT_STREQ(goose_frame.pdu.dat_set, "CC/0$GO$PowerRequest");
    ASSERT_STREQ(goose_frame.pdu.go_id, "CC/0$GO$PowerRequest");
    ASSERT_EQ(goose_frame.pdu.timestamp.to_ms(), 1667349763000);
    ASSERT_EQ(goose_frame.pdu.st_num, 1);
    ASSERT_EQ(goose_frame.pdu.sq_num, 0);
    ASSERT_FALSE(goose_frame.pdu.simulation);
    ASSERT_EQ(goose_frame.pdu.conf_rev, 0);
    ASSERT_EQ(goose_frame.pdu.ndsCom, 0);
    ASSERT_EQ(goose_frame.pdu.apdu_entries.size(), 8);

    ASSERT_EQ(goose_frame.pdu.apdu_entries[0].tag, 0x86);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[0].value.size(), 2);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[0].value[0], 0x00);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[0].value[1], 0x01);

    ASSERT_EQ(goose_frame.pdu.apdu_entries[1].tag, 0x86);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[1].value.size(), 2);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[1].value[0], 0xff);
    ASSERT_EQ(goose_frame.pdu.apdu_entries[1].value[1], 0xff);
}

TEST(SecureGooseFrame, real_world_example_invalid_crc) {
    const char data_hex[] = "2c52afb6ed180080e11614028100A0C888B80001"
                            "00a20023dead618197801543432f3024"
                            "474f24506f7765725265717565737400"
                            "81022710821543432f3024474f24506f"
                            "7765725265717565737400831543432f"
                            "3024474f24506f776572526571756573"
                            "740084086361bd030000000a85040000"
                            "00018604000000008701008804000000"
                            "008901008a0400000008ab2486020001"
                            "8602ffff860200018602000087040000"
                            "00008704000000008602ffff8602ffff"
                            "ad00207929ec787000393de8800a61b2"
                            "b996f8d7b14bf55eda560562668fc890"
                            "2ba088";
    std::uint8_t data[sizeof(data_hex) / 2];

    hex_to_vec(data_hex, data, sizeof(data));

    goose_ethernet::EthernetFrame ethernet_frame(data, sizeof(data));
    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(ethernet_frame), std::runtime_error);
}

TEST(SecureGooseFrame, real_world_example_invalid_root_tag) {
    const char data_hex[] = "2c52afb6ed180080e11614028100A0C888B80001"
                            "00a200233dac638197801543432f3024"
                            "474f24506f7765725265717565737400"
                            "81022710821543432f3024474f24506f"
                            "7765725265717565737400831543432f"
                            "3024474f24506f776572526571756573"
                            "740084086361bd030000000a85040000"
                            "00018604000000008701008804000000"
                            "008901008a0400000008ab2486020001"
                            "8602ffff860200018602000087040000"
                            "00008704000000008602ffff8602ffff"
                            "ad00207929ec787000393de8800a61b2"
                            "b996f8d7b14bf55eda560562668fc890"
                            "2ba088";
    std::uint8_t data[sizeof(data_hex) / 2];

    hex_to_vec(data_hex, data, sizeof(data));

    goose_ethernet::EthernetFrame ethernet_frame(data, sizeof(data));
    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(ethernet_frame), std::runtime_error);
}

TEST(SecureGooseFrame, encode_decode) {
    goose::frame::SecureGooseFrame goose_frame;
    goose_frame.appid[0] = 0x00;
    goose_frame.appid[1] = 0x01;
    memset(goose_frame.source_mac_address, 0x00, 6);
    memset(goose_frame.destination_mac_address, 0x00, 6);
    goose_frame.vlan_id = 2;
    goose_frame.priority = 7;

    {
        goose::frame::GoosePDU pdu;
        strcpy(pdu.go_cb_ref, "GO_CB_REF");
        pdu.time_allowed_to_live = 10000;
        strcpy(pdu.dat_set, "DAT_SET");
        strcpy(pdu.go_id, "GO_ID");
        pdu.timestamp = goose::frame::GooseTimestamp::from_ms(1667349763000);
        pdu.st_num = 1;
        pdu.sq_num = 0;
        pdu.simulation = false;
        pdu.conf_rev = 0;
        pdu.ndsCom = 0;
        pdu.apdu_entries.resize(2);
        pdu.apdu_entries[0].tag = 0x86;
        pdu.apdu_entries[0].value = {0, 1};
        pdu.apdu_entries[1].tag = 0x87;
        pdu.apdu_entries[1].value = {0xde, 0xad, 0xbe, 0xef};
        goose_frame.pdu = pdu;
    }

    std::uint8_t key[48] = {0};
    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = i;
    }

    auto serialized = goose_frame.serialize(std::vector<std::uint8_t>(key, key + sizeof(key)));

    auto deserialized = goose::frame::SecureGooseFrame(serialized, std::vector<std::uint8_t>(key, key + sizeof(key)));

    ASSERT_EQ(deserialized.vlan_id, 2);
    ASSERT_EQ(deserialized.priority, 7);

    ASSERT_EQ(deserialized.appid[0], 0x00);
    ASSERT_EQ(deserialized.appid[1], 0x01);
    ASSERT_STREQ(deserialized.pdu.go_cb_ref, goose_frame.pdu.go_cb_ref);
    ASSERT_EQ(deserialized.pdu.time_allowed_to_live, goose_frame.pdu.time_allowed_to_live);
    ASSERT_STREQ(deserialized.pdu.dat_set, goose_frame.pdu.dat_set);
    ASSERT_STREQ(deserialized.pdu.go_id, goose_frame.pdu.go_id);
    ASSERT_EQ(deserialized.pdu.timestamp, goose_frame.pdu.timestamp);
    ASSERT_EQ(deserialized.pdu.st_num, goose_frame.pdu.st_num);
    ASSERT_EQ(deserialized.pdu.sq_num, goose_frame.pdu.sq_num);
    ASSERT_EQ(deserialized.pdu.simulation, goose_frame.pdu.simulation);
    ASSERT_EQ(deserialized.pdu.conf_rev, goose_frame.pdu.conf_rev);
    ASSERT_EQ(deserialized.pdu.ndsCom, goose_frame.pdu.ndsCom);
    ASSERT_EQ(deserialized.pdu.apdu_entries.size(), 2);

    ASSERT_EQ(deserialized.pdu.apdu_entries[0].tag, 0x86);
    ASSERT_EQ(deserialized.pdu.apdu_entries[0].value.size(), 2);
    ASSERT_EQ(deserialized.pdu.apdu_entries[0].value[0], 0);
    ASSERT_EQ(deserialized.pdu.apdu_entries[0].value[1], 1);

    ASSERT_EQ(deserialized.pdu.apdu_entries[1].tag, 0x87);
    ASSERT_EQ(deserialized.pdu.apdu_entries[1].value.size(), 4);
    ASSERT_EQ(deserialized.pdu.apdu_entries[1].value[0], 0xde);
    ASSERT_EQ(deserialized.pdu.apdu_entries[1].value[1], 0xad);
    ASSERT_EQ(deserialized.pdu.apdu_entries[1].value[2], 0xbe);
    ASSERT_EQ(deserialized.pdu.apdu_entries[1].value[3], 0xef);
}

TEST(SecureGooseFrame, deserialize_different_hmac_key) {
    goose::frame::SecureGooseFrame goose_frame;
    goose_frame.appid[0] = 0x00;
    goose_frame.appid[1] = 0x01;
    memset(goose_frame.source_mac_address, 0x00, 6);
    memset(goose_frame.destination_mac_address, 0x00, 6);

    {
        goose::frame::GoosePDU pdu;
        strcpy(pdu.go_cb_ref, "GO_CB_REF");
        pdu.time_allowed_to_live = 10000;
        strcpy(pdu.dat_set, "DAT_SET");
        strcpy(pdu.go_id, "GO_ID");
        pdu.timestamp = goose::frame::GooseTimestamp::from_ms(1667349763000);
        pdu.st_num = 1;
        pdu.sq_num = 0;
        pdu.simulation = false;
        pdu.conf_rev = 0;
        pdu.ndsCom = 0;
        pdu.apdu_entries.resize(2);
        pdu.apdu_entries[0].tag = 0x86;
        pdu.apdu_entries[0].value = {0, 1};
        pdu.apdu_entries[1].tag = 0x87;
        pdu.apdu_entries[1].value = {0, 0, 0, 0};
        goose_frame.pdu = pdu;
    }

    std::uint8_t key[48] = {0};
    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = i;
    }

    auto serialized = goose_frame.serialize(std::vector<std::uint8_t>(key, key + sizeof(key)));

    serialized.payload[serialized.payload.size() - 1]++;
    ASSERT_THROW(goose::frame::SecureGooseFrame frame(serialized, std::vector<std::uint8_t>(key, key + sizeof(key))),
                 std::runtime_error);
}

TEST(SecureGooseFrame, deserialize_edited_payload_throws_invalid_hmac) {
    goose::frame::SecureGooseFrame goose_frame;
    goose_frame.appid[0] = 0x00;
    goose_frame.appid[1] = 0x01;
    memset(goose_frame.source_mac_address, 0x00, 6);
    memset(goose_frame.destination_mac_address, 0x00, 6);

    {
        goose::frame::GoosePDU pdu;
        strcpy(pdu.go_cb_ref, "GO_CB_REF");
        pdu.time_allowed_to_live = 10000;
        strcpy(pdu.dat_set, "DAT_SET");
        strcpy(pdu.go_id, "GO_ID");
        pdu.timestamp = goose::frame::GooseTimestamp::from_ms(1667349763000);
        pdu.st_num = 1;
        pdu.sq_num = 0;
        pdu.simulation = false;
        pdu.conf_rev = 0;
        pdu.ndsCom = 0;
        pdu.apdu_entries.resize(2);
        pdu.apdu_entries[0].tag = 0x86;
        pdu.apdu_entries[0].value = {0, 1};
        pdu.apdu_entries[1].tag = 0x87;
        pdu.apdu_entries[1].value = {0, 0, 0, 0};
        goose_frame.pdu = pdu;
    }

    std::uint8_t key[48] = {0};
    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = i;
    }

    auto serialized = goose_frame.serialize(std::vector<std::uint8_t>(key, key + sizeof(key)));

    serialized.payload[65]++; // edit payload

    ASSERT_THROW(goose::frame::SecureGooseFrame frame(serialized, std::vector<std::uint8_t>(key, key + sizeof(key))),
                 std::runtime_error);
}

namespace {

std::vector<std::uint8_t> test_hmac_key() {
    std::vector<std::uint8_t> key(48);
    for (size_t i = 0; i < key.size(); i++) {
        key[i] = i;
    }
    return key;
}

goose_ethernet::EthernetFrame serialized_secure_frame() {
    goose::frame::SecureGooseFrame goose_frame;
    goose_frame.appid[0] = 0x00;
    goose_frame.appid[1] = 0x01;
    memset(goose_frame.source_mac_address, 0x00, 6);
    memset(goose_frame.destination_mac_address, 0x00, 6);
    goose_frame.vlan_id = 0;
    goose_frame.priority = 5;

    strcpy(goose_frame.pdu.go_cb_ref, "GO_CB_REF");
    goose_frame.pdu.time_allowed_to_live = 10000;
    strcpy(goose_frame.pdu.dat_set, "DAT_SET");
    strcpy(goose_frame.pdu.go_id, "GO_ID");
    goose_frame.pdu.timestamp = goose::frame::GooseTimestamp::from_ms(1667349763000);
    goose_frame.pdu.st_num = 1;
    goose_frame.pdu.sq_num = 0;
    goose_frame.pdu.simulation = false;
    goose_frame.pdu.conf_rev = 0;
    goose_frame.pdu.ndsCom = 0;
    goose_frame.pdu.apdu_entries.resize(1);
    goose_frame.pdu.apdu_entries[0].tag = 0x86;
    goose_frame.pdu.apdu_entries[0].value = {0, 1};

    return goose_frame.serialize(test_hmac_key());
}

std::uint16_t header_length(const goose_ethernet::EthernetFrame& frame) {
    return (frame.payload[2] << 8) | frame.payload[3];
}

// Rewrites the length fields and the CRC16 in reserve2 that covers them, so only the lengths are wrong.
void set_header_lengths(goose_ethernet::EthernetFrame& frame, std::uint16_t length, std::uint8_t extended_length) {
    frame.payload[2] = length >> 8;
    frame.payload[3] = length & 0xFF;
    frame.payload[5] = extended_length;

    const std::uint8_t crc_input[] = {0x88,
                                      0xB8,
                                      frame.payload[0],
                                      frame.payload[1],
                                      frame.payload[2],
                                      frame.payload[3],
                                      frame.payload[4],
                                      frame.payload[5]};
    std::uint16_t crc = 0xFFFF;
    for (auto byte : crc_input) {
        crc ^= byte;
        for (size_t i = 0; i < 8; i++) {
            crc = (crc & 0x0001) ? (crc >> 1) ^ 0xA001 : crc >> 1;
        }
    }
    frame.payload[6] = crc >> 8;
    frame.payload[7] = crc & 0xFF;
}

} // namespace

TEST(SecureGooseFrame, decode_header_too_short_throws) {
    goose_ethernet::EthernetFrame frame;
    frame.ethertype = goose::frame::GOOSE_ETHERTYPE;
    frame.eth_802_1q_tag = 0xA000;
    frame.payload = {0x00, 0x01, 0x00, 0x08};

    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(frame), std::runtime_error);
}

TEST(SecureGooseFrame, decode_length_beyond_payload_throws) {
    auto frame = serialized_secure_frame();
    set_header_lengths(frame, 0xFFFF, 0x23);

    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(frame), std::runtime_error);
}

TEST(SecureGooseFrame, decode_length_below_header_throws) {
    auto frame = serialized_secure_frame();
    set_header_lengths(frame, 7, 0x23);

    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(frame), std::runtime_error);
}

TEST(SecureGooseFrame, decode_extended_length_beyond_payload_throws) {
    auto frame = serialized_secure_frame();
    set_header_lengths(frame, header_length(frame), 0xFF);

    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(frame, test_hmac_key()), std::runtime_error);
    ASSERT_THROW(goose::frame::SecureGooseFrame goose_frame(frame), std::runtime_error);
}

TEST(SecureGooseFrame, decode_with_trailing_padding) {
    auto frame = serialized_secure_frame();
    frame.payload.resize(frame.payload.size() + 16, 0);

    goose::frame::SecureGooseFrame goose_frame(frame, test_hmac_key());
    ASSERT_STREQ(goose_frame.pdu.go_id, "GO_ID");
}
