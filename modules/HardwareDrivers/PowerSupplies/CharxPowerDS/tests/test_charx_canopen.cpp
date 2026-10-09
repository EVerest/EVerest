// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <atomic>
#include <cstring>
#include <thread>

#include <gtest/gtest.h>
#include <linux/can.h>
#include <sys/socket.h>
#include <unistd.h>

#include "charx_canopen.hpp"

using namespace charx;

namespace {
Frame response(uint8_t cs, Obj o, uint32_t value) {
    return Frame{cs,
                 static_cast<uint8_t>(o.index & 0xFF),
                 static_cast<uint8_t>(o.index >> 8),
                 o.sub,
                 static_cast<uint8_t>(value),
                 static_cast<uint8_t>(value >> 8),
                 static_cast<uint8_t>(value >> 16),
                 static_cast<uint8_t>(value >> 24)};
}
} // namespace

TEST(CharxCanopen, UploadRequest) {
    EXPECT_EQ(sdo_upload_request(obj::v_meas), (Frame{0x40, 0x02, 0x40, 0x0C, 0, 0, 0, 0}));
}

TEST(CharxCanopen, DownloadRequestHasNoSizeIndication) {
    // 0x22: expedited, size not indicated - the module rejects 0x23/0x2F on its 1-byte objects
    EXPECT_EQ(sdo_download_request(obj::readiness, 1), (Frame{0x22, 0x00, 0x40, 0x01, 1, 0, 0, 0}));
    EXPECT_EQ(sdo_download_request(obj::v_set, 444000), (Frame{0x22, 0x04, 0x40, 0x06, 0x60, 0xC6, 0x06, 0x00}));
    EXPECT_EQ(sdo_download_request(obj::i_set, -1), (Frame{0x22, 0x04, 0x40, 0x02, 0xFF, 0xFF, 0xFF, 0xFF}));
}

TEST(CharxCanopen, ParseFourByteUpload) {
    const auto f = response(0x43, obj::v_meas, 379248);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::v_meas, true);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->ok());
    EXPECT_EQ(r->value, 379248);
}

TEST(CharxCanopen, ParseNegativeFourByteUpload) {
    const auto f = response(0x43, obj::i_meas, static_cast<uint32_t>(-31));
    const auto r = parse_sdo_response(f.data(), f.size(), obj::i_meas, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->value, -31);
}

TEST(CharxCanopen, ParseOneByteUploadMasksUnusedBytes) {
    // 0x4F: expedited, size indicated, 3 bytes unused - the unused bytes may carry garbage
    const auto f = response(0x4F, obj::ds_output_status, 0xABCDEF01);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::ds_output_status, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->value, 1);
}

TEST(CharxCanopen, ParseTwoByteUpload) {
    const auto f = response(0x4B, obj::contactor_error, 0xFFFF1234);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::contactor_error, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->value, 0x1234);
}

TEST(CharxCanopen, ParseExpeditedUploadWithoutSize) {
    const auto f = response(0x42, obj::flags1, 0x20000000);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::flags1, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(static_cast<uint32_t>(r->value), 0x20000000u);
}

TEST(CharxCanopen, ParseAbort) {
    const auto f = response(0x80, obj::readiness, ABORT_DEVICE_STATE);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::readiness, false);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, SdoResult::Status::Abort);
    EXPECT_EQ(r->abort_code, ABORT_DEVICE_STATE);
    EXPECT_NE(r->describe().find("device state"), std::string::npos);
}

TEST(CharxCanopen, ParseDownloadResponse) {
    const auto f = response(0x60, obj::readiness, 0);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::readiness, false);
    ASSERT_TRUE(r);
    EXPECT_TRUE(r->ok());
}

TEST(CharxCanopen, ResponseForAnotherObjectIsIgnored) {
    const auto f = response(0x43, obj::i_meas, 5);
    EXPECT_FALSE(parse_sdo_response(f.data(), f.size(), obj::v_meas, true));
    const auto g = response(0x43, Obj{0x4002, 0x0D}, 5);
    EXPECT_FALSE(parse_sdo_response(g.data(), g.size(), obj::v_meas, true));
}

TEST(CharxCanopen, ShortFrameIsIgnored) {
    const auto f = response(0x43, obj::v_meas, 5);
    EXPECT_FALSE(parse_sdo_response(f.data(), 7, obj::v_meas, true));
}

TEST(CharxCanopen, WrongCommandIsAProtocolError) {
    // a download confirmation where an upload answer is expected, and vice versa
    const auto f = response(0x60, obj::v_meas, 0);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::v_meas, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, SdoResult::Status::Protocol);
    const auto g = response(0x43, obj::readiness, 1);
    const auto s = parse_sdo_response(g.data(), g.size(), obj::readiness, false);
    ASSERT_TRUE(s);
    EXPECT_EQ(s->status, SdoResult::Status::Protocol);
}

TEST(CharxCanopen, SegmentedUploadIsAProtocolError) {
    const auto f = response(0x41, obj::v_meas, 4);
    const auto r = parse_sdo_response(f.data(), f.size(), obj::v_meas, true);
    ASSERT_TRUE(r);
    EXPECT_EQ(r->status, SdoResult::Status::Protocol);
}

TEST(CharxCanopen, FlagsToString) {
    EXPECT_EQ(flags1_to_string(0), "-");
    EXPECT_EQ(flags1_to_string((1u << flag::PfcOff) | (1u << flag::DcOutputSideOff)), "PfcOff,DcOutputSideOff");
    EXPECT_EQ(flags1_to_string(1u << flag::PowerOn), "PowerOn");
}

TEST(CharxCanopen, OpeningAMissingInterfaceThrows) {
    EXPECT_THROW(Canopen("charx_nonexistent", 2, 1, std::chrono::milliseconds(10)), std::runtime_error);
}

namespace {
// The far end of a socketpair that plays the module: answers SDO uploads of v_meas with 379248 mV.
class Responder {
public:
    explicit Responder(int fd_) : fd(fd_), thread([this] { run(); }) {
    }
    ~Responder() {
        stop = true;
        shutdown(fd, SHUT_RDWR);
        thread.join();
        close(fd);
    }
    std::atomic<bool> answer{true};
    std::atomic<int> heartbeats{0};

private:
    void run() {
        struct can_frame f {};
        while (!stop && recv(fd, &f, sizeof(f), 0) == static_cast<ssize_t>(sizeof(f))) {
            if (f.can_id == 0x701) {
                heartbeats++;
                continue;
            }
            if (f.can_id != 0x602 || !answer) {
                continue;
            }
            struct can_frame r {};
            r.can_id = 0x582;
            r.can_dlc = 8;
            const auto reply = Frame{0x43, f.data[1], f.data[2], f.data[3], 0x70, 0xC9, 0x05, 0x00};
            std::memcpy(r.data, reply.data(), 8);
            send(fd, &r, sizeof(r), MSG_NOSIGNAL);
        }
    }
    int fd;
    std::atomic<bool> stop{false};
    std::thread thread;
};

std::pair<int, int> frame_socketpair() {
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) != 0) {
        throw std::runtime_error("socketpair");
    }
    return {sv[0], sv[1]};
}
} // namespace

TEST(CharxCanopenSocket, ReadOverTheBus) {
    const auto [a, b] = frame_socketpair();
    Responder module(b);
    Canopen can(a, 2, 1, std::chrono::milliseconds(200));
    const auto r = can.read(obj::v_meas);
    ASSERT_TRUE(r.ok()) << r.describe();
    EXPECT_EQ(r.value, 379248);
    EXPECT_EQ(can.io_errors_in_row(), 0);
}

TEST(CharxCanopenSocket, NoAnswerIsATimeout) {
    const auto [a, b] = frame_socketpair();
    Responder module(b);
    module.answer = false;
    Canopen can(a, 2, 1, std::chrono::milliseconds(30));
    const auto r = can.read(obj::v_meas);
    EXPECT_EQ(r.status, SdoResult::Status::Timeout);
    EXPECT_EQ(can.io_errors_in_row(), 0);
}

TEST(CharxCanopenSocket, FailedSendIsAnIoErrorNotAnException) {
    // The crash this driver once had: a send that fails (there ENOBUFS - no node acknowledged) threw out of
    // the poll loop and took the whole EVerest stack down. Here the peer is gone, so every send fails.
    const auto [a, b] = frame_socketpair();
    close(b);
    Canopen can(a, 2, 1, std::chrono::milliseconds(30));
    SdoResult r;
    EXPECT_NO_THROW(r = can.read(obj::v_meas));
    EXPECT_EQ(r.status, SdoResult::Status::IoError);
    EXPECT_NO_THROW(r = can.write(obj::readiness, 0));
    EXPECT_EQ(r.status, SdoResult::Status::IoError);
    EXPECT_FALSE(can.start_remote_node());
    EXPECT_EQ(can.io_errors_in_row(), 2);
}

TEST(CharxCanopenSocket, HeartbeatIsSent) {
    const auto [a, b] = frame_socketpair();
    Responder module(b);
    {
        Canopen can(a, 2, 1, std::chrono::milliseconds(30));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    EXPECT_GE(module.heartbeats.load(), 1);
}
