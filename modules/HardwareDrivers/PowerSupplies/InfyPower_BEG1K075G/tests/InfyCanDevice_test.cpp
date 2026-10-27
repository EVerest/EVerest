// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#include "InfyCanDevice.hpp"

#include <gtest/gtest.h>
#include <linux/can.h>

#include <vector>

namespace {

using namespace can_packet_acdc;

// 50 ms ticks, 1 s receive timeout
constexpr unsigned RX_TIMEOUT_TICKS = 20;

struct Sent {
    uint32_t can_id;
    std::vector<uint8_t> payload;

    bool is_write() const {
        return command_number_from_can_id(can_id) == CMD_WRITE;
    }
    bool is_read() const {
        return command_number_from_can_id(can_id) == CMD_READ;
    }
    // The first two payload bytes name the register
    uint16_t type() const {
        return static_cast<uint16_t>(payload.at(0) << 8 | payload.at(1));
    }
    uint16_t last_word() const {
        return static_cast<uint16_t>(payload.at(6) << 8 | payload.at(7));
    }
};

constexpr uint16_t REG_DC_VOLTAGE = 0x1001;
constexpr uint16_t REG_ON_OFF = 0x1110;
constexpr uint16_t REG_WALK_IN = 0x1122;
constexpr uint16_t REG_OUTPUT_MODE = 0x1126;
constexpr uint16_t REG_WORKING_MODE = 0x2110;
constexpr uint16_t REG_POWER_FACTOR = 0x2105;
constexpr uint16_t ON = 0xA0;
constexpr uint16_t OFF = 0xA1;

// The socket is replaced by a recording sink that can drop frames like a busy bus does.
class TestDevice : public InfyCanDevice {
public:
    using InfyCanDevice::link_handler;
    using InfyCanDevice::rx_handler;
    using InfyCanDevice::tick_handler;

    std::vector<Sent> sent;
    bool accept{true};
    std::vector<bool> connection_reports;

    TestDevice() {
        signal_connection.connect([this](bool connected) { connection_reports.push_back(connected); });
    }

    void ticks(unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            tick_handler();
        }
    }

    void reply(uint8_t command) {
        auto id = encode_can_id(ADDR_MODULE, ADDR_CONTROLLER, command, 0x0A, 0) | CAN_EFF_FLAG;
        rx_handler(id, SystemDCVoltage(400.));
    }
    void read_reply() {
        reply(CMD_READ);
    }
    void write_ack() {
        reply(CMD_WRITE);
    }

    // Link up and first poll answered
    void module_answers() {
        link_handler(true);
        tick_handler();
        read_reply();
        sent.clear();
    }

    // Drains the resync and the poll that follows it
    void connect() {
        module_answers();
        ticks(RX_TIMEOUT_TICKS);
        read_reply();
        sent.clear();
        connection_reports.clear();
    }

    std::vector<Sent> writes() const {
        std::vector<Sent> result;
        for (auto const& frame : sent) {
            if (frame.is_write()) {
                result.push_back(frame);
            }
        }
        return result;
    }

protected:
    bool tx_frame(uint32_t can_id, const std::vector<uint8_t>& payload) override {
        if (not accept) {
            return false;
        }
        sent.push_back({can_id, payload});
        return true;
    }
};

TEST(InfyCanDevice, no_reply_reports_not_connected_once) {
    TestDevice dev;
    dev.link_handler(true);
    dev.ticks(RX_TIMEOUT_TICKS + 5);

    EXPECT_EQ(dev.connection_reports, std::vector<bool>{false});
    EXPECT_FALSE(dev.sent.empty());
    EXPECT_TRUE(dev.writes().empty()) << "only polls go out while the module is not connected";
}

TEST(InfyCanDevice, resync_precedes_connection_and_ends_with_on_off) {
    TestDevice dev;
    dev.switch_on_off(true);
    dev.module_answers();

    dev.ticks(3);
    ASSERT_EQ(dev.sent.size(), 3u);
    EXPECT_EQ(dev.sent[0].type(), REG_WORKING_MODE);
    EXPECT_EQ(dev.sent[1].type(), REG_WALK_IN);
    EXPECT_EQ(dev.sent[2].type(), REG_ON_OFF);
    EXPECT_EQ(dev.sent[2].last_word(), ON);
    EXPECT_TRUE(dev.connection_reports.empty()) << "not connected before the module answers a poll";

    dev.tick_handler();
    ASSERT_EQ(dev.sent.size(), 4u);
    EXPECT_TRUE(dev.sent[3].is_read());
    dev.read_reply();
    EXPECT_EQ(dev.connection_reports, std::vector<bool>{true});
}

TEST(InfyCanDevice, resync_sends_stored_settings_and_setpoints) {
    TestDevice dev;
    dev.adjust_power_factor(1.0);
    dev.set_output_mode(InfyCanDevice::OutputMode::Automatic);
    dev.set_voltage_current(400., 10.);
    dev.module_answers();

    dev.ticks(7);
    ASSERT_EQ(dev.sent.size(), 7u);
    EXPECT_EQ(dev.sent[0].type(), REG_POWER_FACTOR);
    EXPECT_EQ(dev.sent[1].type(), REG_OUTPUT_MODE);
    EXPECT_EQ(dev.sent[2].type(), REG_WORKING_MODE);
    EXPECT_EQ(dev.sent[3].type(), REG_WALK_IN);
    EXPECT_EQ(dev.sent[4].type(), 0x1001);
    EXPECT_EQ(dev.sent[5].type(), 0x1002);
    EXPECT_EQ(dev.sent[6].type(), REG_ON_OFF);
    for (auto const& frame : dev.sent) {
        EXPECT_TRUE(frame.is_write());
    }
}

TEST(InfyCanDevice, resync_reads_output_state_when_its_frame_is_sent) {
    TestDevice dev;
    dev.switch_on_off(true);
    dev.module_answers();

    dev.ticks(2);
    dev.switch_on_off(false);
    dev.tick_handler();

    ASSERT_EQ(dev.sent.size(), 3u);
    EXPECT_EQ(dev.sent[2].type(), REG_ON_OFF);
    EXPECT_EQ(dev.sent[2].last_word(), OFF);
}

TEST(InfyCanDevice, dropped_resync_step_is_retried) {
    TestDevice dev;
    dev.module_answers();

    dev.accept = false;
    dev.ticks(5);
    EXPECT_TRUE(dev.sent.empty());

    dev.accept = true;
    dev.ticks(3);
    ASSERT_EQ(dev.sent.size(), 3u);
    EXPECT_EQ(dev.sent[0].type(), REG_WORKING_MODE);
    EXPECT_EQ(dev.sent[2].type(), REG_ON_OFF);
}

TEST(InfyCanDevice, module_lost_during_resync_is_reported_not_connected) {
    TestDevice dev;
    dev.module_answers();

    dev.accept = false;
    dev.ticks(3 + RX_TIMEOUT_TICKS + 1);

    EXPECT_EQ(dev.connection_reports, std::vector<bool>{false});
}

TEST(InfyCanDevice, long_resync_completes_without_acks) {
    TestDevice dev;
    for (uint8_t reg = 0x20; reg < 0x20 + RX_TIMEOUT_TICKS; ++reg) {
        dev.set_generic_setting(0x21, reg, 1);
    }
    dev.module_answers();

    dev.ticks(RX_TIMEOUT_TICKS + 3);
    EXPECT_EQ(dev.writes().size(), RX_TIMEOUT_TICKS + 3);
    EXPECT_TRUE(dev.connection_reports.empty());

    dev.tick_handler();
    dev.read_reply();
    EXPECT_EQ(dev.connection_reports, std::vector<bool>{true});
}

TEST(InfyCanDevice, link_loss_aborts_resync_and_reports_not_connected) {
    TestDevice dev;
    dev.module_answers();
    dev.tick_handler();

    dev.link_handler(false);
    EXPECT_EQ(dev.connection_reports, std::vector<bool>{false});

    dev.sent.clear();
    dev.ticks(5);
    EXPECT_TRUE(dev.sent.empty()) << "nothing is sent while the link is down";
}

TEST(InfyCanDevice, connected_module_times_out_once_it_stops_answering) {
    TestDevice dev;
    dev.connect();

    dev.ticks(RX_TIMEOUT_TICKS - 1);
    dev.read_reply();
    dev.ticks(RX_TIMEOUT_TICKS - 1);
    EXPECT_TRUE(dev.connection_reports.empty());

    dev.ticks(2);
    EXPECT_EQ(dev.connection_reports, std::vector<bool>{false});
}

TEST(InfyCanDevice, write_acks_keep_the_connection_alive) {
    TestDevice dev;
    dev.connect();

    for (unsigned i = 0; i < 3 * RX_TIMEOUT_TICKS; ++i) {
        dev.tick_handler();
        dev.write_ack();
    }
    EXPECT_TRUE(dev.connection_reports.empty());
}

TEST(InfyCanDevice, queued_reads_wait_for_the_connection_and_interleave_with_the_cycle) {
    TestDevice dev;
    dev.request_rx(ADDR_MODULE, GenericSetting(0x11, 0x03, 0));
    dev.link_handler(true);
    dev.ticks(5);
    for (auto const& frame : dev.sent) {
        EXPECT_NE(frame.type(), 0x1103) << "queued frames wait for the connection";
    }

    dev.connect();
    dev.ticks(2);
    ASSERT_EQ(dev.sent.size(), 2u);
    unsigned requests = 0;
    for (auto const& frame : dev.sent) {
        if (frame.type() == 0x1103) {
            ++requests;
            EXPECT_TRUE(frame.is_read());
        }
    }
    EXPECT_EQ(requests, 1u) << "one queued frame per two ticks, the other tick belongs to the cycle";
}

TEST(InfyCanDevice, queued_writes_are_replaced_by_the_resync) {
    TestDevice dev;
    dev.set_output_mode(InfyCanDevice::OutputMode::Series);
    dev.set_output_mode(InfyCanDevice::OutputMode::Automatic);
    dev.connect();

    dev.ticks(10);
    for (auto const& frame : dev.sent) {
        EXPECT_NE(frame.type(), REG_OUTPUT_MODE) << "the resync was the only sender of the setting";
    }
}

TEST(InfyCanDevice, dropped_queued_frame_is_sent_on_the_next_turn) {
    TestDevice dev;
    dev.connect();
    dev.request_rx(ADDR_MODULE, GenericSetting(0x11, 0x03, 0));

    dev.accept = false;
    dev.tick_handler();
    dev.accept = true;
    dev.ticks(3);

    unsigned requests = 0;
    for (auto const& frame : dev.sent) {
        if (frame.type() == 0x1103) {
            ++requests;
        }
    }
    EXPECT_EQ(requests, 1u);
}

TEST(InfyCanDevice, frames_for_other_controllers_are_ignored) {
    TestDevice dev;
    dev.link_handler(true);
    dev.tick_handler();

    auto id = encode_can_id(ADDR_MODULE, 0xF1, CMD_READ, 0x0A, 0) | CAN_EFF_FLAG;
    dev.rx_handler(id, SystemDCVoltage(400.));
    dev.ticks(RX_TIMEOUT_TICKS + 1);

    EXPECT_EQ(dev.connection_reports, std::vector<bool>{false});
    EXPECT_TRUE(dev.writes().empty()) << "no resync was started";
}

} // namespace
