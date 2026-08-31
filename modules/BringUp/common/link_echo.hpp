// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#pragma once

// Raw-ethernet echo test for the MCS bring-up panels: proves that payload actually crosses the
// SPE link. Both TAP devices usually live on ONE bench host, where any IP traffic between them
// (ping, UDP - even bound to the device) is short-circuited by local routing and by the
// local-martian filter, and never touches the wire. AF_PACKET frames sent on a specific device
// have no such shortcut: they go tap -> daemon -> UDP -> LAN8650 -> 10BASE-T1S -> and back up the
// whole chain on the far side.
//
// Frame format (EtherType 0x88B5, the IEEE 802 "local experimental" one):
//   [eth dst 6][eth src 6][0x88B5 2] [magic "MCSECHO1" 8][type u8][flags u8][seq u32 be]
//   [t_send_ns u64 be][payload_len u16 be][pattern payload ...][ignored trailer]
// The payload length is explicit because the transport may append bytes - Ethernet MACs pad
// short frames to the 60-byte minimum, and a bridging hop may pass the FCS through - and a
// pattern check inferred from the frame size would fail on every such frame.
// PING carries a seq-derived byte pattern; the responder verifies it and answers PONG (payload
// echoed) with the ok-flag, unicast to the ping's source MAC. RTT is computed at the ping sender
// from its own echoed timestamp, so it needs no cross-host clock.
//
// Needs CAP_NET_RAW - the bring-up scripts run the slac panels under sudo, like the daemon
// (setcap is no alternative: capability-armed binaries ignore LD_LIBRARY_PATH).

#include <arpa/inet.h>
#include <cstring>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bringup {

constexpr std::uint16_t link_echo_ethertype = 0x88B5;
constexpr std::size_t link_echo_header_size = 14 + 8 + 1 + 1 + 4 + 8 + 2;

// --- pure codec, kept free of sockets so it stays reasonable to eyeball -----------------------

struct echo_frame {
    std::array<std::uint8_t, 6> dst{};
    std::array<std::uint8_t, 6> src{};
    bool is_pong{false};
    bool payload_ok{false}; // PONG only: the responder's verdict on the ping's pattern
    std::uint32_t seq{0};
    std::uint64_t t_send_ns{0};
    std::size_t payload_size{0};
};

inline std::uint8_t echo_pattern_byte(std::uint32_t seq, std::size_t i) {
    return static_cast<std::uint8_t>((seq * 31u + i * 7u + 0xA5u) & 0xFFu);
}

inline std::vector<std::uint8_t> echo_encode(echo_frame const& f) {
    std::vector<std::uint8_t> out(link_echo_header_size + f.payload_size, 0);
    std::memcpy(out.data(), f.dst.data(), 6);
    std::memcpy(out.data() + 6, f.src.data(), 6);
    out[12] = link_echo_ethertype >> 8;
    out[13] = link_echo_ethertype & 0xFF;
    std::memcpy(out.data() + 14, "MCSECHO1", 8);
    out[22] = f.is_pong ? 1 : 0;
    out[23] = f.payload_ok ? 1 : 0;
    for (int i = 0; i < 4; i++) {
        out[24 + i] = static_cast<std::uint8_t>(f.seq >> (24 - 8 * i));
    }
    for (int i = 0; i < 8; i++) {
        out[28 + i] = static_cast<std::uint8_t>(f.t_send_ns >> (56 - 8 * i));
    }
    out[36] = static_cast<std::uint8_t>(f.payload_size >> 8);
    out[37] = static_cast<std::uint8_t>(f.payload_size & 0xFF);
    for (std::size_t i = 0; i < f.payload_size; i++) {
        out[link_echo_header_size + i] = echo_pattern_byte(f.seq, i);
    }
    return out;
}

// Returns false for anything that is not one of our frames. `pattern_ok` is evaluated against
// the seq-derived pattern either way; for a PONG the sender cross-checks it too.
inline bool echo_decode(std::uint8_t const* data, std::size_t size, echo_frame& f, bool& pattern_ok) {
    if (size < link_echo_header_size) {
        return false;
    }
    if (data[12] != (link_echo_ethertype >> 8) or data[13] != (link_echo_ethertype & 0xFF)) {
        return false;
    }
    if (std::memcmp(data + 14, "MCSECHO1", 8) != 0) {
        return false;
    }
    std::memcpy(f.dst.data(), data, 6);
    std::memcpy(f.src.data(), data + 6, 6);
    f.is_pong = data[22] != 0;
    f.payload_ok = data[23] != 0;
    f.seq = 0;
    for (int i = 0; i < 4; i++) {
        f.seq = (f.seq << 8) | data[24 + i];
    }
    f.t_send_ns = 0;
    for (int i = 0; i < 8; i++) {
        f.t_send_ns = (f.t_send_ns << 8) | data[28 + i];
    }
    // The declared length is authoritative; anything beyond it is transport trailer (minimum-frame
    // padding, a passed-through FCS) and deliberately ignored. A frame SHORTER than its declaration
    // really did lose payload, and that must fail the pattern, not read out of bounds.
    f.payload_size = (static_cast<std::size_t>(data[36]) << 8) | data[37];
    if (link_echo_header_size + f.payload_size > size) {
        pattern_ok = false;
        return true;
    }
    pattern_ok = true;
    for (std::size_t i = 0; i < f.payload_size; i++) {
        if (data[link_echo_header_size + i] != echo_pattern_byte(f.seq, i)) {
            pattern_ok = false;
            break;
        }
    }
    return true;
}

// --- the engine --------------------------------------------------------------------------------

struct echo_stats {
    bool armed{false};       // socket open, responder listening
    std::string error;       // why not, when not armed
    std::string device;
    std::string own_mac;
    std::string peer_mac;    // source of the last frame received from the wire
    std::uint32_t tx_ping{0};
    std::uint32_t rx_pong{0};
    std::uint32_t rx_pong_bad{0}; // pong whose echoed payload failed verification
    std::uint32_t rx_ping{0};     // we are also the responder for the other side
    std::uint32_t tx_pong{0};
    std::uint64_t last_rtt_us{0};
    std::uint64_t avg_rtt_us{0}; // running mean over rx_pong
    bool auto_ping{false};
};

class link_echo {
public:
    // on_update is called from internal threads after every counter change - post a UI event,
    // do not render from it.
    link_echo(std::string device, std::function<void()> on_update) :
        m_device(std::move(device)), m_on_update(std::move(on_update)) {
        m_stats.device = m_device;
        if (m_device.empty()) {
            m_stats.error = "disabled (echo_device empty)";
            return;
        }
        open_socket();
        if (m_fd >= 0) {
            m_rx = std::thread([this] { rx_loop(); });
            m_auto = std::thread([this] { auto_loop(); });
        }
    }

    link_echo(link_echo const&) = delete;
    link_echo& operator=(link_echo const&) = delete;

    ~link_echo() {
        m_stop = true;
        if (m_fd >= 0) {
            ::shutdown(m_fd, SHUT_RDWR);
            ::close(m_fd);
        }
        if (m_rx.joinable()) {
            m_rx.join();
        }
        if (m_auto.joinable()) {
            m_auto.join();
        }
    }

    echo_stats stats() const {
        std::scoped_lock lock(m_mutex);
        return m_stats;
    }

    // Broadcast pings: no MAC discovery needed, the responder unicasts back.
    void send_burst(unsigned count, std::size_t payload_size) {
        for (unsigned i = 0; i < count and not m_stop; i++) {
            send_ping(payload_size);
        }
        m_on_update();
    }

    void toggle_auto_ping() {
        std::scoped_lock lock(m_mutex);
        m_stats.auto_ping = not m_stats.auto_ping;
    }

private:
    void open_socket() {
        m_fd = ::socket(AF_PACKET, SOCK_RAW, htons(link_echo_ethertype));
        if (m_fd < 0) {
            fail(errno == EPERM or errno == EACCES ? "no CAP_NET_RAW - run this panel under sudo"
                                                   : std::string("socket: ") + std::strerror(errno));
            return;
        }
        ifreq ifr{};
        std::strncpy(ifr.ifr_name, m_device.c_str(), IFNAMSIZ - 1);
        if (::ioctl(m_fd, SIOCGIFINDEX, &ifr) < 0) {
            fail("device '" + m_device + "' not found");
            return;
        }
        m_ifindex = ifr.ifr_ifindex;
        if (::ioctl(m_fd, SIOCGIFHWADDR, &ifr) < 0) {
            fail(std::string("SIOCGIFHWADDR: ") + std::strerror(errno));
            return;
        }
        // shutdown() does not unblock recv() on AF_PACKET sockets, so the rx loop wakes on a
        // timeout to notice m_stop - 200 ms bounds the destructor, not the data path.
        timeval tv{};
        tv.tv_usec = 200 * 1000;
        ::setsockopt(m_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::memcpy(m_own_mac.data(), ifr.ifr_hwaddr.sa_data, 6);
        sockaddr_ll addr{};
        addr.sll_family = AF_PACKET;
        addr.sll_protocol = htons(link_echo_ethertype);
        addr.sll_ifindex = m_ifindex;
        if (::bind(m_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            fail(std::string("bind: ") + std::strerror(errno));
            return;
        }
        std::scoped_lock lock(m_mutex);
        m_stats.armed = true;
        m_stats.own_mac = mac_string(m_own_mac);
    }

    void fail(std::string why) {
        if (m_fd >= 0) {
            ::close(m_fd);
            m_fd = -1;
        }
        std::scoped_lock lock(m_mutex);
        m_stats.error = std::move(why);
    }

    static std::string mac_string(std::array<std::uint8_t, 6> const& mac) {
        char buf[18];
        std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
                      mac[5]);
        return buf;
    }

    static std::uint64_t now_ns() {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
                .count());
    }

    void send_frame(echo_frame const& f) {
        auto const bytes = echo_encode(f);
        sockaddr_ll addr{};
        addr.sll_family = AF_PACKET;
        addr.sll_protocol = htons(link_echo_ethertype);
        addr.sll_ifindex = m_ifindex;
        addr.sll_halen = 6;
        std::memcpy(addr.sll_addr, f.dst.data(), 6);
        ::sendto(m_fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr));
    }

    void send_ping(std::size_t payload_size) {
        echo_frame f;
        f.dst.fill(0xFF);
        f.src = m_own_mac;
        f.is_pong = false;
        f.t_send_ns = now_ns();
        {
            std::scoped_lock lock(m_mutex);
            f.seq = ++m_seq;
            m_stats.tx_ping++;
        }
        f.payload_size = payload_size;
        send_frame(f);
    }

    void rx_loop() {
        std::vector<std::uint8_t> buf(2048);
        while (not m_stop) {
            auto const n = ::recv(m_fd, buf.data(), buf.size(), 0);
            if (n <= 0) {
                if (m_stop) {
                    return;
                }
                continue;
            }
            echo_frame f;
            bool pattern_ok = false;
            if (not echo_decode(buf.data(), static_cast<std::size_t>(n), f, pattern_ok)) {
                continue;
            }
            if (f.src == m_own_mac) {
                // Our own broadcast looped back by the kernel - not wire traffic, not counted.
                continue;
            }
            if (f.is_pong) {
                auto const rtt_us = (now_ns() - f.t_send_ns) / 1000u;
                std::scoped_lock lock(m_mutex);
                m_stats.peer_mac = mac_string(f.src);
                m_stats.rx_pong++;
                if (not(pattern_ok and f.payload_ok)) {
                    m_stats.rx_pong_bad++;
                }
                m_stats.last_rtt_us = rtt_us;
                // Running mean; the delta must be signed - a sample faster than the average would
                // otherwise underflow the unsigned subtraction.
                m_stats.avg_rtt_us = static_cast<std::uint64_t>(
                    static_cast<std::int64_t>(m_stats.avg_rtt_us) +
                    (static_cast<std::int64_t>(rtt_us) - static_cast<std::int64_t>(m_stats.avg_rtt_us)) /
                        static_cast<std::int64_t>(m_stats.rx_pong));
            } else {
                echo_frame pong = f;
                pong.dst = f.src;
                pong.src = m_own_mac;
                pong.is_pong = true;
                pong.payload_ok = pattern_ok; // our verdict rides back; timestamp+seq echoed as-is
                send_frame(pong);
                std::scoped_lock lock(m_mutex);
                m_stats.peer_mac = mac_string(f.src);
                m_stats.rx_ping++;
                m_stats.tx_pong++;
            }
            m_on_update();
        }
    }

    void auto_loop() {
        while (not m_stop) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            bool enabled = false;
            {
                std::scoped_lock lock(m_mutex);
                enabled = m_stats.auto_ping;
            }
            if (enabled and not m_stop) {
                send_ping(64);
                m_on_update();
            }
        }
    }

    std::string m_device;
    std::function<void()> m_on_update;
    int m_fd{-1};
    int m_ifindex{0};
    std::array<std::uint8_t, 6> m_own_mac{};
    std::atomic<bool> m_stop{false};
    std::uint32_t m_seq{0};
    mutable std::mutex m_mutex;
    echo_stats m_stats;
    std::thread m_rx;
    std::thread m_auto;
};

} // namespace bringup
