// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <mutex>
#include <optional>
#include <queue>

#include <iso15118/control_event.hpp>

namespace iso15118::session {

class ControlEventQueue {
public:
    std::optional<ControlEvent> pop();
    void push(ControlEvent);

private:
    std::queue<ControlEvent> queue;
    std::mutex mutex;
};

} // namespace iso15118::session
