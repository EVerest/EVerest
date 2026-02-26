// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>

#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/fsm/fsm.hpp>
#include <iso15118/message/variant.hpp>

using namespace iso15118;

class FsmStateHelper {
public:
    FsmStateHelper(const ev::d20::session::feedback::Callbacks& callbacks) : ctx(callbacks, msg_exch){};

    ev::d20::Context& get_context();

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        msg_exch.set_response(std::make_unique<message_20::Variant>(response));
    }

private:
    // TODO(SL): Check how to remove output_buffer & output_stream_view
    std::array<uint8_t, 1024> output_buffer{};
    io::StreamOutputView output_stream_view{output_buffer.data(), output_buffer.size()};

    ev::d20::MessageExchange msg_exch{output_stream_view};

    ev::d20::Context ctx;
};
