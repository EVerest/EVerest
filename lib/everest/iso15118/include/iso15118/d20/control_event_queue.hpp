// SPDX-License-Identifier: Apache-2.0
// Copyright 2023 Pionix GmbH and Contributors to EVerest
#pragma once

#include <optional>
#include <queue>

#include "control_event.hpp"

#include <iso15118/detail/mutex.hpp>

namespace iso15118::d20 {

class ControlEventQueue {
public:
    std::optional<ControlEvent> pop();
    void push(ControlEvent);

private:
    std::queue<ControlEvent> queue;
    detail::Mutex mutex;
};

} // namespace iso15118::d20
