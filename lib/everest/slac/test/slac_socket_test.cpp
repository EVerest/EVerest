// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
//
// slac_socket as the client's policy: what it does with a frame it can never send, and how it
// opens, so a stalled modem is back-pressure on the fd rather than a device queue filling behind
// the kernel's back. Both need a raw socket on `lo`, which requires CAP_NET_RAW; they skip without
// it. Run as root, in CI's container, or locally under
// `unshare -rn sh -c 'ip link set lo up && slac_socket_test'`.
#include <cstdio>
#include <everest/slac/HomeplugMessage.hpp>
#include <everest/slac/slac_socket.hpp>
#include <linux/if_ether.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace everest::lib::slac;

namespace {

bool assert_true(bool cond, const char* test_name, const char* details) {
    if (not cond) {
        std::printf("[%s] FAIL: %s\n", test_name, details);
        return false;
    }
    return true;
}

bool open_or_skip(slac_socket& socket, const char* test_name) {
    if (socket.open("lo")) {
        return true;
    }
    std::printf("[SKIP] %s: raw socket on lo not available (%s); needs CAP_NET_RAW, e.g. "
                "`unshare -rn sh -c 'ip link set lo up && slac_socket_test'`\n",
                test_name, socket.get_error_message().c_str());
    return false;
}

int send_buffer_of(int fd) {
    int value = -1;
    socklen_t len = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &value, &len) != 0) {
        return -1;
    }
    return value;
}

// The kernel takes frames into its own memory up to the send buffer. At the default size a modem
// that stops taking frames is invisible until the device queue overflows; at the minimum the
// socket's own memory fills first and the fd stops being writable, which the client can wait for.
bool test_open_sets_the_send_buffer_to_the_minimum() {
    const char* test_name = "open_sets_the_send_buffer_to_the_minimum";
    slac_socket socket;
    if (not open_or_skip(socket, test_name)) {
        return true;
    }
    int const reference = ::socket(AF_PACKET, SOCK_RAW, 0);
    if (!assert_true(reference >= 0, test_name, "could not open a reference raw socket")) {
        return false;
    }
    auto const default_size = send_buffer_of(reference);
    ::close(reference);
    auto const size = send_buffer_of(socket.get_fd());
    if (!assert_true(size > 0 and default_size > 0, test_name, "could not read SO_SNDBUF")) {
        return false;
    }
    return assert_true(size < default_size, test_name, "the send buffer was left at its default size");
}

// A frame the socket itself rejects is dropped and counted as sent. Returning false with no error
// would mean "wait for writable", and a raw socket is always writable, so the client would retry
// the same frame on every pass and nothing queued behind it would ever go out.
bool test_an_invalid_frame_is_dropped_not_retried() {
    const char* test_name = "an_invalid_frame_is_dropped_not_retried";
    slac_socket socket;
    if (not open_or_skip(socket, test_name)) {
        return true;
    }
    messages::HomeplugMessage empty;
    if (!assert_true(not empty.is_valid(), test_name, "an empty message counts as valid")) {
        return false;
    }
    bool ok = assert_true(socket.tx(empty), test_name, "the invalid frame was kept for a retry");
    ok = assert_true(socket.get_error() == 0, test_name, "the invalid frame was recorded as a socket error") and ok;
    return ok;
}

} // namespace

int main() {
    int failed = 0;
    struct {
        const char* name;
        bool (*fn)();
    } const tests[] = {
        {"open_sets_the_send_buffer_to_the_minimum", test_open_sets_the_send_buffer_to_the_minimum},
        {"an_invalid_frame_is_dropped_not_retried", test_an_invalid_frame_is_dropped_not_retried},
    };
    for (auto const& t : tests) {
        if (t.fn()) {
            std::printf("[PASS] %s\n", t.name);
        } else {
            std::printf("[FAIL] %s\n", t.name);
            ++failed;
        }
    }
    return failed == 0 ? 0 : 1;
}
