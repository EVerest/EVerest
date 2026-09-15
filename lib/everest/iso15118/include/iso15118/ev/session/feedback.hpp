// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>

#include <iso15118/io/ipv6_endpoint.hpp>

namespace iso15118::ev::feedback {

enum class Signal {
    DLINK_TERMINATE, // session ended, link may be torn down
    DLINK_PAUSE,     // session paused, keep the link for a resume
    DLINK_ERROR,     // no session was established (SDP timeout, connect/handshake failure) or teardown
};

struct Callbacks {
    std::function<void(const io::Ipv6EndPoint&)> connected;
    std::function<void(Signal)> signal;
    // Fired when the response watchdog expires (a sent request got no response in
    // time). Distinct from stopped, which fires on every session end.
    std::function<void()> timed_out;
    std::function<void()> stopped;
};

} // namespace iso15118::ev::feedback

namespace iso15118::ev {

class Feedback {
public:
    explicit Feedback(feedback::Callbacks);

    void connected(const io::Ipv6EndPoint&) const;
    void signal(feedback::Signal) const;
    void timed_out() const;
    void stopped() const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::ev
