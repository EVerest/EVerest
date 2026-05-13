// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <array>

#include <everest/util/fsm/fsm.hpp>
#include <iso15118/ev/d20/context.hpp>
#include <iso15118/ev/d20/states.hpp>
#include <iso15118/message/variant.hpp>
#include <iso15118/session/logger.hpp>

using namespace iso15118;

class FsmStateHelper {
public:
    FsmStateHelper(const ev::d20::session::feedback::Callbacks& callbacks) : ctx(callbacks, msg_exch, logger) {
        // Install a no-op session log callback so SessionLogger::event() does not throw bad_function_call
        // when state enter() invokes m_ctx.log.enter_state(...). Tests that need to capture log output
        // override this callback themselves and reset it at the end of the test case.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    };

    ~FsmStateHelper() {
        // Reset the global session log callback so it does not leak across TEST_CASEs.
        iso15118::session::logging::set_session_log_callback(
            [](std::size_t, const iso15118::session::logging::Event&) {});
    }

    ev::d20::Context& get_context();

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        msg_exch.set_response(std::make_unique<message_20::Variant>(response));
    }

private:
    // TODO(SL): Check how to remove output_buffer & output_stream_view
    std::array<uint8_t, 1024> output_buffer{};
    io::StreamOutputView output_stream_view{output_buffer.data(), output_buffer.size()};

    ev::d20::MessageExchange msg_exch{output_stream_view};

    iso15118::session::SessionLogger logger{this};

    ev::d20::Context ctx;
};
