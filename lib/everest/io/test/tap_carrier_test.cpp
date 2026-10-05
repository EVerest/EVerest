// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Carrier support for TAP devices: tap_handler's open(..., carrier_on), set_carrier, carrier, and the kernel
// behaviour a carrier watcher relies on (IFF_LOWER_UP of RTM_NEWLINK; a tap has no operstate string).
// TUNSETCARRIER flips netif_carrier_ok() at once; IFF_RUNNING, the qdisc and the rtnetlink notification follow
// with the linkwatch work, about once per second. Hence the waits.
// TAP creation needs CAP_NET_ADMIN; tap_handler::open flattens every creation failure to EPERM, so
// probe_tap_creation asks the kernel and separates "cannot create" (skip) from "open() failed anyway" (defect).

#include <everest/io/event/fd_event_client.hpp>
#include <everest/io/event/unique_fd.hpp>
#include <everest/io/tun_tap/tap_client.hpp>
#include <everest/io/tun_tap/tap_handler.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <net/if.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <linux/if_tun.h>
#include <linux/rtnetlink.h>

#include <gtest/gtest.h>

using everest::lib::io::event::unique_fd;
using everest::lib::io::tun_tap::tap_client;
using everest::lib::io::tun_tap::tap_handler;

namespace {

/// Minimal synchronous ClientPolicy: a pollable fd that never becomes readable plus the errno the case asks for.
class residual_error_policy {
public:
    using PayloadT = std::vector<std::uint8_t>;

    bool open(int residual_error) {
        m_fd = unique_fd(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
        m_error = residual_error;
        return m_fd.is_fd();
    }

    bool tx(PayloadT const&) {
        return true;
    }

    bool rx(PayloadT& data) {
        data.clear();
        return false;
    }

    int get_fd() const {
        return m_fd;
    }

    int get_error() const {
        return m_error;
    }

private:
    unique_fd m_fd;
    int m_error{0};
};

using residual_error_client = everest::lib::io::event::fd_event_client<residual_error_policy>::type;

/// Drive \p client's event loop until \p predicate holds or \p timeout elapses.
template <class ClientT, class PredicateT>
bool pump_until(ClientT& client, std::chrono::milliseconds timeout, PredicateT predicate) {
    auto const deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        client.sync(std::chrono::milliseconds(1));
        if (predicate()) {
            return true;
        }
    }
    return predicate();
}

// <net/if.h> stops at IFF_DYNAMIC; <linux/if.h> next to <net/if.h> collides on struct ifreq.
#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP 0x10000
#endif

constexpr int test_mtu = 1500;
constexpr int mac_address_length = 6;
// A /30 per test out of TEST-NET-1 (RFC 5737); ctest jobs may overlap in time.
constexpr char test_netmask[] = "255.255.255.252";
// Covers the ~1 s linkwatch dampening interval.
constexpr int settle_timeout_ms = 4000;
// An activated qdisc adds no delay; this covers scheduling only.
constexpr int frame_timeout_ms = 500;
// IEEE 802.1 local experimental ethertype; nothing on the host stack claims it.
constexpr std::uint8_t test_ethertype_high = 0x88;
constexpr std::uint8_t test_ethertype_low = 0xb5;

/// The kernel's own verdict on creating a TAP device. Returns 0 when one can be created.
int probe_tap_creation() {
    unique_fd probe(::open("/dev/net/tun", O_RDWR));
    if (not probe.is_fd()) {
        return errno;
    }
    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, "evioprobe", IFNAMSIZ - 1);
    ifr.ifr_flags = IFF_TAP | IFF_NO_PI;
    if (::ioctl(probe, TUNSETIFF, &ifr) == -1) {
        return errno;
    }
    return 0;
}

/// Open \p tap, or skip when the environment cannot create a TAP device. When the probe shows it can, a failed
/// open() is a defect and fails the test. A macro because GTEST_SKIP and FAIL only return from the test body.
#define OPEN_TAP_OR_SKIP(tap, device, ip, carrier_on)                                                                  \
    do {                                                                                                               \
        if (not(tap).open((device), (ip), test_netmask, test_mtu, (carrier_on))) {                                     \
            const int reason = probe_tap_creation();                                                                   \
            if (reason == 0) {                                                                                         \
                FAIL() << "open() failed on '" << (device) << "' (" << std::strerror((tap).get_error())                \
                       << ") although this environment can create TAP devices";                                        \
            }                                                                                                          \
            GTEST_SKIP() << "cannot create TAP device '" << (device) << "': " << std::strerror(reason)                 \
                         << " - CAP_NET_ADMIN is required";                                                            \
        }                                                                                                              \
    } while (false)

/// Skip when the kernel predates TUNSETCARRIER (v5.0); the ioctl reports EINVAL or ENOTTY.
#define SKIP_IF_CARRIER_UNSUPPORTED(tap, ok)                                                                           \
    do {                                                                                                               \
        if (not(ok) and ((tap).get_error() == EINVAL or (tap).get_error() == ENOTTY)) {                                \
            GTEST_SKIP() << "kernel does not implement TUNSETCARRIER: " << std::strerror((tap).get_error());           \
        }                                                                                                              \
    } while (false)

/// Poll \ref tap_handler::carrier until it reports \p expected; the operstate lags the ioctl (linkwatch).
bool wait_for_carrier(tap_handler const& tap, bool expected, int timeout_ms) {
    using clock = std::chrono::steady_clock;
    auto const deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        if (tap.carrier() == std::optional<bool>(expected)) {
            return true;
        }
        if (clock::now() >= deadline) {
            return false;
        }
        ::usleep(20 * 1000);
    }
}

/// An rtnetlink socket subscribed to RTMGRP_LINK.
unique_fd open_link_watcher() {
    unique_fd fd(::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE));
    if (not fd.is_fd()) {
        return fd;
    }
    sockaddr_nl addr{};
    addr.nl_family = AF_NETLINK;
    addr.nl_groups = RTMGRP_LINK;
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        fd.close();
    }
    return fd;
}

/// Feed the ifi_flags of every RTM_NEWLINK for \p ifindex to \p visit until it returns true or the timeout elapses.
template <class VisitT> void for_each_link_flags(int nl_fd, int ifindex, int timeout_ms, VisitT visit) {
    using clock = std::chrono::steady_clock;
    auto const deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    alignas(NLMSG_ALIGNTO) char buffer[8192];

    while (true) {
        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (remaining <= 0) {
            return;
        }
        pollfd pfd{nl_fd, POLLIN, 0};
        if (::poll(&pfd, 1, static_cast<int>(remaining)) <= 0) {
            return;
        }
        int length = static_cast<int>(::recv(nl_fd, buffer, sizeof(buffer), 0));
        if (length <= 0) {
            return;
        }
        for (auto* header = reinterpret_cast<nlmsghdr*>(buffer); NLMSG_OK(header, length);
             header = NLMSG_NEXT(header, length)) {
            if (header->nlmsg_type != RTM_NEWLINK) {
                continue;
            }
            auto const* info = static_cast<ifinfomsg const*>(NLMSG_DATA(header));
            if (info->ifi_index == ifindex and visit(static_cast<unsigned int>(info->ifi_flags))) {
                return;
            }
        }
    }
}

/// The ifi_flags of every RTM_NEWLINK for \p ifindex within \p timeout_ms, in order; waits out the whole window.
std::vector<unsigned int> collect_link_flags(int nl_fd, int ifindex, int timeout_ms) {
    std::vector<unsigned int> observed;
    for_each_link_flags(nl_fd, ifindex, timeout_ms, [&observed](unsigned int flags) {
        observed.push_back(flags);
        return false;
    });
    return observed;
}

/// Flags of the first RTM_NEWLINK on \p ifindex whose \p mask bits are in state \p expect_set; nothing on timeout.
std::optional<unsigned int> await_link_flags(int nl_fd, int ifindex, unsigned int mask, bool expect_set,
                                             int timeout_ms) {
    std::optional<unsigned int> matched;
    for_each_link_flags(nl_fd, ifindex, timeout_ms, [&](unsigned int flags) {
        if (((flags & mask) == mask) != expect_set) {
            return false;
        }
        matched = flags;
        return true;
    });
    return matched;
}

/// A minimum sized Ethernet frame with a locally administered source and the test ethertype.
std::vector<std::uint8_t> test_frame() {
    std::vector<std::uint8_t> frame(60, 0x00);
    std::memset(frame.data(), 0xff, mac_address_length);
    frame[mac_address_length] = 0x02;
    frame[2 * mac_address_length - 1] = 0x01;
    frame[2 * mac_address_length] = test_ethertype_high;
    frame[2 * mac_address_length + 1] = test_ethertype_low;
    return frame;
}

bool is_test_frame(std::vector<std::uint8_t> const& frame) {
    return frame.size() >= 2 * mac_address_length + 2 and frame[2 * mac_address_length] == test_ethertype_high and
           frame[2 * mac_address_length + 1] == test_ethertype_low;
}

/// Read from the tap until \ref test_frame arrives, ignoring the host stack's own frames (IPv6 DAD, MLD).
bool await_test_frame(tap_handler& tap, int timeout_ms) {
    using clock = std::chrono::steady_clock;
    auto const deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    tap_handler::PayloadT frame;
    while (true) {
        auto const remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now()).count();
        if (remaining <= 0) {
            return false;
        }
        pollfd pfd{tap.get_fd(), POLLIN, 0};
        if (::poll(&pfd, 1, static_cast<int>(remaining)) <= 0) {
            return false;
        }
        if (tap.rx(frame) and is_test_frame(frame)) {
            return true;
        }
    }
}

/// Discard whatever the host stack has already queued towards the tap.
void drain_tap(tap_handler& tap) {
    tap_handler::PayloadT frame;
    while (tap.rx(frame)) {
    }
}

/// An AF_PACKET socket and the address of \p ifindex, as a SLAC sender uses them.
struct raw_sender {
    unique_fd fd;
    sockaddr_ll destination{};

    bool send(std::vector<std::uint8_t> const& frame) {
        return ::sendto(fd, frame.data(), frame.size(), 0, reinterpret_cast<sockaddr*>(&destination),
                        sizeof(destination)) == static_cast<ssize_t>(frame.size());
    }
};

raw_sender open_raw_sender(int ifindex) {
    raw_sender sender{unique_fd(::socket(AF_PACKET, SOCK_RAW, 0)), {}};
    sender.destination.sll_family = AF_PACKET;
    sender.destination.sll_ifindex = ifindex;
    sender.destination.sll_halen = mac_address_length;
    std::memset(sender.destination.sll_addr, 0xff, mac_address_length);
    return sender;
}

} // namespace

// Default open() leaves the kernel's carrier on; no ioctl is issued when carrier_on is not passed.
TEST(tap_carrier, default_open_leaves_the_carrier_on) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_a", "192.0.2.1", true);

    // Settled state only: the initial operstate is IF_OPER_UNKNOWN for a fresh device, DOWN for a persistent tap.
    EXPECT_TRUE(wait_for_carrier(tap, true, settle_timeout_ms)) << "the carrier never came up";
}

// A watcher subscribed before the device exists never sees a carrier. open() drops the carrier before IFF_UP since
// the IFF_UP transition announces synchronously. IFF_RUNNING derives from the operstate, which starts at
// IF_OPER_UNKNOWN (read as running by dev_get_flags) until linkwatch corrects it; LOWER_UP is exact and never set.
TEST(tap_carrier, open_with_carrier_off_is_never_announced_with_a_carrier) {
    unique_fd watcher = open_link_watcher();
    ASSERT_TRUE(watcher.is_fd()) << "rtnetlink socket: " << std::strerror(errno);

    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_b", "192.0.2.5", false);

    if (tap.carrier_setup_error() == EINVAL or tap.carrier_setup_error() == ENOTTY) {
        GTEST_SKIP() << "kernel does not implement TUNSETCARRIER: " << std::strerror(tap.carrier_setup_error());
    }
    ASSERT_EQ(tap.carrier_setup_error(), 0)
        << "the carrier request failed: " << std::strerror(tap.carrier_setup_error());

    const int ifindex = static_cast<int>(::if_nametoindex("eviocarr_b"));
    ASSERT_NE(ifindex, 0) << "if_nametoindex: " << std::strerror(errno);

    auto const announced = collect_link_flags(watcher, ifindex, settle_timeout_ms);
    ASSERT_FALSE(announced.empty()) << "no RTM_NEWLINK for the device at all - the watcher saw nothing";
    for (auto const flags : announced) {
        EXPECT_EQ(flags & IFF_LOWER_UP, 0u) << "the device was announced with a carrier";
    }
    EXPECT_EQ(announced.back() & IFF_RUNNING, 0u) << "the device is still announced as running once linkwatch settled";
    EXPECT_EQ(tap.carrier(), std::optional<bool>(false));
}

TEST(tap_carrier, set_carrier_moves_the_kernel_state_both_ways) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_c", "192.0.2.9", true);

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());
    EXPECT_EQ(tap.get_error(), 0);
    EXPECT_TRUE(wait_for_carrier(tap, false, settle_timeout_ms)) << "the carrier never went down";

    ASSERT_TRUE(tap.set_carrier(true)) << "set_carrier(true): " << std::strerror(tap.get_error());
    EXPECT_TRUE(wait_for_carrier(tap, true, settle_timeout_ms)) << "the carrier never came back";

    // Idempotent: setting the state the device already has is not an error.
    EXPECT_TRUE(tap.set_carrier(true));
    EXPECT_EQ(tap.carrier(), std::optional<bool>(true));
}

// A carrier watcher keys on IFF_RUNNING / IFF_LOWER_UP of RTM_NEWLINK; a tap has no operstate string.
TEST(tap_carrier, rtnetlink_reports_the_carrier_change_as_a_flag_change) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_d", "192.0.2.13", true);

    const int ifindex = static_cast<int>(::if_nametoindex("eviocarr_d"));
    ASSERT_NE(ifindex, 0) << "if_nametoindex: " << std::strerror(errno);

    unique_fd watcher = open_link_watcher();
    ASSERT_TRUE(watcher.is_fd()) << "rtnetlink socket: " << std::strerror(errno);

    constexpr unsigned int carrier_flags = IFF_RUNNING | IFF_LOWER_UP;

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());

    auto const down_flags = await_link_flags(watcher, ifindex, carrier_flags, false, settle_timeout_ms);
    ASSERT_TRUE(down_flags.has_value()) << "no RTM_NEWLINK with the carrier flags cleared";
    EXPECT_EQ(*down_flags & IFF_RUNNING, 0u);
    EXPECT_EQ(*down_flags & IFF_LOWER_UP, 0u);
    // IFF_UP is orthogonal to the carrier and stays as bring_device_up left it.
    EXPECT_NE(*down_flags & IFF_UP, 0u);

    ASSERT_TRUE(tap.set_carrier(true)) << "set_carrier(true): " << std::strerror(tap.get_error());

    auto const up_flags = await_link_flags(watcher, ifindex, carrier_flags, true, settle_timeout_ms);
    ASSERT_TRUE(up_flags.has_value()) << "no RTM_NEWLINK with the carrier flags set";
    EXPECT_NE(*up_flags & IFF_RUNNING, 0u);
    EXPECT_NE(*up_flags & IFF_LOWER_UP, 0u);
}

// Why the carrier stays untouched in PLC mode: SLAC sends CM_SET_KEY and the sounding MMEs over an AF_PACKET
// socket before any link exists. With the carrier down linkwatch swaps the qdisc for noop and the enqueue drops
// them, and sendto() still succeeds because packet_snd() checks IFF_UP, not the carrier. The loss is silent.
TEST(tap_carrier, a_raw_socket_send_is_silently_dropped_while_the_carrier_is_off) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_e", "192.0.2.17", true);

    const int ifindex = static_cast<int>(::if_nametoindex("eviocarr_e"));
    ASSERT_NE(ifindex, 0) << "if_nametoindex: " << std::strerror(errno);

    // An AF_PACKET socket needs CAP_NET_RAW, which CAP_NET_ADMIN does not imply.
    auto sender = open_raw_sender(ifindex);
    if (not sender.fd.is_fd()) {
        GTEST_SKIP() << "cannot open an AF_PACKET socket: " << std::strerror(errno) << " - CAP_NET_RAW is required";
    }

    unique_fd watcher = open_link_watcher();
    ASSERT_TRUE(watcher.is_fd()) << "rtnetlink socket: " << std::strerror(errno);
    constexpr unsigned int carrier_flags = IFF_RUNNING | IFF_LOWER_UP;

    // Baseline: with a carrier the frame reaches the tap fd.
    drain_tap(tap);
    ASSERT_TRUE(sender.send(test_frame())) << "raw send with the carrier on: " << std::strerror(errno);
    ASSERT_TRUE(await_test_frame(tap, frame_timeout_ms)) << "the frame did not reach the tap with the carrier on";

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());
    // The linkwatch run that clears these flags also calls dev_deactivate, so the qdisc is gone once announced.
    ASSERT_TRUE(await_link_flags(watcher, ifindex, carrier_flags, false, settle_timeout_ms).has_value())
        << "the carrier-down was never announced";

    drain_tap(tap);
    errno = 0;
    EXPECT_TRUE(sender.send(test_frame()))
        << "sendto reported a failure; it is expected to succeed and drop: " << std::strerror(errno);
    EXPECT_FALSE(await_test_frame(tap, frame_timeout_ms)) << "a frame crossed a device without a carrier";

    ASSERT_TRUE(tap.set_carrier(true)) << "set_carrier(true): " << std::strerror(tap.get_error());
    ASSERT_TRUE(await_link_flags(watcher, ifindex, carrier_flags, true, settle_timeout_ms).has_value())
        << "the carrier-up was never announced";

    drain_tap(tap);
    ASSERT_TRUE(sender.send(test_frame())) << "raw send with the carrier back on: " << std::strerror(errno);
    EXPECT_TRUE(await_test_frame(tap, frame_timeout_ms)) << "the frame did not reach the tap once the carrier returned";
}

// tun_get_user does not consult the carrier, so frames can still be injected from the firmware side.
TEST(tap_carrier, writing_into_the_tap_still_works_while_the_carrier_is_off) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_f", "192.0.2.21", true);

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());
    ASSERT_TRUE(wait_for_carrier(tap, false, settle_timeout_ms)) << "the carrier never went down";

    EXPECT_TRUE(tap.tx(test_frame())) << "tx with the carrier off: " << std::strerror(tap.get_error());
}

// A successful open() reports no error whatever the carrier request did: fd_event_client fails the connection on
// any nonzero policy error after open, and an owner that resets on error would replay the same open() forever.
TEST(tap_carrier, a_successful_open_reports_no_error) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_h", "192.0.2.33", false);

    EXPECT_EQ(tap.get_error(), 0) << "a successful open left an errno that would fail the fresh connection";
    // Without TUNSETCARRIER the errno must appear on the dedicated channel and nowhere else.
    if (tap.carrier_setup_error() != 0) {
        EXPECT_TRUE(tap.carrier_setup_error() == EINVAL or tap.carrier_setup_error() == ENOTTY)
            << "unexpected carrier setup errno: " << std::strerror(tap.carrier_setup_error());
        EXPECT_EQ(tap.get_error(), 0);
    }
}

// Same through tap_client: a tap opened carrier-off must reach the code-0 up-edge its owner keys "connected" on.
TEST(tap_carrier, tap_client_opened_carrier_off_reaches_the_code_zero_up_edge) {
    const int reason = probe_tap_creation();
    if (reason != 0) {
        GTEST_SKIP() << "cannot create TAP devices: " << std::strerror(reason) << " - CAP_NET_ADMIN is required";
    }

    std::vector<int> codes;
    tap_client client("eviocarr_i", "192.0.2.37", test_netmask, test_mtu, false);
    client.set_error_handler([&codes](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, std::chrono::milliseconds(2000), [&codes] { return not codes.empty(); }))
        << "the client never reported a connection state";
    EXPECT_EQ(codes.front(), 0) << "the fresh connection was reported as failed: " << std::strerror(codes.front());
}

TEST(tap_carrier, fd_event_client_fails_a_fresh_connection_on_a_residual_errno) {
    std::vector<int> codes;
    residual_error_client client(EINVAL);
    client.set_error_handler([&codes](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, std::chrono::milliseconds(2000), [&codes] { return not codes.empty(); }))
        << "the client never reported a connection state";
    EXPECT_EQ(codes.front(), EINVAL) << "a residual errno no longer fails the connection; if fd_event_client "
                                        "changed, tap_handler's carrier_setup_error() split may be revisited";
}

TEST(tap_carrier, fd_event_client_reaches_the_code_zero_up_edge_on_a_clean_open) {
    std::vector<int> codes;
    residual_error_client client(0);
    client.set_error_handler([&codes](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, std::chrono::milliseconds(2000), [&codes] { return not codes.empty(); }))
        << "the client never reported a connection state";
    EXPECT_EQ(codes.front(), 0);
}

// carrier() queries by name; after a failed open() the name belongs to somebody else's live device.
TEST(tap_carrier, carrier_reports_nothing_after_a_failed_open) {
    tap_handler owner;
    OPEN_TAP_OR_SKIP(owner, "eviocarr_g", "192.0.2.25", true);

    tap_handler intruder;
    ASSERT_FALSE(intruder.open("eviocarr_g", "192.0.2.29", test_netmask, test_mtu, true))
        << "opening a device name another handler already holds unexpectedly succeeded";
    EXPECT_NE(intruder.get_error(), 0);
    EXPECT_EQ(intruder.carrier(), std::nullopt);

    // The failed open must not have disturbed the handler that does own the device.
    EXPECT_TRUE(wait_for_carrier(owner, true, settle_timeout_ms)) << "the owner's carrier was disturbed";
}

// An unopened handler sends the ioctl to fd -1 and reports EBADF, like one fd_event_client tore down mid-reset.
TEST(tap_carrier, set_carrier_on_an_unopened_handler_fails_without_crashing) {
    tap_handler tap;

    EXPECT_FALSE(tap.set_carrier(true));
    EXPECT_EQ(tap.get_error(), EBADF);
    EXPECT_FALSE(tap.set_carrier(false));
    EXPECT_EQ(tap.get_error(), EBADF);
    // No device name to query, so no answer is invented.
    EXPECT_EQ(tap.carrier(), std::nullopt);
}
