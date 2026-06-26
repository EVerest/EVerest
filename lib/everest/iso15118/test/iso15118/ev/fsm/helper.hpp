// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
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
    FsmStateHelper(const ev::d20::session::feedback::Callbacks& callbacks) :
        ctx(callbacks, msg_exch, logger, evcc_id, control_event) {
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

    ev::d20::MessageExchange& get_message_exchange() {
        return msg_exch;
    }

    template <typename ResponseType> void handle_response(const ResponseType& response) {
        msg_exch.set_response(std::make_unique<message_20::Variant>(response));
    }

private:
    ev::d20::MessageExchange msg_exch{};

    iso15118::session::SessionLogger logger{this};

    message_20::datatypes::Identifier evcc_id{"EVTESTID01"};
    std::optional<ev::d20::ControlEvent> control_event{};

    ev::d20::Context ctx;
};
