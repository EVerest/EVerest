// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Pionix GmbH and Contributors to EVerest
#include <iso15118/ev/fsm_base.hpp>

namespace iso15118::ev {

const char* disposition_violation(Disposition d, bool consumed, bool has_request, bool session_stopped,
                                  bool transitioned, bool handover) {
    switch (d) {
    case Disposition::Handover:
        return handover ? nullptr : "Handover without a negotiated other protocol";
    case Disposition::Awaiting:
        return has_request ? nullptr : "Awaiting without a pending request";
    case Disposition::Stopping:
        return session_stopped ? nullptr : "Stopping without stop_session()";
    case Disposition::Transitioning:
        return transitioned ? nullptr : "Transitioning without a new state";
    case Disposition::Ignored:
        return consumed ? "Ignored but a response was consumed" : nullptr;
    }
    return "unknown disposition";
}

} // namespace iso15118::ev
