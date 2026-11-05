// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace module {

/// Value a requirement may publish before ready() can handle it. Subscribing in ready() would lose
/// a value published earlier, e.g. a cable already in the socket at boot.
template <typename T> class LatestValueForwarder {
public:
    using Sink = std::function<void(const T&)>;

    /// Forwards value to the sink, or keeps it as the latest value until connect() runs. Returns whether the
    /// sink received it, so a caller can follow up outside the lock.
    bool publish(const T& value) {
        std::scoped_lock lock(mutex);
        if (sink) {
            sink(value);
            return true;
        }
        latest = value;
        return false;
    }

    /// Sets the sink and replays the latest value kept so far. The sink runs under the lock, so a
    /// value published concurrently is delivered after the replay, never before it.
    void connect(Sink s) {
        std::scoped_lock lock(mutex);
        sink = std::move(s);
        if (latest.has_value()) {
            sink(*latest);
            latest.reset();
        }
    }

private:
    std::mutex mutex;
    Sink sink;
    std::optional<T> latest;
};

} // namespace module
