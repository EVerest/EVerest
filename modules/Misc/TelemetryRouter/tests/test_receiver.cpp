// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <catch2/catch_all.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <everest/io/event/unique_fd.hpp>
#include <utils/telemetry/transport.hpp>
#include <utils/telemetry/wire.hpp>

#include "receiver.hpp"

using namespace telemetry_router;
using namespace std::chrono_literals;

namespace {

struct Collector {
    void datagram(const std::uint8_t* data, std::size_t size) {
        const std::lock_guard lock(mutex);
        datagrams.emplace_back(data, data + size);
        cv.notify_all();
    }

    void truncated() {
        const std::lock_guard lock(mutex);
        ++truncated_count;
        cv.notify_all();
    }

    template <typename Predicate> bool wait(Predicate predicate) {
        std::unique_lock lock(mutex);
        return cv.wait_for(lock, 5s, predicate);
    }

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<std::vector<std::uint8_t>> datagrams;
    int truncated_count{0};
    std::atomic<int> ticks{0};
};

std::string make_socket_path() {
    std::array<char, 40> dir_template{"/tmp/telemetry_router_test_XXXXXX"};
    REQUIRE(::mkdtemp(dir_template.data()) != nullptr);
    return std::string(dir_template.data()) + "/telemetry.sock";
}

// blocking, so that a full receive queue delays the test instead of dropping datagrams
void send_to(const std::string& path, const std::vector<std::uint8_t>& data) {
    everest::lib::io::event::unique_fd client(::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    REQUIRE(path.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    REQUIRE(::sendto(client, data.data(), data.size(), 0, reinterpret_cast<const sockaddr*>(&address),
                     sizeof(address)) == static_cast<ssize_t>(data.size()));
}

} // namespace

TEST_CASE("The receiver delivers queued and new datagrams on its own thread", "[telemetry_router]") {
    const auto path = make_socket_path();
    everest::lib::io::event::unique_fd socket(everest::telemetry::bind_receiver_socket(path, 0600));
    Collector collector;

    const auto first = everest::telemetry::wire::encode(everest::telemetry::wire::Sample{"m", "before", 1, 1});
    send_to(path, first);

    DatagramReceiver receiver(
        std::move(socket), [&collector](const std::uint8_t* data, std::size_t size) { collector.datagram(data, size); },
        [&collector]() { collector.truncated(); }, 20ms, [&collector]() { ++collector.ticks; });
    receiver.start();

    for (int i = 0; i < 100; ++i) {
        send_to(path, first);
    }
    send_to(path, std::vector<std::uint8_t>(everest::telemetry::wire::MAX_DATAGRAM_SIZE + 100, 'x'));

    REQUIRE(
        collector.wait([&collector] { return collector.datagrams.size() == 101 and collector.truncated_count == 1; }));
    CHECK(collector.datagrams.front() == first);
    REQUIRE(collector.wait([&collector] { return collector.ticks >= 2; }));

    const auto start = std::chrono::steady_clock::now();
    receiver.stop();
    CHECK(std::chrono::steady_clock::now() - start < 1s);

    ::unlink(path.c_str());
}
