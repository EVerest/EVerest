// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <cstring>

#include <gtest/gtest.h>

#include <goose/sender.hpp>

class DummyEthernetInterface : public goose_ethernet::EthernetInterfaceIntf {
    std::function<void(const std::uint8_t*, size_t)> send_callback;
    std::function<std::vector<std::uint8_t>()> receive_callback;

public:
    DummyEthernetInterface(std::function<void(const std::uint8_t*, size_t)> send_callback,
                           std::function<std::vector<std::uint8_t>()> receive_callback) :
        send_callback(send_callback), receive_callback(receive_callback) {
    }

    void send_packet_raw(const std::uint8_t* packet, size_t size) override {
        send_callback(packet, size);
    }

    std::optional<std::vector<std::uint8_t>> receive_packet_raw() override {
        return receive_callback();
    }

    // Dummy implementation
    const std::uint8_t* get_mac_address() const override {
        return nullptr;
    }
};

TEST(DummyEthernetInterface, callback_works) {
    std::uint8_t send_counter = 0;
    std::uint8_t receive_counter = 0;

    auto dummy_frame = goose_ethernet::EthernetFrame(std::vector<std::uint8_t>(60));

    DummyEthernetInterface intf(
        [&](const std::uint8_t* packet, size_t size) {
            ASSERT_EQ(dummy_frame.serialize().size(), size);
            send_counter++;
        },
        [&]() -> std::vector<std::uint8_t> {
            receive_counter++;
            return dummy_frame.serialize();
        });

    intf.send_packet(dummy_frame);
    EXPECT_EQ(send_counter, 1);

    auto received = intf.receive_packet();
    if (!received.has_value()) {
        FAIL() << "Received frame is empty";
    }
    auto recv = received.value();
    EXPECT_EQ(receive_counter, 1);
    EXPECT_EQ(recv.serialize(), dummy_frame.serialize());
}

struct SentFrame {
    std::uint32_t st_num;
    std::uint32_t sq_num;

    bool operator==(const SentFrame& other) const {
        return st_num == other.st_num && sq_num == other.sq_num;
    }
};

class FrameRecorder {
    std::mutex mutex;
    std::vector<SentFrame> frames;

public:
    void record(const std::uint8_t* data, size_t size) {
        auto frame = goose::frame::GooseFrame(goose_ethernet::EthernetFrame(data, size));
        std::lock_guard<std::mutex> lock(mutex);
        frames.push_back({frame.pdu.st_num, frame.pdu.sq_num});
    }

    std::vector<SentFrame> sent() {
        std::lock_guard<std::mutex> lock(mutex);
        return frames;
    }
};

class SteppedSender : public goose::sender::Sender {
    std::vector<std::chrono::milliseconds> requested_waits;
    size_t elapsed_waits = 0;
    bool waiting = false;

protected:
    void wait_for_new_packet(std::unique_lock<std::mutex>& lock, std::chrono::milliseconds timeout) override {
        requested_waits.push_back(timeout);
        auto woken = [this] { return stop_flag || has_new_package || elapsed_waits >= requested_waits.size(); };
        if (woken()) {
            return;
        }
        waiting = true;
        current_packet_cv.notify_all();
        current_packet_cv.wait(lock, woken);
        waiting = false;
    }

public:
    using goose::sender::Sender::Sender;

    ~SteppedSender() {
        SteppedSender::stop();
    }

    bool await_blocked_wait(size_t count) {
        const auto generous_bound_for_loaded_hosts = std::chrono::seconds(5);
        std::unique_lock<std::mutex> lock(current_packet_mutex);
        return current_packet_cv.wait_for(lock, generous_bound_for_loaded_hosts,
                                          [this, count] { return waiting && requested_waits.size() >= count; });
    }

    std::vector<std::chrono::milliseconds> requested_waits_so_far() {
        std::lock_guard<std::mutex> lock(current_packet_mutex);
        return requested_waits;
    }

    void elapse_wait() {
        {
            std::lock_guard<std::mutex> lock(current_packet_mutex);
            elapsed_waits = requested_waits.size();
        }
        current_packet_cv.notify_all();
    }

    void stop() override {
        {
            std::lock_guard<std::mutex> lock(current_packet_mutex);
            stop_flag = true;
        }
        goose::sender::Sender::stop();
    }
};

static goose::frame::GooseFrame make_frame() {
    goose::frame::GooseFrame frame;
    frame.appid[0] = 0x01;
    frame.appid[1] = 0x02;
    strcpy(frame.pdu.go_cb_ref, "goose_cb_ref");
    strcpy(frame.pdu.dat_set, "dataset");
    strcpy(frame.pdu.go_id, "goose_id");
    frame.pdu.timestamp = goose::frame::GooseTimestamp::now();
    // Random data, should be overwritten by sender
    frame.pdu.sq_num = 0xdead;
    frame.pdu.st_num = 0xbeef;
    return frame;
}

using std::chrono::milliseconds;

TEST(Sender, single_frame_goes_through_all_delays) {
    auto recorder = std::make_shared<FrameRecorder>();
    auto intf = std::make_shared<DummyEthernetInterface>(
        [recorder](const std::uint8_t* data, size_t size) { recorder->record(data, size); },
        []() -> std::vector<std::uint8_t> { return {}; });

    SteppedSender sender(milliseconds(10), {milliseconds(1), milliseconds(2), milliseconds(4)}, intf);

    sender.start();

    sender.send(new goose::sender::SendPacketNormal(make_frame()));

    const std::vector<milliseconds> expected_waits{milliseconds(1), milliseconds(2), milliseconds(4), milliseconds(10),
                                                   milliseconds(10)};
    std::vector<SentFrame> expected_frames;
    for (size_t i = 0; i < expected_waits.size(); i++) {
        ASSERT_TRUE(sender.await_blocked_wait(i + 1));
        expected_frames.push_back({1, static_cast<std::uint32_t>(i)});

        ASSERT_EQ(recorder->sent(), expected_frames);
        ASSERT_EQ(sender.requested_waits_so_far(),
                  std::vector<milliseconds>(expected_waits.begin(), expected_waits.begin() + i + 1));

        sender.elapse_wait();
    }
}

TEST(Sender, multiple_frames_go_through_all_delays) {
    auto recorder = std::make_shared<FrameRecorder>();
    auto intf = std::make_shared<DummyEthernetInterface>(
        [recorder](const std::uint8_t* data, size_t size) { recorder->record(data, size); },
        []() -> std::vector<std::uint8_t> { return {}; });

    auto t0 = milliseconds(10);
    auto ts = std::vector<milliseconds>{milliseconds(1), milliseconds(2), milliseconds(4)};

    SteppedSender sender(t0, ts, intf);

    sender.start();

    const std::vector<milliseconds> waits_per_frame{ts[0], ts[1], ts[2], t0, t0};
    std::vector<milliseconds> expected_waits;
    std::vector<SentFrame> expected_frames;

    for (std::uint32_t st_num = 1; st_num <= 5; st_num++) {
        sender.send(new goose::sender::SendPacketNormal(make_frame()));

        for (std::uint32_t sq_num = 0; sq_num < waits_per_frame.size(); sq_num++) {
            expected_waits.push_back(waits_per_frame[sq_num]);
            expected_frames.push_back({st_num, sq_num});
            ASSERT_TRUE(sender.await_blocked_wait(expected_waits.size()));

            ASSERT_EQ(recorder->sent(), expected_frames);
            ASSERT_EQ(sender.requested_waits_so_far(), expected_waits);

            if (sq_num + 1 < waits_per_frame.size()) {
                sender.elapse_wait();
            }
        }
    }
}

TEST(Sender, frame_sent_before_start_goes_through_all_delays) {
    auto recorder = std::make_shared<FrameRecorder>();
    auto intf = std::make_shared<DummyEthernetInterface>(
        [recorder](const std::uint8_t* data, size_t size) { recorder->record(data, size); },
        []() -> std::vector<std::uint8_t> { return {}; });

    SteppedSender sender(milliseconds(10), {milliseconds(1), milliseconds(2), milliseconds(4)}, intf);

    sender.send(new goose::sender::SendPacketNormal(make_frame()));
    sender.start();

    ASSERT_TRUE(sender.await_blocked_wait(1));

    ASSERT_EQ(recorder->sent(), (std::vector<SentFrame>{{1, 0}}));
    ASSERT_EQ(sender.requested_waits_so_far(), std::vector<milliseconds>{milliseconds(1)});
}
