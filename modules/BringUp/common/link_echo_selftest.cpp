// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// Not wired into any build. Compile and run on a veth pair, no hardware needed:
//   g++ -std=c++17 -Wall -Wextra -I . -o /tmp/le_test link_echo_selftest.cpp -pthread
//   unshare -rn bash -c 'ip link add veth0 type veth peer name veth1;
//     ip link set veth0 up; ip link set veth1 up; /tmp/le_test'  (one line)
#include "link_echo.hpp"
#include <cstdio>
#include <thread>

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    using namespace bringup;

    // Codec: a transport trailer (minimum-frame padding, a passed-through FCS) must not fail the
    // pattern check - the bench found the SPE chain delivering frames with trailing bytes.
    {
        echo_frame tf;
        tf.seq = 7;
        tf.payload_size = 32;
        auto bytes = echo_encode(tf);
        for (auto b : {0xDE, 0xAD, 0xBE, 0xEF}) {
            bytes.push_back(static_cast<std::uint8_t>(b));
        }
        echo_frame decoded;
        bool pattern_ok = false;
        if (not echo_decode(bytes.data(), bytes.size(), decoded, pattern_ok) or not pattern_ok or
            decoded.payload_size != 32) {
            std::printf("FAIL: codec trailer tolerance\n");
            return 1;
        }
        // ... while a frame truncated below its declared length must fail it.
        if (not echo_decode(bytes.data(), link_echo_header_size + 16, decoded, pattern_ok) or pattern_ok) {
            std::printf("FAIL: codec truncation detection\n");
            return 1;
        }
    }

    link_echo a("veth0", [] {});
    link_echo b("veth1", [] {});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto sa = a.stats();
    auto sb = b.stats();
    if (not sa.armed or not sb.armed) {
        std::printf("FAIL: not armed: a='%s' b='%s'\n", sa.error.c_str(), sb.error.c_str());
        return 1;
    }
    a.send_burst(10, 32);
    a.send_burst(5, 1400);
    b.send_burst(3, 64); // the other direction too
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    sa = a.stats();
    sb = b.stats();
    std::printf("a: tx_ping=%u rx_pong=%u bad=%u rx_ping=%u tx_pong=%u rtt_last=%lu rtt_avg=%lu peer=%s\n", sa.tx_ping,
                sa.rx_pong, sa.rx_pong_bad, sa.rx_ping, sa.tx_pong, (unsigned long)sa.last_rtt_us,
                (unsigned long)sa.avg_rtt_us, sa.peer_mac.c_str());
    std::printf("b: tx_ping=%u rx_pong=%u bad=%u rx_ping=%u tx_pong=%u\n", sb.tx_ping, sb.rx_pong, sb.rx_pong_bad,
                sb.rx_ping, sb.tx_pong);
    bool const ok = sa.tx_ping == 15 and sa.rx_pong == 15 and sa.rx_pong_bad == 0 and sa.rx_ping == 3 and
                    sa.tx_pong == 3 and sb.rx_ping == 15 and sb.tx_pong == 15 and sb.rx_pong == 3 and
                    sb.rx_pong_bad == 0 and not sa.peer_mac.empty();
    std::printf("%s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
