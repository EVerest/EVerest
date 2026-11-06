// SPDX-License-Identifier: Apache-2.0
// Copyright Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/session/fsm.hpp>

namespace iso15118::ev::session {

namespace {
using Violation = std::optional<std::string_view>;
} // namespace

Violation disposition_violation(Disposition d, bool consumed, bool has_request, bool session_stopped, bool transitioned,
                                bool handover, bool ongoing_wait) {
    switch (d) {
    case Disposition::Handover:
        return handover ? Violation{} : "Handover without a negotiated other protocol";
    case Disposition::Awaiting:
        return has_request ? Violation{} : "Awaiting without a pending request";
    case Disposition::Stopping:
        return session_stopped ? Violation{} : "Stopping without stop_session()";
    case Disposition::Transitioning:
        if (not transitioned) {
            return "Transitioning without a new state";
        }
        return has_request ? Violation{} : "Transitioning without a pending request";
    case Disposition::Waiting:
        if (not transitioned) {
            return "Waiting without a new state";
        }
        if (has_request) {
            return "Waiting with a pending request";
        }
        return ongoing_wait ? Violation{} : "Waiting without an ongoing timeout";
    case Disposition::Ignored:
        return consumed ? "Ignored but a response was consumed" : Violation{};
    }
    return "unknown disposition";
}

} // namespace iso15118::ev::session
