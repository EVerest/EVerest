// SPDX-License-Identifier: Apache-2.0
// Copyright 2025 Pionix GmbH and Contributors to EVerest
#pragma once

#include <iso15118/din/context.hpp>
#include <iso15118/message_din/common_types.hpp>

namespace iso15118::din::state {

namespace dt = message_din::datatypes;

template <typename Response> Response& response_with_code(Response& res, dt::ResponseCode code) {
    res.response_code = code;
    return res;
}

inline void setup_header(message_din::Header& header, const dt::SessionId& session_id) {
    header.session_id = session_id;
}

// Sets the response header to the assigned session id and reports whether the request carried the same
// id ([V2G-DC-391] session id match). SessionSetup is exempt (it assigns the id).
inline bool validate_and_setup_header(message_din::Header& header, const dt::SessionId& session_id,
                                      const dt::SessionId& req_session_id) {
    setup_header(header, session_id);
    return session_id == req_session_id;
}

// Overrides the EVSEIsolationStatus of a DC response with the module's isolation-monitoring result
// (update_isolation_status) when it reported one, so the EV sees the real level (mirrors EvseV2G, which
// reports the module value in every DC response). CableCheckRes applies the same precedence, but inside
// its own response builder (it has three legs with different fallbacks) rather than through this helper.
inline void apply_isolation_status(const Context& ctx, dt::DcEvseStatus& status) {
    if (const auto level = ctx.reported_isolation_level()) {
        status.evse_isolation_status = level.value();
    }
}

} // namespace iso15118::din::state
