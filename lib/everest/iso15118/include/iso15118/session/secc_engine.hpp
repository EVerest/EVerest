// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <cstddef>
#include <optional>

#include <iso15118/d20/control_event.hpp>
#include <iso15118/d20/timeout.hpp>
#include <iso15118/io/sdp.hpp>
#include <iso15118/io/stream_view.hpp>
#include <iso15118/message/type.hpp>
#include <iso15118/message/v2g_message_type.hpp>

namespace iso15118 {

namespace session::feedback {
enum class SessionStopAction;
} // namespace session::feedback

// An outgoing response staged by an engine, its payload sitting in the Session's output buffer.
struct SeccOutgoing {
    size_t payload_size;
    io::v2gtp::PayloadType payload_type;
    V2gMessageType message_type;
};

// The engines are held by value in a std::variant and dispatched with std::visit -- there is no
// virtual base class, so a missing or mistyped member is a compile error at the Session's visiting
// call site rather than here.

} // namespace iso15118
