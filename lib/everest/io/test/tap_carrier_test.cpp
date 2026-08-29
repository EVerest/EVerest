// SPDX-License-Identifier: Apache-2.0
// Copyright 2020 - 2026 Pionix GmbH and Contributors to EVerest
//
// Carrier support for TAP devices. Two things are pinned down here: the tap_handler API
// (open(..., carrier_on), set_carrier, carrier) and the kernel behaviour a consumer is
// allowed to rely on. The latter is why the rtnetlink cases earn their length - the contract
// for a carrier watcher is the IFF_LOWER_UP flag of RTM_NEWLINK: not the operstate string,
// which a tap does not maintain, and not IFF_RUNNING on its own, which carries a transient
// false positive when the device is created (see the second case).
//
// The kernel's timing is the thing to keep in mind while reading: TUNSETCARRIER flips
// netif_carrier_ok() at once, but everything derived from it - the operstate behind
// IFF_RUNNING, the qdisc, the rtnetlink notification - is updated by the linkwatch work,
// which dampens itself to roughly one run per second. Hence the waits below; none of them is
// a sleep tuned to make a flaky assertion pass.
//
// Creating a TAP device needs CAP_NET_ADMIN, so every case that opens one skips instead of
// failing when the runner does not have it. tap_handler::open reports EPERM for every
// creation failure (it flattens the exceptions from socket::create_tap_device), so neither the
// skip nor its message can be conditioned on that errno; probe_tap_creation asks the kernel
// directly, which both names the real reason and separates "this environment cannot make TAP
// devices" from "it can, and open() still failed" - the latter being a defect, not a skip.

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

/// The smallest synchronous ClientPolicy fd_event_client accepts, shaped like tap_handler: it opens
/// a pollable fd that never becomes readable, and reports exactly the errno the case asks for. Its
/// only job is to separate "open succeeded and left an errno" from "open succeeded cleanly", which is
/// the distinction tap_handler's carrier_setup_error() exists to keep on the right side of.
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

// <net/if.h> stops at IFF_DYNAMIC; the flag that mirrors netif_carrier_ok() is a kernel
// addition, and pulling in <linux/if.h> next to <net/if.h> collides on struct ifreq.
#ifndef IFF_LOWER_UP
#define IFF_LOWER_UP 0x10000
#endif

constexpr int test_mtu = 1500;
constexpr int mac_address_length = 6;
// A /30 per test out of TEST-NET-1 (RFC 5737): the cases run as separate ctest jobs and may
// overlap in time, and two taps in the same subnet would fight over the same route.
constexpr char test_netmask[] = "255.255.255.252";
// Generous against the ~1 s linkwatch dampening interval, which the kernel may have just
// re-armed for an unrelated device when a case starts.
constexpr int settle_timeout_ms = 4000;
// Frames traverse an activated qdisc without delay, so this only has to cover scheduling.
constexpr int frame_timeout_ms = 500;
// IEEE 802.1 local experimental ethertype: nothing on the host stack claims it, so a frame
// carrying it is unambiguously the one a case sent.
constexpr std::uint8_t test_ethertype_high = 0x88;
constexpr std::uint8_t test_ethertype_low = 0xb5;

/// The kernel's own verdict on creating a TAP device, so a skip can name the real reason
/// instead of tap_handler's flattened EPERM. Returns 0 when a TAP device can be created.
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

/// Open \p tap, or skip the calling test when the environment cannot create a TAP device. When the
/// probe shows the environment *can* create one, a failed open() is a defect in open() rather than a
/// missing privilege, so it fails the test instead of being skipped away - a name collision or a
/// broken configure step would otherwise disappear behind a nonsensical "CAP_NET_ADMIN is required".
/// A macro rather than a function because GTEST_SKIP and FAIL only return from the test body.
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

/// Skip when the running kernel predates TUNSETCARRIER (v5.0). The ioctl reports EINVAL or
/// ENOTTY in that case, which is exactly the distinction tap_handler's caller has to make.
#define SKIP_IF_CARRIER_UNSUPPORTED(tap, ok)                                                                           \
    do {                                                                                                               \
        if (not(ok) and ((tap).get_error() == EINVAL or (tap).get_error() == ENOTTY)) {                                \
            GTEST_SKIP() << "kernel does not implement TUNSETCARRIER: " << std::strerror((tap).get_error());           \
        }                                                                                                              \
    } while (false)

/// Poll \ref tap_handler::carrier until it reports \p expected. Waiting is not optional: the
/// operstate behind IFF_RUNNING is updated by the kernel's linkwatch work, not by the ioctl.
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

/// An rtnetlink socket subscribed to link changes - the consumer's vantage point.
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

/// Feed the ifi_flags of every RTM_NEWLINK for \p ifindex to \p visit, until \p visit returns
/// true or \p timeout_ms elapses. The one netlink read loop the two waits below are built from.
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

/// The ifi_flags of every RTM_NEWLINK for \p ifindex that arrives within \p timeout_ms, in order.
/// Collecting rather than matching is what lets a case assert that something was *never* announced,
/// and it always waits out the whole window.
std::vector<unsigned int> collect_link_flags(int nl_fd, int ifindex, int timeout_ms) {
    std::vector<unsigned int> observed;
    for_each_link_flags(nl_fd, ifindex, timeout_ms, [&observed](unsigned int flags) {
        observed.push_back(flags);
        return false;
    });
    return observed;
}

/// Wait for an RTM_NEWLINK on \p ifindex whose \p mask bits are in the state \p expect_set and
/// report the flags it carried. No value on timeout. Unrelated updates for the same device (an
/// address settling, for instance) are skipped rather than mistaken for the answer.
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

/// Read from the tap until \ref test_frame shows up, ignoring what the host stack emits on its
/// own (IPv6 DAD and MLD, mostly). True when it arrives, false on timeout.
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

/// An AF_PACKET socket and the address of \p ifindex, as a SLAC-style sender would use them.
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

// A device the kernel just created carries the carrier the kernel chose: on. This is the
// backward compatibility guarantee - no existing caller passes carrier_on, and no ioctl is
// issued for them.
TEST(tap_carrier, default_open_leaves_the_carrier_on) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_a", "192.0.2.1", true);

    // Only the settled state is asserted. Reading immediately would rest on the operstate a device
    // happens to start from, which is IF_OPER_UNKNOWN for a device this open() created but DOWN for a
    // pre-existing persistent tap - and the lag documented on carrier() applies to both.
    EXPECT_TRUE(wait_for_carrier(tap, true, settle_timeout_ms)) << "the carrier never came up";
}

// The point of the open() parameter, and the assertion that gives it its value: a watcher that
// subscribes before the device exists is never told the device has a carrier. The announcement
// being foreclosed is the one the IFF_UP transition emits synchronously, which is why open() drops
// the carrier before bringing the device up - a caller doing it afterwards, however promptly,
// cannot retract an announcement the kernel has already sent.
//
// This is also where the consumer contract gets sharper than "watch the flags". IFF_LOWER_UP is
// netif_carrier_ok() read straight out, so it is exact and never announced set here. IFF_RUNNING is
// derived from the operstate, which starts at IF_OPER_UNKNOWN - dev_get_flags reads that as running
// for backward compatibility - and is only corrected when the linkwatch work runs, up to a second
// later. So the bring-up announcement may legitimately carry IFF_RUNNING on a device whose carrier
// is already down, followed by a corrected announcement.
//
// Consequence for a carrier watcher (McsDataLink): key on IFF_LOWER_UP. A watcher keyed on
// IFF_RUNNING alone would report a spurious carrier-up for up to a second every time the tap is
// created or re-created after a reset - which for D-LINK_READY is exactly the wrong answer at
// exactly the wrong moment. The loop below asserts both halves of that: LOWER_UP never set, and any
// IFF_RUNNING seen is transient and corrected before the window closes.
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

// The consumer contract. A carrier watcher (McsDataLink and anything else supervising an SPE
// link) must key on the IFF_RUNNING / IFF_LOWER_UP flags of RTM_NEWLINK. A tap does not
// maintain a meaningful operstate string, so a watcher keyed on operstate == "up" would be
// blind here while staying correct on a physical netdev - the failure this case forecloses.
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

// Why carrier must stay untouched in PLC mode. HomePlug SLAC exchanges CM_SET_KEY and the
// sounding MMEs over an AF_PACKET socket on this device before any link exists, and with the
// carrier down those frames never leave the host: linkwatch swaps the qdisc for noop and the
// enqueue drops them.
//
// Measured, and worse than the plan assumed: sendto() still *succeeds*. packet_snd() checks
// IFF_UP, not the carrier, so a raw sender gets no ENETDOWN and no errno of any kind - the loss
// is silent. A SLAC implementation therefore cannot detect this condition at all, which is the
// argument for never dropping the carrier on a device carrying HomePlug traffic.
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

    // The baseline: with a carrier, the frame reaches the device and shows up on the tap fd.
    // Without it the drop below would prove nothing about the carrier.
    drain_tap(tap);
    ASSERT_TRUE(sender.send(test_frame())) << "raw send with the carrier on: " << std::strerror(errno);
    ASSERT_TRUE(await_test_frame(tap, frame_timeout_ms)) << "the frame did not reach the tap with the carrier on";

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());
    // The same linkwatch run that clears these flags calls dev_deactivate, so the notification
    // is the point from which the qdisc is known to be gone.
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

// The other direction: tun_get_user does not consult the carrier, so the bridge can still
// inject frames from the firmware side while the carrier is down. Documented, not relied upon -
// when the link is down the firmware has nothing to forward anyway.
TEST(tap_carrier, writing_into_the_tap_still_works_while_the_carrier_is_off) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_f", "192.0.2.21", true);

    const bool down_ok = tap.set_carrier(false);
    SKIP_IF_CARRIER_UNSUPPORTED(tap, down_ok);
    ASSERT_TRUE(down_ok) << "set_carrier(false): " << std::strerror(tap.get_error());
    ASSERT_TRUE(wait_for_carrier(tap, false, settle_timeout_ms)) << "the carrier never went down";

    EXPECT_TRUE(tap.tx(test_frame())) << "tx with the carrier off: " << std::strerror(tap.get_error());
}

// The contract that keeps the carrier feature from breaking the client it is used through: a
// successful open() reports no error, whatever the carrier request did. fd_event_client reads the
// policy's error immediately after a successful open and fails the fresh connection on anything
// nonzero, so a residual errno here does not degrade to a warning - it tears the device down, and an
// owner that resets on error replays the same open() and gets the same residual, forever. The two
// framework cases below demonstrate exactly that, on a stub policy rather than on a tap.
TEST(tap_carrier, a_successful_open_reports_no_error) {
    tap_handler tap;
    OPEN_TAP_OR_SKIP(tap, "eviocarr_h", "192.0.2.33", false);

    EXPECT_EQ(tap.get_error(), 0) << "a successful open left an errno that would fail the fresh connection";
    // On a kernel with TUNSETCARRIER nothing failed, so both channels read zero. On one without, the
    // errno must appear on the dedicated channel and nowhere else - that is the whole point of it.
    if (tap.carrier_setup_error() != 0) {
        EXPECT_TRUE(tap.carrier_setup_error() == EINVAL or tap.carrier_setup_error() == ENOTTY)
            << "unexpected carrier setup errno: " << std::strerror(tap.carrier_setup_error());
        EXPECT_EQ(tap.get_error(), 0);
    }
}

// The end-to-end version of the case above, through the client the bridge actually uses: a tap opened
// carrier-off must still reach the code-0 up-edge its owner keys "connected" on.
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

// A residual errno after a successful open is not a warning the framework tolerates.
TEST(tap_carrier, fd_event_client_fails_a_fresh_connection_on_a_residual_errno) {
    std::vector<int> codes;
    residual_error_client client(EINVAL);
    client.set_error_handler([&codes](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, std::chrono::milliseconds(2000), [&codes] { return not codes.empty(); }))
        << "the client never reported a connection state";
    EXPECT_EQ(codes.front(), EINVAL) << "a residual errno no longer fails the connection; if fd_event_client "
                                        "changed, tap_handler's carrier_setup_error() split may be revisited";
}

// ... while a policy that keeps its error clean reaches the up-edge, which is what tap_handler does.
TEST(tap_carrier, fd_event_client_reaches_the_code_zero_up_edge_on_a_clean_open) {
    std::vector<int> codes;
    residual_error_client client(0);
    client.set_error_handler([&codes](int code, std::string const&) { codes.push_back(code); });

    ASSERT_TRUE(pump_until(client, std::chrono::milliseconds(2000), [&codes] { return not codes.empty(); }))
        << "the client never reported a connection state";
    EXPECT_EQ(codes.front(), 0);
}

// carrier() queries by name, so it has to answer for the device this handler owns and no other. A
// failed open() is the case where those diverge: the name is taken precisely because somebody else
// holds a live device under it, and reporting that device's carrier as this handler's would be a
// confident wrong answer - the worst kind for a supervision input.
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

// The handler is a thin syscall wrapper with no state of its own, so an unopened one has to
// answer without a device: the ioctl goes to fd -1 and reports EBADF. This is also the shape of
// a handler that fd_event_client has torn down mid-reset.
TEST(tap_carrier, set_carrier_on_an_unopened_handler_fails_without_crashing) {
    tap_handler tap;

    EXPECT_FALSE(tap.set_carrier(true));
    EXPECT_EQ(tap.get_error(), EBADF);
    EXPECT_FALSE(tap.set_carrier(false));
    EXPECT_EQ(tap.get_error(), EBADF);
    // No device name to query, so no answer is invented.
    EXPECT_EQ(tap.carrier(), std::nullopt);
}
