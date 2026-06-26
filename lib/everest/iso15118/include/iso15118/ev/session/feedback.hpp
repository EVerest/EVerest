// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#pragma once

#include <functional>

#include <iso15118/ev/d20/evse_session_info.hpp>
#include <iso15118/io/ipv6_endpoint.hpp>
#include <iso15118/message/session_setup.hpp>
#include <iso15118/message/type.hpp>

namespace iso15118::ev::feedback {

struct Callbacks {
    std::function<void(const io::Ipv6EndPoint&)> connected;
    std::function<void(message_20::Type)> v2g_message;
    std::function<void(const message_20::SessionSetupResponse&)> session_setup_response;
    // Fired when the response watchdog expires (a sent request got no response in
    // time). Distinct from stopped, which fires on every session end.
    std::function<void()> timed_out;
    std::function<void()> stopped;
    std::function<void(const d20::EVSESessionInfo&)> evse_session_info;
};

} // namespace iso15118::ev::feedback

namespace iso15118::ev {

class Feedback {
public:
    explicit Feedback(feedback::Callbacks);

    void connected(const io::Ipv6EndPoint&) const;
    void v2g_message(message_20::Type) const;
    void session_setup_response(const message_20::SessionSetupResponse&) const;
    void timed_out() const;
    void stopped() const;
    void evse_session_info(const d20::EVSESessionInfo&) const;

private:
    feedback::Callbacks callbacks;
};

} // namespace iso15118::ev

// Backward-compat aliases so the oracle tests' iso15118::ev::d20::session
// spelling keeps compiling; the canonical types live in iso15118::ev.
namespace iso15118::ev::d20::session {
namespace feedback {
using Callbacks = ::iso15118::ev::feedback::Callbacks;
} // namespace feedback
using Feedback = ::iso15118::ev::Feedback;
} // namespace iso15118::ev::d20::session
