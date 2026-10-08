// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest

#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

#include <generated/types/board_support_common.hpp>

namespace module {

/// PP ampacity the BSP may publish before ready() can handle it. Subscribing in ready() would lose
/// a value published earlier, e.g. a cable already in the socket at boot.
class PpAmpacityForwarder {
public:
    using Sink = std::function<void(const types::board_support_common::ProximityPilot&)>;

    /// Forwards pp to the sink, or keeps it as the latest value until connect() runs.
    void publish(const types::board_support_common::ProximityPilot& pp) {
        std::scoped_lock lock(mutex);
        if (sink) {
            sink(pp);
        } else {
            latest = pp;
        }
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
    std::optional<types::board_support_common::ProximityPilot> latest;
};

} // namespace module
