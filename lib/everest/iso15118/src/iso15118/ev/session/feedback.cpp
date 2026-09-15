// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/feedback.hpp>

#include <iso15118/detail/helper.hpp>

namespace iso15118::ev {

Feedback::Feedback(feedback::Callbacks callbacks_) : callbacks(std::move(callbacks_)) {
}

void Feedback::connected(const io::Ipv6EndPoint& endpoint) const {
    call_if_available(callbacks.connected, endpoint);
}

void Feedback::signal(feedback::Signal signal) const {
    call_if_available(callbacks.signal, signal);
}

void Feedback::timed_out() const {
    call_if_available(callbacks.timed_out);
}

void Feedback::stopped() const {
    call_if_available(callbacks.stopped);
}

} // namespace iso15118::ev
